/**
 * @file ui_remote_control.c
 * @brief UI远程控制实现 - 线程安全的UI按钮控制
 */

#include "ui_remote_control.h"
#include "app_manager.h"
#include "can_handler.h"
#include "uds_handler.h"
#include "file_transfer.h"
#include "../ui/ui_can_monitor.h"
#include "../ui/ui_uds.h"
#include "../utils/logger.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ========== 数据结构 ========== */

typedef struct {
    char id[32];
    char data[128];
    int channel;
    bool extended;
} can_send_data_t;

typedef struct {
    char ssid[128];
    char password[128];
} wifi_connect_data_t;

typedef struct {
    char old_path[256];
    char new_name[128];
} file_rename_data_t;

typedef struct {
    int channel;
    uint32_t bitrate;
} bitrate_data_t;

typedef struct {
    char iface[16];
    uint32_t bitrate;
    uint32_t tx_id;
    uint32_t rx_id;
    uint32_t block_size;
} uds_param_data_t;

/* ========== 初始化和清理 ========== */

int ui_remote_init(void)
{
    log_info("UI远程控制模块初始化");
    return 0;
}

void ui_remote_deinit(void)
{
    log_info("UI远程控制模块清理");
}

/* ========== CAN监控页面控制 ========== */

static void can_start_async_cb(void *user_data)
{
    (void)user_data;
    log_info("[远程控制] 启动CAN监控");
    
    // 启动CAN
    can_handler_start();
    
    // 如果在CAN监控页面，UI会自动更新
    log_info("[远程控制] CAN监控已启动");
}

void ui_remote_can_click_start(void)
{
    lv_async_call(can_start_async_cb, NULL);
}

static void can_stop_async_cb(void *user_data)
{
    (void)user_data;
    log_info("[远程控制] 停止CAN监控");
    
    // 停止CAN
    can_handler_stop();
    
    log_info("[远程控制] CAN监控已停止");
}

void ui_remote_can_click_stop(void)
{
    lv_async_call(can_stop_async_cb, NULL);
}

static void can_clear_async_cb(void *user_data)
{
    (void)user_data;
    log_info("[远程控制] 清除CAN消息");
    
    // 清除CAN消息显示
    ui_can_monitor_clear_messages_async();
    
    log_info("[远程控制] CAN消息已清除");
}

void ui_remote_can_click_clear(void)
{
    lv_async_call(can_clear_async_cb, NULL);
}

static void can_send_frame_async_cb(void *user_data)
{
    can_send_data_t *data = (can_send_data_t *)user_data;
    if (!data) return;
    
    log_info("[远程控制] 发送CAN帧: ID=%s, Data=%s, Channel=%d, Extended=%d",
             data->id, data->data, data->channel, data->extended);
    
    // 解析ID
    uint32_t id = 0;
    sscanf(data->id, "%x", &id);
    
    // 解析数据
    can_frame_t frame = {0};
    frame.can_id = id;
    frame.is_extended = data->extended;
    frame.channel = data->channel;
    
    // 解析十六进制数据字符串
    const char *hex = data->data;
    int len = strlen(hex);
    if (len % 2 != 0) {
        log_error("[远程控制] 数据长度必须是偶数");
        free(data);
        return;
    }
    
    frame.can_dlc = len / 2;
    if (frame.can_dlc > 8) frame.can_dlc = 8;
    
    for (int i = 0; i < frame.can_dlc; i++) {
        char byte_str[3] = {hex[i*2], hex[i*2+1], '\0'};
        sscanf(byte_str, "%hhx", &frame.data[i]);
    }
    
    // 发送CAN帧
    can_handler_send(&frame);
    
    log_info("[远程控制] CAN帧已发送");
    free(data);
}

void ui_remote_can_send_frame(const char *id, const char *data, int channel, bool extended)
{
    can_send_data_t *send_data = (can_send_data_t *)malloc(sizeof(can_send_data_t));
    if (!send_data) return;
    
    snprintf(send_data->id, sizeof(send_data->id), "%s", id);
    snprintf(send_data->data, sizeof(send_data->data), "%s", data);
    send_data->channel = channel;
    send_data->extended = extended;
    
    lv_async_call(can_send_frame_async_cb, send_data);
}

static void can_record_async_cb(void *user_data)
{
    (void)user_data;
    log_info("[远程控制] 切换CAN录制状态");
    
    // TODO: 实现录制按钮点击逻辑
    // 需要访问ui_can_monitor的录制状态并切换
    
    log_info("[远程控制] CAN录制状态已切换");
}

void ui_remote_can_click_record(void)
{
    lv_async_call(can_record_async_cb, NULL);
}

static void can_set_bitrate_async_cb(void *user_data)
{
    bitrate_data_t *data = (bitrate_data_t *)user_data;
    if (!data) return;
    
    log_info("[远程控制] 设置CAN%d波特率: %u", data->channel, data->bitrate);
    
    // 设置波特率
    if (data->channel == 0) {
        can_handler_configure("can0", data->bitrate);
    } else if (data->channel == 1) {
        can_handler_configure("can1", data->bitrate);
    }
    
    log_info("[远程控制] 波特率已设置");
    free(data);
}

void ui_remote_can_set_bitrate(int channel, uint32_t bitrate)
{
    bitrate_data_t *data = (bitrate_data_t *)malloc(sizeof(bitrate_data_t));
    if (!data) return;
    
    data->channel = channel;
    data->bitrate = bitrate;
    
    lv_async_call(can_set_bitrate_async_cb, data);
}

/* ========== UDS诊断页面控制 ========== */

static void uds_select_file_async_cb(void *user_data)
{
    char *path = (char *)user_data;
    if (!path) return;
    
    log_info("[远程控制] 选择UDS文件: %s", path);
    
    // 设置UDS文件路径并同步到页面状态
    ui_uds_remote_set_file(path);
    
    log_info("[远程控制] UDS文件已选择");
    free(path);
}

void ui_remote_uds_select_file(const char *file_path)
{
    if (!file_path) return;
    
    char *path = strdup(file_path);
    if (!path) return;
    
    lv_async_call(uds_select_file_async_cb, path);
}

static void uds_start_async_cb(void *user_data)
{
    (void)user_data;
    log_info("[远程控制] 开始UDS刷写");
    ui_uds_remote_start();
    
    log_info("[远程控制] UDS刷写已开始");
}

void ui_remote_uds_click_start(void)
{
    lv_async_call(uds_start_async_cb, NULL);
}

static void uds_stop_async_cb(void *user_data)
{
    (void)user_data;
    log_info("[远程控制] 停止UDS刷写");
    ui_uds_remote_stop();
    
    log_info("[远程控制] UDS刷写已停止");
}

void ui_remote_uds_click_stop(void)
{
    lv_async_call(uds_stop_async_cb, NULL);
}

static void uds_set_bitrate_async_cb(void *user_data)
{
    uint32_t *bitrate = (uint32_t *)user_data;
    if (!bitrate) return;
    
    log_info("[远程控制] 设置UDS波特率: %u", *bitrate);
    ui_uds_remote_set_params(NULL, *bitrate, 0, 0, 0);
    
    log_info("[远程控制] UDS波特率已设置");
    free(bitrate);
}

void ui_remote_uds_set_bitrate(uint32_t bitrate)
{
    uint32_t *data = (uint32_t *)malloc(sizeof(uint32_t));
    if (!data) return;
    
    *data = bitrate;
    lv_async_call(uds_set_bitrate_async_cb, data);
}

static void uds_set_params_async_cb(void *user_data)
{
    uds_param_data_t *data = (uds_param_data_t *)user_data;
    if (!data) return;

    log_info("[远程控制] 同步UDS参数: iface=%s bitrate=%u tx=0x%X rx=0x%X blk=%u",
             data->iface[0] ? data->iface : "(keep)", data->bitrate, data->tx_id, data->rx_id, data->block_size);
    ui_uds_remote_set_params(data->iface, data->bitrate, data->tx_id, data->rx_id, data->block_size);
    free(data);
}

void ui_remote_uds_set_params(const char *iface, uint32_t bitrate, uint32_t tx_id, uint32_t rx_id, uint32_t block_size)
{
    uds_param_data_t *data = (uds_param_data_t *)malloc(sizeof(uds_param_data_t));
    if (!data) return;
    memset(data, 0, sizeof(*data));
    if (iface && iface[0]) {
        strncpy(data->iface, iface, sizeof(data->iface) - 1);
    }
    data->bitrate = bitrate;
    data->tx_id = tx_id;
    data->rx_id = rx_id;
    data->block_size = block_size;
    lv_async_call(uds_set_params_async_cb, data);
}

static void uds_clear_log_async_cb(void *user_data)
{
    (void)user_data;
    log_info("[远程控制] 清除UDS日志");
    ui_uds_remote_clear_logs();
    log_info("[远程控制] UDS日志已清除");
}

void ui_remote_uds_clear_log(void)
{
    lv_async_call(uds_clear_log_async_cb, NULL);
}

int ui_remote_uds_get_state_json(char *buf, size_t size)
{
    return ui_uds_get_state_json(buf, size);
}

/* ========== WiFi页面控制 ========== */

static void wifi_scan_async_cb(void *user_data)
{
    (void)user_data;
    log_info("[远程控制] 扫描WiFi");
    
    // TODO: 触发WiFi扫描
    // 需要调用WiFi扫描功能
    
    log_info("[远程控制] WiFi扫描已开始");
}

void ui_remote_wifi_click_scan(void)
{
    lv_async_call(wifi_scan_async_cb, NULL);
}

static void wifi_connect_async_cb(void *user_data)
{
    wifi_connect_data_t *data = (wifi_connect_data_t *)user_data;
    if (!data) return;
    
    log_info("[远程控制] 连接WiFi: %s", data->ssid);
    
    // TODO: 实现WiFi连接逻辑
    // 需要调用wpa_supplicant命令
    
    log_info("[远程控制] WiFi连接请求已发送");
    free(data);
}

void ui_remote_wifi_connect(const char *ssid, const char *password)
{
    if (!ssid) return;
    
    wifi_connect_data_t *data = (wifi_connect_data_t *)malloc(sizeof(wifi_connect_data_t));
    if (!data) return;
    
    snprintf(data->ssid, sizeof(data->ssid), "%s", ssid);
    snprintf(data->password, sizeof(data->password), "%s", password ? password : "");
    
    lv_async_call(wifi_connect_async_cb, data);
}

static void wifi_disconnect_async_cb(void *user_data)
{
    (void)user_data;
    log_info("[远程控制] 断开WiFi");
    
    // TODO: 实现WiFi断开逻辑
    system("killall wpa_supplicant");
    
    log_info("[远程控制] WiFi已断开");
}

void ui_remote_wifi_disconnect(void)
{
    lv_async_call(wifi_disconnect_async_cb, NULL);
}

static void wifi_forget_async_cb(void *user_data)
{
    char *ssid = (char *)user_data;
    if (!ssid) return;
    
    log_info("[远程控制] 删除WiFi: %s", ssid);
    
    // TODO: 实现删除WiFi配置的逻辑
    
    log_info("[远程控制] WiFi配置已删除");
    free(ssid);
}

void ui_remote_wifi_forget(const char *ssid)
{
    if (!ssid) return;
    
    char *ssid_copy = strdup(ssid);
    if (!ssid_copy) return;
    
    lv_async_call(wifi_forget_async_cb, ssid_copy);
}

/* ========== 文件管理页面控制 ========== */

static void file_refresh_async_cb(void *user_data)
{
    (void)user_data;
    log_info("[远程控制] 刷新文件列表");
    
    // TODO: 触发文件列表刷新
    
    log_info("[远程控制] 文件列表已刷新");
}

void ui_remote_file_refresh(void)
{
    lv_async_call(file_refresh_async_cb, NULL);
}

static void file_enter_dir_async_cb(void *user_data)
{
    char *path = (char *)user_data;
    if (!path) return;
    
    log_info("[远程控制] 进入目录: %s", path);
    
    // TODO: 切换到指定目录
    
    log_info("[远程控制] 已进入目录");
    free(path);
}

void ui_remote_file_enter_dir(const char *path)
{
    if (!path) return;
    
    char *path_copy = strdup(path);
    if (!path_copy) return;
    
    lv_async_call(file_enter_dir_async_cb, path_copy);
}

static void file_go_back_async_cb(void *user_data)
{
    (void)user_data;
    log_info("[远程控制] 返回上级目录");
    
    // TODO: 返回上级目录
    
    log_info("[远程控制] 已返回上级目录");
}

void ui_remote_file_go_back(void)
{
    lv_async_call(file_go_back_async_cb, NULL);
}

static void file_delete_async_cb(void *user_data)
{
    char *path = (char *)user_data;
    if (!path) return;
    
    log_info("[远程控制] 删除文件/目录: %s", path);
    
    // 使用file_transfer的删除功能
    file_delete_recursive(path);
    
    log_info("[远程控制] 文件/目录已删除");
    free(path);
}

void ui_remote_file_delete(const char *path)
{
    if (!path) return;
    
    char *path_copy = strdup(path);
    if (!path_copy) return;
    
    lv_async_call(file_delete_async_cb, path_copy);
}

static void file_rename_async_cb(void *user_data)
{
    file_rename_data_t *data = (file_rename_data_t *)user_data;
    if (!data) return;
    
    log_info("[远程控制] 重命名: %s -> %s", data->old_path, data->new_name);
    
    // 构造新路径
    char new_path[512];
    char *last_slash = strrchr(data->old_path, '/');
    if (last_slash) {
        size_t dir_len = last_slash - data->old_path + 1;
        snprintf(new_path, sizeof(new_path), "%.*s%s", 
                 (int)dir_len, data->old_path, data->new_name);
    } else {
        snprintf(new_path, sizeof(new_path), "%s", data->new_name);
    }
    
    // 使用file_transfer的重命名功能
    file_rename(data->old_path, new_path);
    
    log_info("[远程控制] 文件/目录已重命名");
    free(data);
}

void ui_remote_file_rename(const char *old_path, const char *new_name)
{
    if (!old_path || !new_name) return;
    
    file_rename_data_t *data = (file_rename_data_t *)malloc(sizeof(file_rename_data_t));
    if (!data) return;
    
    snprintf(data->old_path, sizeof(data->old_path), "%s", old_path);
    snprintf(data->new_name, sizeof(data->new_name), "%s", new_name);
    
    lv_async_call(file_rename_async_cb, data);
}

/* ========== 主页控制 ========== */

static void navigate_async_cb(void *user_data)
{
    char *page_name = (char *)user_data;
    if (!page_name) return;
    
    log_info("[远程控制] 导航到页面: %s", page_name);
    
    app_page_t page = APP_PAGE_HOME;
    
    if (strcmp(page_name, "home") == 0) {
        page = APP_PAGE_HOME;
    } else if (strcmp(page_name, "can") == 0) {
        page = APP_PAGE_CAN_MONITOR;
    } else if (strcmp(page_name, "uds") == 0) {
        page = APP_PAGE_UDS;
    } else if (strcmp(page_name, "wifi") == 0) {
        page = APP_PAGE_WIFI;
    } else if (strcmp(page_name, "file") == 0) {
        page = APP_PAGE_FILE_MANAGER;
    } else if (strcmp(page_name, "websocket") == 0) {
        page = APP_PAGE_WEBSOCKET;
    } else {
        log_error("[远程控制] 未知页面: %s", page_name);
        free(page_name);
        return;
    }
    
    app_manager_switch_to_page(page);
    
    log_info("[远程控制] 已导航到页面");
    free(page_name);
}

void ui_remote_navigate(const char *page_name)
{
    if (!page_name) return;
    
    char *page_copy = strdup(page_name);
    if (!page_copy) return;
    
    lv_async_call(navigate_async_cb, page_copy);
}

