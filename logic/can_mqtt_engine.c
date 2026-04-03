/**
 * @file can_mqtt_engine.c
 * @brief 板端 CAN→MQTT 规则引擎实现
 */

#include "can_mqtt_engine.h"
#include "mqtt_client.h"
#include "../utils/logger.h"

#include <json-c/json.h>

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/*  内部状态                                                             */
/* ------------------------------------------------------------------ */

static can_mqtt_rules_t g_rules = {
    .version      = 1,
    .updated_at   = 0,
    .rules        = NULL,
    .rule_count   = 0,
    .rule_capacity = 0,
};

static pthread_mutex_t g_rules_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_initialized = false;

/* 规则文件候选路径 */
static const char * const RULE_PATHS[] = {
    "/mnt/UDISK/can_mqtt_rules.json",
    "/mnt/SDCARD/can_mqtt_rules.json",
    NULL,
};

/* ------------------------------------------------------------------ */
/*  工具函数                                                             */
/* ------------------------------------------------------------------ */

/**
 * 从 CAN 数据中提取信号值（Intel / Motorola 字节序）
 */
static uint64_t extract_signal_raw(const uint8_t *data, int dlc,
                                   const can_mqtt_decode_t *dec)
{
    uint64_t raw = 0;

    if (dec->byte_order == CAN_MQTT_BYTE_ORDER_LITTLE_ENDIAN) {
        /* Intel 字节序：start_bit 是 LSB 位置 */
        for (int i = 0; i < dec->bit_length; i++) {
            int bit_pos     = dec->start_bit + i;
            int byte_idx    = bit_pos / 8;
            int bit_in_byte = bit_pos % 8;
            if (byte_idx < dlc && byte_idx < 8) {
                if (data[byte_idx] & (1u << bit_in_byte)) {
                    raw |= ((uint64_t)1 << i);
                }
            }
        }
    } else {
        /* Motorola 字节序（大端）：
         * start_bit 直接采用 DBC/XLS "Motorola bit 编号"：
         *   bit 0  = 字节0 的 MSB（物理位7），bit 7  = 字节0 的 LSB（物理位0）
         *   bit 8  = 字节1 的 MSB（物理位7），bit 15 = 字节1 的 LSB（物理位0）
         *   以此类推：物理 bit_in_byte = 7 - (start_bit % 8)
         * 从 MSB 向 LSB 顺序读取，跨字节时 byte_idx++, phys_bit 回到 7。
         */
        int byte_idx    = dec->start_bit / 8;
        int bit_in_byte = 7 - (dec->start_bit % 8);
        for (int i = dec->bit_length - 1; i >= 0; i--) {
            if (byte_idx < dlc && byte_idx < 8) {
                if (data[byte_idx] & (1u << bit_in_byte)) {
                    raw |= ((uint64_t)1 << i);
                }
            }
            if (bit_in_byte > 0) {
                bit_in_byte--;
            } else {
                byte_idx++;
                bit_in_byte = 7;
            }
        }
    }
    return raw;
}

/**
 * 将原始整数值转换为物理值（处理符号位、系数、偏移）
 */
static double raw_to_physical(uint64_t raw, const can_mqtt_decode_t *dec)
{
    double value;
    if (dec->is_signed && dec->bit_length > 0) {
        int64_t signed_raw = (int64_t)raw;
        uint64_t sign_bit  = (uint64_t)1 << (dec->bit_length - 1);
        if (raw & sign_bit) {
            /* 符号扩展 */
            signed_raw = (int64_t)(raw | (~((sign_bit << 1) - 1)));
        }
        value = (double)signed_raw;
    } else {
        value = (double)(uint64_t)raw;
    }
    return value * dec->factor + dec->offset;
}

/**
 * 替换 topic 模板中的占位符
 * 支持：{topic_prefix} {device_id} {message_name} {signal_name} {channel}
 */
static void build_topic(char *out, size_t out_size,
                        const char *tmpl,
                        const char *topic_prefix,
                        const char *device_id,
                        const char *message_name,
                        const char *signal_name,
                        const char *channel)
{
    char tmp[512];
    const char *src = tmpl;
    char *dst = tmp;
    size_t remaining = sizeof(tmp) - 1;

    while (*src && remaining > 0) {
        if (src[0] == '{') {
            const char *end = strchr(src, '}');
            if (end) {
                size_t key_len = (size_t)(end - src - 1);
                char key[64] = {0};
                if (key_len < sizeof(key)) {
                    strncpy(key, src + 1, key_len);
                }
                const char *val = "";
                if (strcmp(key, "topic_prefix")  == 0) val = topic_prefix  ? topic_prefix  : "";
                else if (strcmp(key, "device_id")    == 0) val = device_id    ? device_id    : "";
                else if (strcmp(key, "message_name") == 0) val = message_name ? message_name : "";
                else if (strcmp(key, "signal_name")  == 0) val = signal_name  ? signal_name  : "";
                else if (strcmp(key, "channel")      == 0) val = channel      ? channel      : "";

                size_t vlen = strlen(val);
                if (vlen > remaining) vlen = remaining;
                memcpy(dst, val, vlen);
                dst += vlen;
                remaining -= vlen;
                src = end + 1;
                continue;
            }
        }
        *dst++ = *src++;
        remaining--;
    }
    *dst = '\0';
    strncpy(out, tmp, out_size - 1);
    out[out_size - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/*  规则内存管理                                                          */
/* ------------------------------------------------------------------ */

static void rules_free_data(can_mqtt_rules_t *r)
{
    if (r->rules) {
        free(r->rules);
        r->rules = NULL;
    }
    r->rule_count    = 0;
    r->rule_capacity = 0;
}

/* ------------------------------------------------------------------ */
/*  JSON 解析辅助                                                         */
/* ------------------------------------------------------------------ */

static const char *jstr(json_object *obj, const char *key, const char *def)
{
    json_object *v = NULL;
    if (json_object_object_get_ex(obj, key, &v) && json_object_is_type(v, json_type_string)) {
        return json_object_get_string(v);
    }
    return def;
}

static int jint(json_object *obj, const char *key, int def)
{
    json_object *v = NULL;
    if (json_object_object_get_ex(obj, key, &v)) {
        return json_object_get_int(v);
    }
    return def;
}

static double jdbl(json_object *obj, const char *key, double def)
{
    json_object *v = NULL;
    if (json_object_object_get_ex(obj, key, &v)) {
        return json_object_get_double(v);
    }
    return def;
}

static bool jbool(json_object *obj, const char *key, bool def)
{
    json_object *v = NULL;
    if (json_object_object_get_ex(obj, key, &v)) {
        return json_object_get_boolean(v) ? true : false;
    }
    return def;
}

static can_mqtt_rule_t parse_rule(json_object *jrule)
{
    can_mqtt_rule_t r;
    memset(&r, 0, sizeof(r));

    strncpy(r.id,           jstr(jrule, "id",       ""),   sizeof(r.id)   - 1);
    strncpy(r.name,         jstr(jrule, "name",     ""),   sizeof(r.name) - 1);
    r.enabled  = jbool(jrule, "enabled",  true);
    r.priority = jint(jrule, "priority", 0);

    /* match */
    json_object *jmatch = NULL;
    if (json_object_object_get_ex(jrule, "match", &jmatch)) {
        strncpy(r.channel, jstr(jmatch, "channel", "any"), sizeof(r.channel) - 1);
        r.can_id      = (uint32_t)jint(jmatch, "can_id", 0);
        r.is_extended = jbool(jmatch, "is_extended", false);
        r.match_any_id= jbool(jmatch, "match_any_id", false);
    } else {
        strncpy(r.channel, "any", sizeof(r.channel) - 1);
        r.match_any_id = true;
    }

    /* source */
    json_object *jsrc = NULL;
    if (json_object_object_get_ex(jrule, "source", &jsrc)) {
        strncpy(r.message_name, jstr(jsrc, "message_name", ""), sizeof(r.message_name) - 1);
        strncpy(r.signal_name,  jstr(jsrc, "signal_name",  ""), sizeof(r.signal_name)  - 1);
    }

    /* decode */
    json_object *jdec = NULL;
    if (json_object_object_get_ex(jrule, "decode", &jdec)) {
        r.decode.start_bit  = jint(jdec, "start_bit",  0);
        r.decode.bit_length = jint(jdec, "bit_length", 8);
        const char *bo = jstr(jdec, "byte_order", "little_endian");
        r.decode.byte_order = (strcmp(bo, "big_endian") == 0)
                              ? CAN_MQTT_BYTE_ORDER_BIG_ENDIAN
                              : CAN_MQTT_BYTE_ORDER_LITTLE_ENDIAN;
        r.decode.is_signed  = jbool(jdec, "signed", false);
        r.decode.factor     = jdbl(jdec, "factor", 1.0);
        r.decode.offset     = jdbl(jdec, "offset", 0.0);
        strncpy(r.decode.unit, jstr(jdec, "unit", ""), sizeof(r.decode.unit) - 1);
    } else {
        r.decode.bit_length = 8;
        r.decode.factor     = 1.0;
    }

    /* mqtt */
    json_object *jmqtt = NULL;
    if (json_object_object_get_ex(jrule, "mqtt", &jmqtt)) {
        strncpy(r.mqtt.topic_template,
                jstr(jmqtt, "topic_template",
                     "{topic_prefix}/device/{device_id}/signals/{message_name}/{signal_name}"),
                sizeof(r.mqtt.topic_template) - 1);
        const char *pm = jstr(jmqtt, "payload_mode", "json");
        r.mqtt.payload_mode = (strcmp(pm, "raw") == 0)
                              ? CAN_MQTT_PAYLOAD_RAW
                              : CAN_MQTT_PAYLOAD_JSON;
        r.mqtt.qos    = jint(jmqtt, "qos", 1);
        r.mqtt.retain = jbool(jmqtt, "retain", false);
    } else {
        strncpy(r.mqtt.topic_template,
                "{topic_prefix}/device/{device_id}/signals/{message_name}/{signal_name}",
                sizeof(r.mqtt.topic_template) - 1);
        r.mqtt.qos = 1;
    }

    return r;
}

/* ------------------------------------------------------------------ */
/*  JSON 序列化                                                           */
/* ------------------------------------------------------------------ */

/**
 * 将规则集序列化为 JSON 字符串（malloc 分配，调用方负责 free）
 */
static char *rules_to_json_str(const can_mqtt_rules_t *r)
{
    json_object *root = json_object_new_object();
    json_object_object_add(root, "version",    json_object_new_int(r->version));
    json_object_object_add(root, "updated_at", json_object_new_int64((int64_t)r->updated_at));

    json_object *jarr = json_object_new_array();
    for (int i = 0; i < r->rule_count; i++) {
        const can_mqtt_rule_t *rule = &r->rules[i];
        json_object *jrule = json_object_new_object();

        json_object_object_add(jrule, "id",       json_object_new_string(rule->id));
        json_object_object_add(jrule, "name",     json_object_new_string(rule->name));
        json_object_object_add(jrule, "enabled",  json_object_new_boolean(rule->enabled));
        json_object_object_add(jrule, "priority", json_object_new_int(rule->priority));

        /* match */
        json_object *jmatch = json_object_new_object();
        json_object_object_add(jmatch, "channel",     json_object_new_string(rule->channel));
        json_object_object_add(jmatch, "can_id",      json_object_new_int((int)rule->can_id));
        json_object_object_add(jmatch, "is_extended", json_object_new_boolean(rule->is_extended));
        json_object_object_add(jmatch, "match_any_id",json_object_new_boolean(rule->match_any_id));
        json_object_object_add(jrule, "match", jmatch);

        /* source */
        json_object *jsrc = json_object_new_object();
        json_object_object_add(jsrc, "type",         json_object_new_string("manual_field"));
        json_object_object_add(jsrc, "message_name", json_object_new_string(rule->message_name));
        json_object_object_add(jsrc, "signal_name",  json_object_new_string(rule->signal_name));
        json_object_object_add(jrule, "source", jsrc);

        /* decode */
        json_object *jdec = json_object_new_object();
        json_object_object_add(jdec, "mode",       json_object_new_string("manual_field"));
        json_object_object_add(jdec, "start_bit",  json_object_new_int(rule->decode.start_bit));
        json_object_object_add(jdec, "bit_length", json_object_new_int(rule->decode.bit_length));
        json_object_object_add(jdec, "byte_order",
            json_object_new_string(rule->decode.byte_order == CAN_MQTT_BYTE_ORDER_BIG_ENDIAN
                                   ? "big_endian" : "little_endian"));
        json_object_object_add(jdec, "signed",     json_object_new_boolean(rule->decode.is_signed));
        json_object_object_add(jdec, "factor",     json_object_new_double(rule->decode.factor));
        json_object_object_add(jdec, "offset",     json_object_new_double(rule->decode.offset));
        json_object_object_add(jdec, "unit",       json_object_new_string(rule->decode.unit));
        json_object_object_add(jrule, "decode", jdec);

        /* mqtt */
        json_object *jmqtt = json_object_new_object();
        json_object_object_add(jmqtt, "topic_template",
                               json_object_new_string(rule->mqtt.topic_template));
        json_object_object_add(jmqtt, "payload_mode",
            json_object_new_string(rule->mqtt.payload_mode == CAN_MQTT_PAYLOAD_RAW ? "raw" : "json"));
        json_object_object_add(jmqtt, "qos",    json_object_new_int(rule->mqtt.qos));
        json_object_object_add(jmqtt, "retain", json_object_new_boolean(rule->mqtt.retain));
        json_object_object_add(jrule, "mqtt", jmqtt);

        json_object_array_add(jarr, jrule);
    }
    json_object_object_add(root, "rules", jarr);

    const char *raw = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    char *result = raw ? strdup(raw) : NULL;
    json_object_put(root);
    return result;
}

/* ------------------------------------------------------------------ */
/*  公开 API                                                             */
/* ------------------------------------------------------------------ */

int can_mqtt_engine_init(void)
{
    if (g_initialized) {
        return 0;
    }
    pthread_mutex_lock(&g_rules_mutex);
    g_rules.version    = 1;
    g_rules.updated_at = 0;
    rules_free_data(&g_rules);
    g_initialized = true;
    pthread_mutex_unlock(&g_rules_mutex);

    char used_path[256] = {0};
    int ret = can_mqtt_engine_load_rules_best(used_path, sizeof(used_path));
    if (ret == 0) {
        log_info("CAN-MQTT引擎：加载规则 %s（共 %d 条）", used_path, g_rules.rule_count);
    } else {
        log_info("CAN-MQTT引擎：未找到规则文件，使用空规则");
    }
    return 0;
}

void can_mqtt_engine_deinit(void)
{
    pthread_mutex_lock(&g_rules_mutex);
    rules_free_data(&g_rules);
    g_initialized = false;
    pthread_mutex_unlock(&g_rules_mutex);
}

int can_mqtt_engine_load_rules(const char *path)
{
    if (!path) return -1;

    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 2 * 1024 * 1024) {
        fclose(f);
        return -1;
    }

    char *buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[nread] = '\0';

    int ret = can_mqtt_engine_set_rules_json(buf);
    free(buf);
    return ret;
}

int can_mqtt_engine_save_rules(const char *path)
{
    if (!path) return -1;

    pthread_mutex_lock(&g_rules_mutex);
    char *json = rules_to_json_str(&g_rules);
    pthread_mutex_unlock(&g_rules_mutex);

    if (!json) return -1;

    FILE *f = fopen(path, "w");
    if (!f) {
        free(json);
        return -1;
    }
    fputs(json, f);
    fclose(f);
    free(json);
    return 0;
}

int can_mqtt_engine_load_rules_best(char *used_path, size_t used_path_size)
{
    for (int i = 0; RULE_PATHS[i]; i++) {
        if (access(RULE_PATHS[i], R_OK) == 0) {
            int ret = can_mqtt_engine_load_rules(RULE_PATHS[i]);
            if (ret == 0) {
                if (used_path && used_path_size > 0) {
                    strncpy(used_path, RULE_PATHS[i], used_path_size - 1);
                    used_path[used_path_size - 1] = '\0';
                }
                return 0;
            }
        }
    }
    return -1;
}

int can_mqtt_engine_save_rules_best(char *used_path, size_t used_path_size)
{
    for (int i = 0; RULE_PATHS[i]; i++) {
        /* 尝试写入 */
        int ret = can_mqtt_engine_save_rules(RULE_PATHS[i]);
        if (ret == 0) {
            if (used_path && used_path_size > 0) {
                strncpy(used_path, RULE_PATHS[i], used_path_size - 1);
                used_path[used_path_size - 1] = '\0';
            }
            return 0;
        }
    }
    return -1;
}

int can_mqtt_engine_set_rules_json(const char *json_str)
{
    if (!json_str) return -1;

    json_object *root = json_tokener_parse(json_str);
    if (!root) {
        log_warn("CAN-MQTT引擎：JSON 解析失败");
        return -1;
    }

    /* 读取版本 */
    int version = jint(root, "version", 1);

    /* 读取规则数组 */
    json_object *jarr = NULL;
    if (!json_object_object_get_ex(root, "rules", &jarr)
        || !json_object_is_type(jarr, json_type_array)) {
        json_object_put(root);
        log_warn("CAN-MQTT引擎：JSON 中缺少 rules 数组");
        return -1;
    }

    int count = (int)json_object_array_length(jarr);

    can_mqtt_rule_t *new_rules = NULL;
    if (count > 0) {
        new_rules = (can_mqtt_rule_t *)malloc((size_t)count * sizeof(can_mqtt_rule_t));
        if (!new_rules) {
            json_object_put(root);
            return -1;
        }
        for (int i = 0; i < count; i++) {
            json_object *jr = json_object_array_get_idx(jarr, i);
            new_rules[i] = parse_rule(jr);
        }
    }

    json_object_put(root);

    pthread_mutex_lock(&g_rules_mutex);
    rules_free_data(&g_rules);
    g_rules.version      = version;
    g_rules.updated_at   = time(NULL);
    g_rules.rules        = new_rules;
    g_rules.rule_count   = count;
    g_rules.rule_capacity = count;
    pthread_mutex_unlock(&g_rules_mutex);

    log_info("CAN-MQTT引擎：已加载 %d 条规则", count);
    return 0;
}

int can_mqtt_engine_get_rules_json(char *buffer, int buffer_size)
{
    pthread_mutex_lock(&g_rules_mutex);
    char *json = rules_to_json_str(&g_rules);
    pthread_mutex_unlock(&g_rules_mutex);

    if (!json) return -1;

    int len = (int)strlen(json);
    if (len >= buffer_size) {
        free(json);
        return -1;
    }
    memcpy(buffer, json, (size_t)len + 1);
    free(json);
    return len;
}

const can_mqtt_rules_t *can_mqtt_engine_get_rules(void)
{
    return &g_rules;
}

/* ------------------------------------------------------------------ */
/*  CAN 帧处理（热路径）                                                   */
/* ------------------------------------------------------------------ */

void can_mqtt_engine_frame_callback(int channel, const can_frame_t *frame, void *user_data)
{
    (void)user_data;

    if (!frame || !g_initialized) return;
    if (!mqtt_client_is_connected()) return;

    bool is_ext  = (frame->can_id & 0x80000000u) != 0;
    uint32_t fid = frame->can_id & 0x1FFFFFFFu;
    const char *ch_name = (channel == 1) ? "can1" : "can0";

    const char *prefix    = mqtt_client_get_topic_prefix();
    const char *device_id = mqtt_client_get_device_id();

    pthread_mutex_lock(&g_rules_mutex);

    for (int i = 0; i < g_rules.rule_count; i++) {
        const can_mqtt_rule_t *rule = &g_rules.rules[i];

        if (!rule->enabled) continue;

        /* 通道匹配 */
        if (strcmp(rule->channel, "any") != 0
            && strcmp(rule->channel, ch_name) != 0) {
            continue;
        }

        /* CAN ID 匹配 */
        if (!rule->match_any_id) {
            if (rule->can_id != fid) continue;
            if (rule->is_extended != is_ext) continue;
        }

        /* 解码 */
        if (rule->decode.bit_length <= 0 || rule->decode.bit_length > 64) continue;

        uint64_t raw = extract_signal_raw(frame->data, frame->can_dlc, &rule->decode);
        double   val = raw_to_physical(raw, &rule->decode);

        /* ── 仅值变化或超时时才发布 ──────────────────────────────────
         * 比较原始整数值（避免浮点舍入导致误判）；
         * 若超过 CAN_MQTT_HEARTBEAT_INTERVAL 秒未发布则强制发布一次。 */
        struct timeval tv;
        gettimeofday(&tv, NULL);
        double ts = (double)tv.tv_sec + (double)tv.tv_usec / 1e6;

        can_mqtt_rule_t *wrule = (can_mqtt_rule_t *)rule; /* 需要写运行时字段 */
        bool value_changed = !wrule->has_last_raw || (wrule->last_raw != raw);
        bool heartbeat_due = (CAN_MQTT_HEARTBEAT_INTERVAL > 0)
                             && ((ts - wrule->last_publish_ts) >= CAN_MQTT_HEARTBEAT_INTERVAL);

        if (!value_changed && !heartbeat_due) continue;

        wrule->last_raw       = raw;
        wrule->has_last_raw   = true;
        wrule->last_publish_ts = ts;

        /* 构建 topic */
        char topic[512];
        build_topic(topic, sizeof(topic),
                    rule->mqtt.topic_template,
                    prefix, device_id,
                    rule->message_name, rule->signal_name,
                    ch_name);

        /* 构建 payload */
        char payload[512];
        if (rule->mqtt.payload_mode == CAN_MQTT_PAYLOAD_RAW) {
            snprintf(payload, sizeof(payload), "%.6g", val);
        } else {
            /* JSON payload */
            if (rule->decode.unit[0]) {
                snprintf(payload, sizeof(payload),
                         "{\"signal\":\"%s\",\"value\":%.6g,\"unit\":\"%s\","
                         "\"can_id\":%u,\"channel\":\"%s\",\"ts\":%.3f}",
                         rule->signal_name, val, rule->decode.unit,
                         fid, ch_name, ts);
            } else {
                snprintf(payload, sizeof(payload),
                         "{\"signal\":\"%s\",\"value\":%.6g,"
                         "\"can_id\":%u,\"channel\":\"%s\",\"ts\":%.3f}",
                         rule->signal_name, val,
                         fid, ch_name, ts);
            }
        }

        /* 发布（在持锁期间，但 MQTT 内部有自己的锁，不会死锁） */
        mqtt_client_publish(topic, payload, rule->mqtt.qos, rule->mqtt.retain);
    }

    pthread_mutex_unlock(&g_rules_mutex);
}
