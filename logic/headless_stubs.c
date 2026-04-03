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

static void _send_ok(const char *id, const char *data_json)
{
    char buf[1024];
    if (data_json && data_json[0])
        snprintf(buf, sizeof(buf),
            "{\"id\":\"%s\",\"ok\":true,\"data\":%s}", id, data_json);
    else
        snprintf(buf, sizeof(buf),
            "{\"id\":\"%s\",\"ok\":true}", id);
    mqtt_client_send_reply_json(buf);
}

static void _send_err(const char *id, const char *msg)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"ok\":false,\"error\":\"%s\"}", id, msg ? msg : "error");
    mqtt_client_send_reply_json(buf);
}

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

    /* 未知命令 — 静默忽略（不发回复，防止旧逻辑超时堆积） */
    return 0;
}
void ws_command_send_ok(const char *rid, const char *extra)
{ (void)rid; (void)extra; }
void ws_command_send_error(const char *rid, const char *msg)
{ (void)rid; (void)msg; }
