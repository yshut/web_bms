#include "app_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

app_config_t g_app_config;

static char *trim(char *s) {
    if (!s) return s;
    while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end-- = '\0';
    }
    return s;
}

static int ieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static int parse_bool(const char *v, bool *out) {
    if (!v || !out) return -1;
    if (ieq(v, "1") || ieq(v, "true") || ieq(v, "yes") || ieq(v, "y") || ieq(v, "on")) {
        *out = true;
        return 0;
    }
    if (ieq(v, "0") || ieq(v, "false") || ieq(v, "no") || ieq(v, "n") || ieq(v, "off")) {
        *out = false;
        return 0;
    }
    return -1;
}

static int parse_u32(const char *v, uint32_t *out) {
    if (!v || !out) return -1;
    char *end = NULL;
    unsigned long x = strtoul(v, &end, 10);
    if (end == v) return -1;
    if (x > 0xFFFFFFFFul) x = 0xFFFFFFFFul;
    *out = (uint32_t)x;
    return 0;
}

static int parse_u16(const char *v, uint16_t *out) {
    if (!v || !out) return -1;
    char *end = NULL;
    unsigned long x = strtoul(v, &end, 10);
    if (end == v) return -1;
    if (x > 65535ul) x = 65535ul;
    *out = (uint16_t)x;
    return 0;
}

static void set_str(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
}

const char *app_config_transport_mode_to_string(app_transport_mode_t mode)
{
    return (mode == APP_TRANSPORT_MQTT) ? "mqtt" : "websocket";
}

void app_config_set_defaults(void) {
    memset(&g_app_config, 0, sizeof(g_app_config));

    /* remote transport */
    g_app_config.transport_mode = APP_TRANSPORT_MQTT;

    /* ws */
    set_str(g_app_config.ws_host, sizeof(g_app_config.ws_host), "192.168.100.1");
    g_app_config.ws_port = 5052;
    set_str(g_app_config.ws_path, sizeof(g_app_config.ws_path), "/ws");
    g_app_config.ws_use_ssl = false;
    g_app_config.ws_reconnect_interval_ms = 4000;
    g_app_config.ws_keepalive_interval_s = 20;

    /* mqtt */
    set_str(g_app_config.mqtt_host, sizeof(g_app_config.mqtt_host), "192.168.100.1");
    g_app_config.mqtt_port = 1883;
    g_app_config.mqtt_client_id[0] = '\0';
    g_app_config.mqtt_username[0] = '\0';
    g_app_config.mqtt_password[0] = '\0';
    g_app_config.mqtt_keepalive_s = 30;
    g_app_config.mqtt_qos = 1;
    set_str(g_app_config.mqtt_topic_prefix, sizeof(g_app_config.mqtt_topic_prefix), "app_lvgl");
    g_app_config.mqtt_use_tls = false;

    /* log */
    set_str(g_app_config.log_file, sizeof(g_app_config.log_file), "/tmp/lvgl_app.log");
    g_app_config.log_level = APP_LOG_DEBUG;

    /* can */
    g_app_config.can0_bitrate = 500000;
    g_app_config.can1_bitrate = 500000;
    set_str(g_app_config.can_record_dir, sizeof(g_app_config.can_record_dir), "/mnt/SDCARD/can_records");
    g_app_config.can_record_max_mb = 40;
    g_app_config.can_record_flush_ms = 200;

    /* storage/network */
    set_str(g_app_config.storage_mount, sizeof(g_app_config.storage_mount), "/mnt/SDCARD");
    set_str(g_app_config.net_iface, sizeof(g_app_config.net_iface), "eth0");
    set_str(g_app_config.wifi_iface, sizeof(g_app_config.wifi_iface), "wlan0");
    g_app_config.net_use_dhcp = false;
    set_str(g_app_config.net_ip, sizeof(g_app_config.net_ip), "192.168.100.100");
    set_str(g_app_config.net_netmask, sizeof(g_app_config.net_netmask), "255.255.255.0");
    set_str(g_app_config.net_gateway, sizeof(g_app_config.net_gateway), "192.168.100.1");
    g_app_config.net_use_dhcp = false;
    set_str(g_app_config.net_ip, sizeof(g_app_config.net_ip), "192.168.100.100");
    set_str(g_app_config.net_netmask, sizeof(g_app_config.net_netmask), "255.255.255.0");
    set_str(g_app_config.net_gateway, sizeof(g_app_config.net_gateway), "192.168.100.1");

    /* font */
    g_app_config.font_path[0] = '\0';   /* 默认走程序内置搜索列表 */
    g_app_config.font_size = 18;

    /* hardware monitor */
    g_app_config.hw_interval_ms = 2000;
    g_app_config.hw_auto_report = true;
    g_app_config.hw_report_interval_ms = 10000;

    /* wifi autoconnect (optional) */
    g_app_config.wifi_ssid[0] = '\0';
    g_app_config.wifi_psk[0] = '\0';
}

static void apply_kv(const char *k_in, const char *v_in) {
    if (!k_in || !v_in) return;
    char kbuf[128];
    set_str(kbuf, sizeof(kbuf), k_in);
    char *k = trim(kbuf);

    char vbuf[512];
    set_str(vbuf, sizeof(vbuf), v_in);
    char *v = trim(vbuf);

    /* remote transport */
    if (ieq(k, "transport_mode") || ieq(k, "REMOTE_TRANSPORT") || ieq(k, "TRANSPORT_MODE")) {
        if (ieq(v, "mqtt")) g_app_config.transport_mode = APP_TRANSPORT_MQTT;
        else g_app_config.transport_mode = APP_TRANSPORT_WEBSOCKET;
        return;
    }

    /* ws */
    if (ieq(k, "ws_host") || ieq(k, "server_host") || ieq(k, "WS_HOST")) {
        if (v[0]) set_str(g_app_config.ws_host, sizeof(g_app_config.ws_host), v);
        return;
    }
    if (ieq(k, "ws_port") || ieq(k, "server_port") || ieq(k, "WS_PORT")) {
        uint16_t p;
        if (parse_u16(v, &p) == 0 && p > 0) g_app_config.ws_port = p;
        return;
    }
    if (ieq(k, "ws_path") || ieq(k, "WS_PATH")) {
        if (v[0]) set_str(g_app_config.ws_path, sizeof(g_app_config.ws_path), v);
        return;
    }
    if (ieq(k, "ws_use_ssl") || ieq(k, "use_ssl") || ieq(k, "WS_USE_SSL")) {
        bool b;
        if (parse_bool(v, &b) == 0) g_app_config.ws_use_ssl = b;
        return;
    }
    if (ieq(k, "ws_reconnect_interval_ms") || ieq(k, "reconnect_interval_ms") || ieq(k, "WS_RECONNECT_MS")) {
        uint32_t x;
        if (parse_u32(v, &x) == 0 && x >= 100) g_app_config.ws_reconnect_interval_ms = x;
        return;
    }
    if (ieq(k, "ws_keepalive_interval_s") || ieq(k, "keepalive_interval_s") || ieq(k, "WS_KEEPALIVE_S")) {
        uint32_t x;
        if (parse_u32(v, &x) == 0 && x >= 1) g_app_config.ws_keepalive_interval_s = x;
        return;
    }

    /* mqtt */
    if (ieq(k, "mqtt_host") || ieq(k, "MQTT_HOST")) {
        if (v[0]) set_str(g_app_config.mqtt_host, sizeof(g_app_config.mqtt_host), v);
        return;
    }
    if (ieq(k, "mqtt_port") || ieq(k, "MQTT_PORT")) {
        uint16_t p;
        if (parse_u16(v, &p) == 0 && p > 0) g_app_config.mqtt_port = p;
        return;
    }
    if (ieq(k, "mqtt_client_id") || ieq(k, "MQTT_CLIENT_ID")) {
        set_str(g_app_config.mqtt_client_id, sizeof(g_app_config.mqtt_client_id), v);
        return;
    }
    if (ieq(k, "mqtt_username") || ieq(k, "MQTT_USERNAME")) {
        set_str(g_app_config.mqtt_username, sizeof(g_app_config.mqtt_username), v);
        return;
    }
    if (ieq(k, "mqtt_password") || ieq(k, "MQTT_PASSWORD")) {
        set_str(g_app_config.mqtt_password, sizeof(g_app_config.mqtt_password), v);
        return;
    }
    if (ieq(k, "mqtt_keepalive_s") || ieq(k, "MQTT_KEEPALIVE_S")) {
        uint32_t x;
        if (parse_u32(v, &x) == 0 && x >= 1) g_app_config.mqtt_keepalive_s = x;
        return;
    }
    if (ieq(k, "mqtt_qos") || ieq(k, "MQTT_QOS")) {
        uint32_t x;
        if (parse_u32(v, &x) == 0 && x <= 2) g_app_config.mqtt_qos = x;
        return;
    }
    if (ieq(k, "mqtt_topic_prefix") || ieq(k, "MQTT_TOPIC_PREFIX")) {
        if (v[0]) set_str(g_app_config.mqtt_topic_prefix, sizeof(g_app_config.mqtt_topic_prefix), v);
        return;
    }
    if (ieq(k, "mqtt_use_tls") || ieq(k, "MQTT_USE_TLS")) {
        bool b;
        if (parse_bool(v, &b) == 0) g_app_config.mqtt_use_tls = b;
        return;
    }

    /* log */
    if (ieq(k, "log_file") || ieq(k, "LOG_FILE")) {
        set_str(g_app_config.log_file, sizeof(g_app_config.log_file), v);
        return;
    }
    if (ieq(k, "log_level") || ieq(k, "LOG_LEVEL")) {
        if (ieq(v, "debug")) g_app_config.log_level = APP_LOG_DEBUG;
        else if (ieq(v, "info")) g_app_config.log_level = APP_LOG_INFO;
        else if (ieq(v, "warn") || ieq(v, "warning")) g_app_config.log_level = APP_LOG_WARN;
        else if (ieq(v, "error")) g_app_config.log_level = APP_LOG_ERROR;
        return;
    }

    /* can */
    if (ieq(k, "can0_bitrate") || ieq(k, "can1") || ieq(k, "can0")) {
        uint32_t x;
        if (parse_u32(v, &x) == 0 && x > 0) g_app_config.can0_bitrate = x;
        return;
    }
    if (ieq(k, "can1_bitrate") || ieq(k, "can2") || ieq(k, "can1")) {
        uint32_t x;
        if (parse_u32(v, &x) == 0 && x > 0) g_app_config.can1_bitrate = x;
        return;
    }
    if (ieq(k, "can_record_dir")) {
        if (v[0]) set_str(g_app_config.can_record_dir, sizeof(g_app_config.can_record_dir), v);
        return;
    }
    if (ieq(k, "can_record_max_mb")) {
        uint32_t x;
        if (parse_u32(v, &x) == 0 && x > 0) g_app_config.can_record_max_mb = x;
        return;
    }
    if (ieq(k, "can_record_flush_ms")) {
        uint32_t x;
        if (parse_u32(v, &x) == 0 && x > 0) g_app_config.can_record_flush_ms = x;
        return;
    }

    /* storage/network */
    if (ieq(k, "storage_mount")) {
        if (v[0]) set_str(g_app_config.storage_mount, sizeof(g_app_config.storage_mount), v);
        return;
    }
    if (ieq(k, "dhcp") || ieq(k, "net_use_dhcp") || ieq(k, "use_dhcp")) {
        bool b;
        if (parse_bool(v, &b) == 0) g_app_config.net_use_dhcp = b;
        return;
    }
    if (ieq(k, "ip") || ieq(k, "net_ip") || ieq(k, "device_ip")) {
        if (v[0]) set_str(g_app_config.net_ip, sizeof(g_app_config.net_ip), v);
        return;
    }
    if (ieq(k, "netmask") || ieq(k, "mask") || ieq(k, "device_netmask")) {
        if (v[0]) set_str(g_app_config.net_netmask, sizeof(g_app_config.net_netmask), v);
        return;
    }
    if (ieq(k, "gateway") || ieq(k, "gw") || ieq(k, "device_gateway")) {
        set_str(g_app_config.net_gateway, sizeof(g_app_config.net_gateway), v);
        return;
    }
    if (ieq(k, "iface")) {
        if (v[0]) set_str(g_app_config.net_iface, sizeof(g_app_config.net_iface), v);
        return;
    }
    if (ieq(k, "net_iface")) {
        if (v[0]) set_str(g_app_config.net_iface, sizeof(g_app_config.net_iface), v);
        return;
    }
    if (ieq(k, "wifi_iface")) {
        if (v[0]) set_str(g_app_config.wifi_iface, sizeof(g_app_config.wifi_iface), v);
        return;
    }

    /* font */
    if (ieq(k, "font_path")) {
        if (v[0]) set_str(g_app_config.font_path, sizeof(g_app_config.font_path), v);
        return;
    }
    if (ieq(k, "font_size")) {
        char *end = NULL;
        long x = strtol(v, &end, 10);
        if (end != v && x >= 10 && x <= 96) g_app_config.font_size = (int)x;
        return;
    }

    /* hardware monitor */
    if (ieq(k, "hw_interval_ms")) {
        uint32_t x;
        if (parse_u32(v, &x) == 0 && x >= 100) g_app_config.hw_interval_ms = x;
        return;
    }
    if (ieq(k, "hw_report_interval_ms")) {
        uint32_t x;
        if (parse_u32(v, &x) == 0 && x >= 200) g_app_config.hw_report_interval_ms = x;
        return;
    }
    if (ieq(k, "hw_auto_report")) {
        bool b;
        if (parse_bool(v, &b) == 0) g_app_config.hw_auto_report = b;
        return;
    }

    /* wifi autoconnect fields (optional) */
    if (ieq(k, "wifi_ssid") || ieq(k, "WIFI_SSID")) {
        set_str(g_app_config.wifi_ssid, sizeof(g_app_config.wifi_ssid), v);
        return;
    }
    if (ieq(k, "wifi_psk") || ieq(k, "WIFI_PSK")) {
        set_str(g_app_config.wifi_psk, sizeof(g_app_config.wifi_psk), v);
        return;
    }
}

static int load_config_file_internal(const char *path, bool reset_defaults, bool allow_legacy_format)
{
    if (reset_defaults) {
        app_config_set_defaults();
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    /* 保护：若文件是二进制（例如误把 lvgl_app 拷贝成 ws_config.txt），则视为无效配置 */
    {
        unsigned char head[64];
        size_t n = fread(head, 1, sizeof(head), fp);
        /* rewind for normal parsing */
        fseek(fp, 0, SEEK_SET);
        if (n >= 4 && head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F') {
            fclose(fp);
            return -1;
        }
        for (size_t i = 0; i < n; i++) {
            if (head[i] == 0x00) {
                fclose(fp);
                return -1;
            }
        }
    }

    char line[768];
    int legacy_idx = 0;
    int seen_effective = 0; /* 是否出现过非空/非注释的有效行 */
    int seen_kv = 0;        /* 是否出现过 key=value */
    while (fgets(line, sizeof(line), fp)) {
        char *s = trim(line);
        if (*s == '\0') continue;
        if (*s == '#') continue;
        if (*s == ';') continue;

        /* strip inline comment: "a=b # xxx" */
        char *hash = strchr(s, '#');
        if (hash) *hash = '\0';
        s = trim(s);
        if (*s == '\0') continue;
        seen_effective = 1;

        char *eq = strchr(s, '=');
        if (eq) {
            seen_kv = 1;
            *eq = '\0';
            char *k = trim(s);
            char *v = trim(eq + 1);
            apply_kv(k, v);
            continue;
        }

        /* legacy positional lines */
        if (!allow_legacy_format) {
            continue;
        }
        legacy_idx++;
        if (legacy_idx == 1) {
            apply_kv("ws_host", s);
        } else if (legacy_idx == 2) {
            apply_kv("ws_port", s);
        } else if (legacy_idx == 3) {
            apply_kv("wifi_ssid", s);
        } else if (legacy_idx == 4) {
            apply_kv("wifi_psk", s);
        } else if (legacy_idx == 5) {
            apply_kv("wifi_iface", s);
        }
    }

    fclose(fp);
    /* 空文件（或只有注释/空行）视为“未配置”，让上层触发自动生成模板 */
    if (!seen_effective) return -1;

    /* 自动升级：若用户仍使用旧格式（位置行）且没有任何 key=value，则写回全量模板，方便后续只改一个文件 */
    if (allow_legacy_format && !seen_kv && legacy_idx > 0) {
        /* best-effort：写失败也不影响本次已加载的配置 */
        (void)app_config_save_file(path);
    }
    return 0;
}

int app_config_load_file(const char *path) {
    return load_config_file_internal(path, true, true);
}

int app_config_load_network_file(const char *path) {
    return load_config_file_internal(path, false, false);
}

int app_config_load_best(char *used_path, size_t used_path_size) {
    const char *paths[] = {
        "/mnt/UDISK/ws_config.txt",
        "/mnt/SDCARD/ws_config.txt",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        if (app_config_load_file(paths[i]) == 0) {
            if (used_path && used_path_size > 0) {
                set_str(used_path, used_path_size, paths[i]);
            }
            return 0;
        }
    }

    app_config_set_defaults();
    if (used_path && used_path_size > 0) used_path[0] = '\0';
    return -1;
}

int app_config_load_network_best(char *used_path, size_t used_path_size) {
    const char *paths[] = {
        "/mnt/UDISK/net_config.txt",
        "/mnt/SDCARD/net_config.txt",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        if (app_config_load_network_file(paths[i]) == 0) {
            if (used_path && used_path_size > 0) {
                set_str(used_path, used_path_size, paths[i]);
            }
            return 0;
        }
    }

    if (used_path && used_path_size > 0) used_path[0] = '\0';
    return -1;
}

static int write_atomic(const char *path, const char *content) {
    if (!path || !content) return -1;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *fp = fopen(tmp, "w");
    if (!fp) return -1;
    fputs(content, fp);
    fflush(fp);
    fclose(fp);
    /* best-effort atomic replace */
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

int app_config_save_file(const char *path) {
    if (!path) return -1;

    /* 统一输出为 key=value 格式，便于完整保存所有配置项 */
    char buf[8192];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - (size_t)n,
                  "# app_lvgl 配置文件（推荐 key=value 格式）\n"
                  "# 说明：仍兼容旧格式（前两行 host/port），但一旦保存将输出为 key=value 全量配置。\n"
                  "# 注意：WSL/虚拟网卡的 172.* 地址通常对开发板不可达，请填写开发板可达的服务器IP。\n"
                  "# 若服务跑在 WSL 且开发板需经 Windows 访问，请把 mqtt_host/ws_host 填成 Windows 主机局域网 IP，而不是 WSL 的 127.0.0.1 或 172.*。\n"
                  "# MQTT 提示：本地联调通常使用 1883；后续部署到服务器时请同步修改 mqtt_host/mqtt_port/topic_prefix。\n"
                  "# MQTT TLS 提示：当前板端客户端仍按明文 TCP 连接，mqtt_use_tls=true 仅保留配置位，暂未启用加密连接。\n"
                  "\n"
                  "# === 传输模式 ===\n"
                  "transport_mode=%s\n"
                  "\n"
                  "# === WebSocket ===\n"
                  "ws_host=%s\n"
                  "ws_port=%u\n"
                  "ws_path=%s\n"
                  "ws_use_ssl=%s\n"
                  "ws_reconnect_interval_ms=%u\n"
                  "ws_keepalive_interval_s=%u\n"
                  "\n"
                  "# === MQTT ===\n"
                  "mqtt_host=%s\n"
                  "mqtt_port=%u\n"
                  "mqtt_client_id=%s\n"
                  "mqtt_username=%s\n"
                  "mqtt_password=%s\n"
                  "mqtt_keepalive_s=%u\n"
                  "mqtt_qos=%u\n"
                  "mqtt_topic_prefix=%s\n"
                  "mqtt_use_tls=%s\n"
                  "\n"
                  "# === 日志 ===\n"
                  "log_file=%s\n"
                  "log_level=%s\n"
                  "\n"
                  "# === CAN ===\n"
                  "can0_bitrate=%u\n"
                  "can1_bitrate=%u\n"
                  "can_record_dir=%s\n"
                  "can_record_max_mb=%u\n"
                  "can_record_flush_ms=%u\n"
                  "\n"
                  "# === 存储 ===\n"
                  "storage_mount=%s\n"
                  "# 板端IP/网卡配置请写入 /mnt/UDISK/net_config.txt\n"
                  "\n"
                  "# === 字体 ===\n"
                  "# font_path 为空则自动从常见路径列表中查找\n"
                  "font_path=%s\n"
                  "font_size=%d\n"
                  "\n"
                  "# === 硬件监控 ===\n"
                  "hw_interval_ms=%u\n"
                  "hw_auto_report=%s\n"
                  "hw_report_interval_ms=%u\n"
                  "\n"
                  "# === WiFi 自动连接（可选，供脚本/系统服务使用）===\n"
                  "wifi_ssid=%s\n"
                  "wifi_psk=%s\n",
                  app_config_transport_mode_to_string(g_app_config.transport_mode),
                  g_app_config.ws_host,
                  (unsigned)g_app_config.ws_port,
                  g_app_config.ws_path,
                  g_app_config.ws_use_ssl ? "true" : "false",
                  (unsigned)g_app_config.ws_reconnect_interval_ms,
                  (unsigned)g_app_config.ws_keepalive_interval_s,
                  g_app_config.mqtt_host,
                  (unsigned)g_app_config.mqtt_port,
                  g_app_config.mqtt_client_id,
                  g_app_config.mqtt_username,
                  g_app_config.mqtt_password,
                  (unsigned)g_app_config.mqtt_keepalive_s,
                  (unsigned)g_app_config.mqtt_qos,
                  g_app_config.mqtt_topic_prefix,
                  g_app_config.mqtt_use_tls ? "true" : "false",
                  g_app_config.log_file[0] ? g_app_config.log_file : "",
                  (g_app_config.log_level == APP_LOG_DEBUG) ? "debug" :
                  (g_app_config.log_level == APP_LOG_INFO) ? "info" :
                  (g_app_config.log_level == APP_LOG_WARN) ? "warn" : "error",
                  (unsigned)g_app_config.can0_bitrate,
                  (unsigned)g_app_config.can1_bitrate,
                  g_app_config.can_record_dir,
                  (unsigned)g_app_config.can_record_max_mb,
                  (unsigned)g_app_config.can_record_flush_ms,
                  g_app_config.storage_mount,
                  g_app_config.font_path[0] ? g_app_config.font_path : "",
                  g_app_config.font_size,
                  (unsigned)g_app_config.hw_interval_ms,
                  g_app_config.hw_auto_report ? "true" : "false",
                  (unsigned)g_app_config.hw_report_interval_ms,
                  g_app_config.wifi_ssid,
                  g_app_config.wifi_psk);

    if (n <= 0 || (size_t)n >= sizeof(buf)) {
        return -1;
    }

    return write_atomic(path, buf);
}

int app_config_save_network_file(const char *path) {
    char buf[1024];
    int n;

    if (!path) return -1;

    n = snprintf(buf, sizeof(buf),
                 "# app_lvgl 板端网络配置\n"
                 "# 说明：此文件只描述开发板自身网卡参数，不包含服务端地址。\n"
                 "# 服务端 WebSocket/MQTT 地址请写入 ws_config.txt。\n"
                 "\n"
                 "dhcp=%s\n"
                 "ip=%s\n"
                 "netmask=%s\n"
                 "gateway=%s\n"
                 "iface=%s\n"
                 "wifi_iface=%s\n",
                 g_app_config.net_use_dhcp ? "true" : "false",
                 g_app_config.net_ip,
                 g_app_config.net_netmask,
                 g_app_config.net_gateway,
                 g_app_config.net_iface,
                 g_app_config.wifi_iface);
    if (n <= 0 || (size_t)n >= sizeof(buf)) {
        return -1;
    }

    return write_atomic(path, buf);
}

int app_config_save_best(char *used_path, size_t used_path_size) {
    const char *paths[] = {
        "/mnt/UDISK/ws_config.txt",
        "/mnt/SDCARD/ws_config.txt",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        if (app_config_save_file(paths[i]) == 0) {
            if (used_path && used_path_size > 0) {
                set_str(used_path, used_path_size, paths[i]);
            }
            return 0;
        }
    }

    if (used_path && used_path_size > 0) used_path[0] = '\0';
    return -1;
}

int app_config_save_network_best(char *used_path, size_t used_path_size) {
    const char *paths[] = {
        "/mnt/UDISK/net_config.txt",
        "/mnt/SDCARD/net_config.txt",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        if (app_config_save_network_file(paths[i]) == 0) {
            if (used_path && used_path_size > 0) {
                set_str(used_path, used_path_size, paths[i]);
            }
            return 0;
        }
    }

    if (used_path && used_path_size > 0) used_path[0] = '\0';
    return -1;
}


