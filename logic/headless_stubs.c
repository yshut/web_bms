/**
 * @file headless_stubs.c
 * @brief 无屏幕模式下对所有 LVGL/UI 函数的空实现
 *
 * 去掉屏幕显示后，logic/ 层依赖的 UI 函数全部在这里提供无害的桩实现。
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/*  lv_async_call 替代实现 — 直接同步调用，无需 LVGL 主线程           */
/* ------------------------------------------------------------------ */
typedef void (*lv_async_cb_t)(void *);

void lv_async_call(lv_async_cb_t cb, void *user_data)
{
    if (cb) cb(user_data);
}

/* ------------------------------------------------------------------ */
/*  ui_home 相关（main.c 调用）                                         */
/* ------------------------------------------------------------------ */
void ui_home_update_server_status_async(bool connected, const char *host, uint16_t port)
{
    (void)connected; (void)host; (void)port;
}

/* ------------------------------------------------------------------ */
/*  ui_can_monitor 相关                                                  */
/* ------------------------------------------------------------------ */
void ui_can_monitor_clear_messages_async(void) {}

/* ------------------------------------------------------------------ */
/*  ui_uds 相关（ws_command_handler 调用）                               */
/* ------------------------------------------------------------------ */
int  ui_uds_remote_set_file(const char *path) { (void)path; return -1; }
int  ui_uds_remote_start(void)                { return -1; }
void ui_uds_remote_stop(void)                 {}
void ui_uds_remote_clear_logs(void)           {}
int  ui_uds_get_state_json(char *buf, size_t size)
{
    if (buf && size > 2) {
        strncpy(buf, "{}", size);
        return 2;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  ui_wifi 相关                                                         */
/* ------------------------------------------------------------------ */
void ui_remote_wifi_click_scan(void)                               {}
void ui_remote_wifi_connect(const char *ssid, const char *psk)     { (void)ssid; (void)psk; }
void ui_remote_wifi_disconnect(void)                               {}
void ui_remote_wifi_forget(const char *ssid)                       { (void)ssid; }

/* ------------------------------------------------------------------ */
/*  ui_file_manager 相关                                                 */
/* ------------------------------------------------------------------ */
void ui_remote_file_refresh(void)                                  {}
void ui_remote_file_enter_dir(const char *path)                    { (void)path; }
void ui_remote_file_go_back(void)                                  {}
void ui_remote_file_delete(const char *path)                       { (void)path; }
void ui_remote_file_rename(const char *old_path, const char *name) { (void)old_path; (void)name; }

/* ------------------------------------------------------------------ */
/*  app_manager 最小实现（去掉 LVGL 依赖）                               */
/* ------------------------------------------------------------------ */
int  app_manager_init(void)   { return 0; }
void app_manager_deinit(void) {}

/* ------------------------------------------------------------------ */
/*  ui_remote_control 桩（logic 层不再编译原文件）                        */
/* ------------------------------------------------------------------ */
#include "can_handler.h"
#include "can_recorder.h"
#include "file_transfer.h"
#include "uds_handler.h"

int  ui_remote_init(void)    { return 0; }
void ui_remote_deinit(void)  {}

void ui_remote_can_click_start(void)  { can_handler_start(); }
void ui_remote_can_click_stop(void)   { can_handler_stop(); }
void ui_remote_can_click_clear(void)  {}
void ui_remote_can_click_record(void) {
    if (can_recorder_is_recording()) can_recorder_stop();
    else can_recorder_start();
}
void ui_remote_can_send_frame(const char *id, const char *data,
                              int channel, bool extended)
{
    (void)id; (void)data; (void)channel; (void)extended;
}
void ui_remote_can_set_bitrate(int channel, uint32_t bitrate)
{
    (void)channel; (void)bitrate;
}
void ui_remote_uds_select_file(const char *p) { (void)p; }
void ui_remote_uds_click_start(void)          {}
void ui_remote_uds_click_stop(void)           {}
void ui_remote_uds_set_bitrate(uint32_t b)    { (void)b; }
void ui_remote_uds_set_params(const char *iface, uint32_t bitrate,
                              uint32_t tx_id, uint32_t rx_id,
                              uint32_t block_size)
{ (void)iface; (void)bitrate; (void)tx_id; (void)rx_id; (void)block_size; }
void ui_remote_uds_clear_log(void) {}
int  ui_remote_uds_get_state_json(char *buf, size_t size)
{
    if (buf && size > 2) { strncpy(buf, "{}", size); return 2; }
    return -1;
}
void ui_remote_navigate(const char *page_name) { (void)page_name; }

/* ------------------------------------------------------------------ */
/*  ws_command_handler 桩                                               */
/* ------------------------------------------------------------------ */
int  ws_command_handler_init(void)                  { return 0; }
void ws_command_handler_deinit(void)                {}
/* ------------------------------------------------------------------ */
/*  ws_command_handler_process — 无 LVGL 模式下真正处理 MQTT 命令      */
/* ------------------------------------------------------------------ */
#include "mqtt_client.h"
#include "utils/app_config.h"

/* 简单 JSON 字段提取（不依赖 json-c）*/
static const char *_find_str(const char *json, const char *key, char *out, size_t olen)
{
    if (!json || !key || !out || olen < 1) return NULL;
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) { out[0]='\0'; return NULL; }
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') { out[0]='\0'; return NULL; }
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i < olen-1) out[i++] = *p++;
        out[i] = '\0';
        return out;
    }
    /* number / bool */
    size_t i = 0;
    while (*p && *p != ',' && *p != '}' && *p != ' ' && i < olen-1)
        out[i++] = *p++;
    out[i] = '\0';
    return out;
}

static char *extract_json_string(const char *json, const char *key)
{
    char tmp[512];
    if (!_find_str(json, key, tmp, sizeof(tmp))) {
        return NULL;
    }
    return strdup(tmp);
}

static int extract_json_int(const char *json, const char *key, int default_value)
{
    char *str = extract_json_string(json, key);
    int value;
    if (!str) return default_value;
    value = atoi(str);
    free(str);
    return value;
}

static bool extract_json_bool(const char *json, const char *key, bool default_value)
{
    char *str = extract_json_string(json, key);
    bool value;
    if (!str) return default_value;
    value = (strcmp(str, "true") == 0 || strcmp(str, "1") == 0);
    free(str);
    return value;
}

static void _send_ok(const char *id, const char *data_json)
{
    const char *rid = (id && id[0]) ? id : "";
    const char *data = (data_json && data_json[0]) ? data_json : NULL;
    size_t need = data
        ? (strlen(rid) + strlen(data) + 32)
        : (strlen(rid) + 24);
    char *buf = (char *)malloc(need);
    if (!buf) {
        return;
    }
    if (data) {
        snprintf(buf, need, "{\"id\":\"%s\",\"ok\":true,\"data\":%s}", rid, data);
    } else {
        snprintf(buf, need, "{\"id\":\"%s\",\"ok\":true}", rid);
    }
    mqtt_client_send_reply_json(buf);
    free(buf);
}

static void _send_err(const char *id, const char *msg)
{
    const char *rid = (id && id[0]) ? id : "";
    const char *err = msg ? msg : "error";
    size_t need = strlen(rid) + strlen(err) + 40;
    char *buf = (char *)malloc(need);
    if (!buf) {
        return;
    }
    snprintf(buf, need, "{\"id\":\"%s\",\"ok\":false,\"error\":\"%s\"}", rid, err);
    mqtt_client_send_reply_json(buf);
    free(buf);
}

static char g_headless_uds_iface[16] = "can0";
static uint32_t g_headless_uds_bitrate = 500000;
static uint32_t g_headless_uds_tx_id = 0x7F3;
static uint32_t g_headless_uds_rx_id = 0x7FB;
static uint32_t g_headless_uds_blk_size = 256;
static char g_headless_uds_path[512] = {0};

int ws_command_handler_process(const char *json)
{
    if (!json) return -1;
    char cmd[64]={}, rid[64]={};
    _find_str(json, "cmd", cmd, sizeof(cmd));
    _find_str(json, "id",  rid, sizeof(rid));

    /* ping */
    if (strcmp(cmd, "ping") == 0) {
        _send_ok(rid, "{\"pong\":true}");
        return 0;
    }

    /* can_get_status — 返回运行/录制状态和波特率 */
    if (strcmp(cmd, "can_get_status") == 0) {
        bool run = can_handler_is_running();
        bool rec = can_recorder_is_recording();
        uint32_t br0 = g_app_config.can0_bitrate;
        uint32_t br1 = g_app_config.can1_bitrate;
        can_handler_get_bitrate_dual(&br0, &br1);
        char d[256];
        snprintf(d, sizeof(d),
            "{\"is_running\":%s,\"running\":%s,"
            "\"is_recording\":%s,\"recording\":%s,"
            "\"can0_bitrate\":%u,\"can1_bitrate\":%u}",
            run?"true":"false", run?"true":"false",
            rec?"true":"false", rec?"true":"false",
            (unsigned)br0, (unsigned)br1);
        _send_ok(rid, d);
        return 0;
    }

    /* can_start */
    if (strcmp(cmd, "can_start") == 0) {
        if (can_handler_start() == 0) _send_ok(rid, NULL);
        else _send_err(rid, "CAN start failed");
        return 0;
    }

    /* can_stop */
    if (strcmp(cmd, "can_stop") == 0) {
        can_handler_stop();
        _send_ok(rid, NULL);
        return 0;
    }

    /* can_get_config */
    if (strcmp(cmd, "can_get_config") == 0) {
        uint32_t br0 = g_app_config.can0_bitrate;
        uint32_t br1 = g_app_config.can1_bitrate;
        can_handler_get_bitrate_dual(&br0, &br1);
        char d[128];
        snprintf(d, sizeof(d),
            "{\"can0_bitrate\":%u,\"can1_bitrate\":%u,"
            "\"can1\":%u,\"can2\":%u}",
            (unsigned)br0, (unsigned)br1,
            (unsigned)br0, (unsigned)br1);
        _send_ok(rid, d);
        return 0;
    }

    /* can_record_start */
    if (strcmp(cmd, "can_record_start") == 0) {
        if (can_recorder_is_recording()) {
            _send_ok(rid, "{\"recording\":true,\"note\":\"already_recording\"}");
        } else if (can_recorder_start() == 0) {
            _send_ok(rid, "{\"recording\":true}");
        } else {
            _send_err(rid, "Failed to start recording");
        }
        return 0;
    }

    /* can_record_stop */
    if (strcmp(cmd, "can_record_stop") == 0) {
        if (!can_recorder_is_recording()) {
            _send_ok(rid, "{\"recording\":false,\"note\":\"not_recording\"}");
        } else {
            const char *fn = can_recorder_get_filename();
            char d[512];
            snprintf(d, sizeof(d), "{\"recording\":false,\"filename\":\"%s\"}", fn?fn:"");
            can_recorder_stop();
            _send_ok(rid, d);
        }
        return 0;
    }

    /* fs_base */
    if (strcmp(cmd, "fs_base") == 0) {
        const char *base = g_app_config.storage_mount[0] ? g_app_config.storage_mount : "/mnt/SDCARD";
        char d[256];
        snprintf(d, sizeof(d), "{\"base\":\"%s\"}", base);
        _send_ok(rid, d);
        return 0;
    }

    /* fs_list */
    if (strcmp(cmd, "fs_list") == 0) {
        char *path = extract_json_string(json, "path");
        if (!path) path = strdup(g_app_config.storage_mount[0] ? g_app_config.storage_mount : "/mnt/SDCARD");
        if (!path) {
            _send_err(rid, "Missing path");
            return 0;
        }
        char *list_json = file_list_directory_json(path);
        if (list_json) {
            _send_ok(rid, list_json);
            free(list_json);
        } else {
            _send_err(rid, "Failed to list directory");
        }
        free(path);
        return 0;
    }

    /* fs_mkdir */
    if (strcmp(cmd, "fs_mkdir") == 0) {
        char *path = extract_json_string(json, "path");
        if (!path) {
            _send_err(rid, "Missing path");
            return 0;
        }
        if (file_mkdir_recursive(path) == 0) {
            char d[640];
            snprintf(d, sizeof(d), "{\"path\":\"%s\"}", path);
            _send_ok(rid, d);
        } else {
            _send_err(rid, "Failed to create directory");
        }
        free(path);
        return 0;
    }

    /* fs_delete */
    if (strcmp(cmd, "fs_delete") == 0) {
        char *path = extract_json_string(json, "path");
        if (!path) {
            _send_err(rid, "Missing path");
            return 0;
        }
        if (file_delete_recursive(path) == 0) {
            char d[640];
            snprintf(d, sizeof(d), "{\"path\":\"%s\"}", path);
            _send_ok(rid, d);
        } else {
            _send_err(rid, "Failed to delete");
        }
        free(path);
        return 0;
    }

    /* fs_rename */
    if (strcmp(cmd, "fs_rename") == 0) {
        char *path = extract_json_string(json, "path");
        char *new_name = extract_json_string(json, "new_name");
        if (!path || !new_name) {
            if (path) free(path);
            if (new_name) free(new_name);
            _send_err(rid, "Missing path or new_name");
            return 0;
        }
        if (file_rename(path, new_name) == 0) {
            _send_ok(rid, NULL);
        } else {
            _send_err(rid, "Failed to rename");
        }
        free(path);
        free(new_name);
        return 0;
    }

    /* fs_upload */
    if (strcmp(cmd, "fs_upload") == 0) {
        char *path = extract_json_string(json, "path");
        char *b64 = extract_json_string(json, "data");
        if (!path || !b64) {
            if (path) free(path);
            if (b64) free(b64);
            _send_err(rid, "Missing path or data");
            return 0;
        }
        size_t bin_len = 0;
        uint8_t *bin = base64_decode(b64, &bin_len);
        if (!bin && bin_len > 0) {
            free(path);
            free(b64);
            _send_err(rid, "Failed to decode data");
            return 0;
        }
        if (file_write(path, bin, bin_len) == 0) {
            char d[640];
            snprintf(d, sizeof(d), "{\"path\":\"%s\",\"size\":%zu}", path, bin_len);
            _send_ok(rid, d);
        } else {
            _send_err(rid, "Failed to write file");
        }
        if (bin) free(bin);
        free(path);
        free(b64);
        return 0;
    }

    /* fs_read */
    if (strcmp(cmd, "fs_read") == 0) {
        char *path = extract_json_string(json, "path");
        if (!path) {
            _send_err(rid, "Missing path");
            return 0;
        }
        size_t size = 0, b64_len = 0;
        uint8_t *data = file_read(path, &size);
        if (!data || size >= 1048576) {
            if (data) free(data);
            free(path);
            _send_err(rid, "File too large or read failed");
            return 0;
        }
        char *b64 = base64_encode(data, size, &b64_len);
        if (!b64) {
            free(data);
            free(path);
            _send_err(rid, "Failed to encode data");
            return 0;
        }
        size_t need = strlen(path) + b64_len + 64;
        char *resp = (char *)malloc(need);
        if (resp) {
            snprintf(resp, need, "{\"name\":\"%s\",\"size\":%zu,\"data\":\"%s\"}", path, size, b64);
            _send_ok(rid, resp);
            free(resp);
        } else {
            _send_err(rid, "Out of memory");
        }
        free(b64);
        free(data);
        free(path);
        return 0;
    }

    /* fs_read_range */
    if (strcmp(cmd, "fs_read_range") == 0) {
        char *path = extract_json_string(json, "path");
        int offset = extract_json_int(json, "offset", 0);
        int length = extract_json_int(json, "length", 65536);
        if (length <= 0) length = 65536;
        if (length > 262144) length = 262144;
        if (!path) {
            _send_err(rid, "Missing path");
            return 0;
        }
        size_t read_len = 0, b64_len = 0;
        bool eof = false;
        uint8_t *data = file_read_range(path, (size_t)(offset < 0 ? 0 : offset), (size_t)length, &read_len, &eof);
        if (!data && read_len == 0) {
            free(path);
            _send_err(rid, "Failed to read file");
            return 0;
        }
        char *b64 = base64_encode(data, read_len, &b64_len);
        if (!b64) {
            if (data) free(data);
            free(path);
            _send_err(rid, "Failed to encode data");
            return 0;
        }
        size_t need = strlen(path) + b64_len + 96;
        char *resp = (char *)malloc(need);
        if (resp) {
            snprintf(resp, need, "{\"name\":\"%s\",\"read\":%zu,\"eof\":%s,\"data\":\"%s\"}",
                     path, read_len, eof ? "true" : "false", b64);
            _send_ok(rid, resp);
            free(resp);
        } else {
            _send_err(rid, "Out of memory");
        }
        if (data) free(data);
        free(b64);
        free(path);
        return 0;
    }

    /* fs_write_range */
    if (strcmp(cmd, "fs_write_range") == 0) {
        char *path = extract_json_string(json, "path");
        char *b64 = extract_json_string(json, "data");
        int offset = extract_json_int(json, "offset", 0);
        bool truncate = extract_json_bool(json, "truncate", false);
        if (!path || !b64) {
            if (path) free(path);
            if (b64) free(b64);
            _send_err(rid, "Missing path or data");
            return 0;
        }
        size_t bin_len = 0;
        uint8_t *bin = base64_decode(b64, &bin_len);
        if (!bin && bin_len > 0) {
            free(path);
            free(b64);
            _send_err(rid, "Failed to decode data");
            return 0;
        }
        if (file_write_range(path, (size_t)(offset < 0 ? 0 : offset), bin, bin_len, truncate) == 0) {
            char d[768];
            snprintf(d, sizeof(d), "{\"path\":\"%s\",\"offset\":%d,\"written\":%zu,\"next\":%zu}",
                     path, offset, bin_len, (size_t)(offset < 0 ? 0 : offset) + bin_len);
            _send_ok(rid, d);
        } else {
            _send_err(rid, "Failed to write range");
        }
        if (bin) free(bin);
        free(path);
        free(b64);
        return 0;
    }

    /* fs_stat */
    if (strcmp(cmd, "fs_stat") == 0) {
        char *path = extract_json_string(json, "path");
        file_info_t info;
        if (!path) {
            _send_err(rid, "Missing path");
            return 0;
        }
        if (file_get_info(path, &info) == 0) {
            char d[768];
            snprintf(d, sizeof(d), "{\"name\":\"%s\",\"size\":%zu,\"mtime\":%ld,\"is_dir\":%s}",
                     info.name, info.size, (long)info.mtime, info.is_dir ? "true" : "false");
            _send_ok(rid, d);
        } else {
            _send_err(rid, "File not found");
        }
        free(path);
        return 0;
    }

    /* uds_set_params */
    if (strcmp(cmd, "uds_set_params") == 0) {
        char *iface = extract_json_string(json, "iface");
        uint32_t tx_id = (uint32_t)extract_json_int(json, "tx_id", (int)g_headless_uds_tx_id);
        uint32_t rx_id = (uint32_t)extract_json_int(json, "rx_id", (int)g_headless_uds_rx_id);
        uint32_t blk = (uint32_t)extract_json_int(json, "block_size", (int)g_headless_uds_blk_size);
        const char *iface_out = (iface && iface[0]) ? iface : "can0";
        if (iface_out) {
            strncpy(g_headless_uds_iface, iface_out, sizeof(g_headless_uds_iface) - 1);
            g_headless_uds_iface[sizeof(g_headless_uds_iface) - 1] = '\0';
        }
        g_headless_uds_tx_id = tx_id;
        g_headless_uds_rx_id = rx_id;
        g_headless_uds_blk_size = blk;
        g_headless_uds_bitrate = (strcmp(g_headless_uds_iface, "can1") == 0)
            ? g_app_config.can1_bitrate
            : g_app_config.can0_bitrate;
        uds_set_params(g_headless_uds_iface, tx_id, rx_id, blk);
        char d[256];
        snprintf(d, sizeof(d), "{\"iface\":\"%s\",\"tx_id\":%u,\"rx_id\":%u,\"block_size\":%u}",
                 g_headless_uds_iface, tx_id, rx_id, blk);
        _send_ok(rid, d);
        if (iface) free(iface);
        return 0;
    }

    /* uds_set_file / uds_click_select_file */
    if (strcmp(cmd, "uds_set_file") == 0 || strcmp(cmd, "uds_click_select_file") == 0) {
        char *path = extract_json_string(json, "path");
        if (!path) {
            _send_err(rid, "Missing file path");
            return 0;
        }
        if (uds_set_file_path(path) == 0) {
            strncpy(g_headless_uds_path, path, sizeof(g_headless_uds_path) - 1);
            g_headless_uds_path[sizeof(g_headless_uds_path) - 1] = '\0';
            _send_ok(rid, NULL);
        } else {
            _send_err(rid, "Invalid file path");
        }
        free(path);
        return 0;
    }

    /* uds_state */
    if (strcmp(cmd, "uds_state") == 0 || strcmp(cmd, "uds_progress") == 0 || strcmp(cmd, "uds_logs") == 0) {
        char d[1024];
        bool running = uds_is_running();
        snprintf(d, sizeof(d),
                 "{\"iface\":\"%s\",\"bitrate\":%u,\"tx_id\":\"%X\",\"rx_id\":\"%X\","
                 "\"block_size\":%u,\"path\":\"%s\",\"percent\":%d,\"running\":%s,\"logs\":[]}",
                 g_headless_uds_iface[0] ? g_headless_uds_iface : "can0",
                 g_headless_uds_bitrate ? g_headless_uds_bitrate : 500000,
                 g_headless_uds_tx_id ? g_headless_uds_tx_id : 0x7F3,
                 g_headless_uds_rx_id ? g_headless_uds_rx_id : 0x7FB,
                 g_headless_uds_blk_size ? g_headless_uds_blk_size : 256,
                 g_headless_uds_path,
                 running ? 1 : 0,
                 running ? "true" : "false");
        _send_ok(rid, d);
        return 0;
    }

    /* uds_list */
    if (strcmp(cmd, "uds_list") == 0) {
        char *dir = extract_json_string(json, "dir");
        if (!dir) dir = strdup(g_app_config.storage_mount[0] ? g_app_config.storage_mount : "/mnt/SDCARD");
        if (!dir) {
            _send_err(rid, "Missing dir");
            return 0;
        }
        char *files = list_s19_files_json(dir);
        if (files) {
            _send_ok(rid, files);
            free(files);
        } else {
            _send_err(rid, "Failed to list files");
        }
        free(dir);
        return 0;
    }

    /* uds_start / uds_click_start */
    if (strcmp(cmd, "uds_start") == 0 || strcmp(cmd, "uds_click_start") == 0) {
        if (uds_start_flash() == 0) {
            _send_ok(rid, NULL);
        } else {
            _send_err(rid, "Failed to start flashing");
        }
        return 0;
    }

    /* uds_stop / uds_click_stop */
    if (strcmp(cmd, "uds_stop") == 0 || strcmp(cmd, "uds_click_stop") == 0) {
        uds_stop_flash();
        _send_ok(rid, NULL);
        return 0;
    }

    /* uds_set_bitrate */
    if (strcmp(cmd, "uds_set_bitrate") == 0) {
        g_headless_uds_bitrate = (uint32_t)extract_json_int(json, "bitrate", (int)g_headless_uds_bitrate);
        _send_ok(rid, NULL);
        return 0;
    }

    /* uds_clear_log */
    if (strcmp(cmd, "uds_clear_log") == 0) {
        _send_ok(rid, NULL);
        return 0;
    }

    /* 未知命令 — 返回错误，避免云端长时间等超时 */
    _send_err(rid, "unknown command");
    return 0;
}
void ws_command_send_ok(const char *rid, const char *extra)
{ (void)rid; (void)extra; }
void ws_command_send_error(const char *rid, const char *msg)
{ (void)rid; (void)msg; }
