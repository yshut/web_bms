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

void app_config_set_defaults(void) {
    memset(&g_app_config, 0, sizeof(g_app_config));

    /* ws */
    set_str(g_app_config.ws_host, sizeof(g_app_config.ws_host), "192.168.100.1");
    g_app_config.ws_port = 5052;
    set_str(g_app_config.ws_path, sizeof(g_app_config.ws_path), "/ws");
    g_app_config.ws_use_ssl = false;
    g_app_config.ws_reconnect_interval_ms = 4000;
    g_app_config.ws_keepalive_interval_s = 20;

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

int app_config_load_file(const char *path) {
    app_config_set_defaults();

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
    if (!seen_kv && legacy_idx > 0) {
        /* best-effort：写失败也不影响本次已加载的配置 */
        (void)app_config_save_file(path);
    }
    return 0;
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
    char buf[4096];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - (size_t)n,
                  "# app_lvgl 配置文件（推荐 key=value 格式）\n"
                  "# 说明：仍兼容旧格式（前两行 host/port），但一旦保存将输出为 key=value 全量配置。\n"
                  "# 注意：WSL/虚拟网卡的 172.* 地址通常对开发板不可达，请填写开发板可达的服务器IP。\n"
                  "\n"
                  "# === WebSocket ===\n"
                  "ws_host=%s\n"
                  "ws_port=%u\n"
                  "ws_path=%s\n"
                  "ws_use_ssl=%s\n"
                  "ws_reconnect_interval_ms=%u\n"
                  "ws_keepalive_interval_s=%u\n"
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
                  "# === 存储/网络 ===\n"
                  "storage_mount=%s\n"
                  "net_iface=%s\n"
                  "wifi_iface=%s\n"
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
                  g_app_config.ws_host,
                  (unsigned)g_app_config.ws_port,
                  g_app_config.ws_path,
                  g_app_config.ws_use_ssl ? "true" : "false",
                  (unsigned)g_app_config.ws_reconnect_interval_ms,
                  (unsigned)g_app_config.ws_keepalive_interval_s,
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
                  g_app_config.net_iface,
                  g_app_config.wifi_iface,
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


