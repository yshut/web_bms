#ifndef APP_LVGL_APP_CONFIG_H
#define APP_LVGL_APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief 应用配置（统一从 /mnt/UDISK/ws_config.txt 或 /mnt/SDCARD/ws_config.txt 读取）
 *
 * 兼容两种格式：
 * 1) 旧格式（位置行）：
 *    第1行: WS_HOST
 *    第2行: WS_PORT
 *    第3行: WIFI_SSID (可选)
 *    第4行: WIFI_PSK  (可选)
 *    第5行: WIFI_IFACE(可选)
 *
 * 2) 新格式（推荐，key=value，支持 # 注释/空行）：
 *    ws_host=192.168.1.100
 *    ws_port=5052
 *    ws_path=/ws
 *    log_file=/tmp/lvgl_app.log
 *    log_level=debug|info|warn|error
 *    can0_bitrate=500000
 *    can1_bitrate=500000
 *    can_record_dir=/mnt/SDCARD/can_records
 *    can_record_max_mb=40
 *    can_record_flush_ms=200
 *    storage_mount=/mnt/SDCARD
 *    net_iface=eth0
 *    wifi_iface=wlan0
 *    font_path=/mnt/SDCARD/fonts/simsun.ttc
 *    font_size=18
 *    hw_interval_ms=2000
 *    hw_auto_report=true
 *    hw_report_interval_ms=10000
 */

typedef enum {
    APP_LOG_DEBUG = 0,
    APP_LOG_INFO,
    APP_LOG_WARN,
    APP_LOG_ERROR,
} app_log_level_t;

typedef struct {
    /* ws */
    char ws_host[128];
    uint16_t ws_port;
    char ws_path[64];
    bool ws_use_ssl;
    uint32_t ws_reconnect_interval_ms;
    uint32_t ws_keepalive_interval_s;

    /* log */
    char log_file[256];         /* 空字符串表示只输出到控制台 */
    app_log_level_t log_level;

    /* can */
    uint32_t can0_bitrate;
    uint32_t can1_bitrate;
    char can_record_dir[256];
    uint32_t can_record_max_mb;
    uint32_t can_record_flush_ms;

    /* storage/network */
    char storage_mount[128];    /* 如 /mnt/SDCARD */
    char net_iface[16];         /* 如 eth0 */
    char wifi_iface[16];        /* 如 wlan0 */

    /* font */
    char font_path[256];        /* 可选：指定单个字体文件 */
    int font_size;              /* px */

    /* hardware monitor */
    uint32_t hw_interval_ms;
    bool hw_auto_report;
    uint32_t hw_report_interval_ms;

    /* wifi autoconnect (可选，仅用于与脚本同源配置) */
    char wifi_ssid[64];
    char wifi_psk[128];
} app_config_t;

extern app_config_t g_app_config;

void app_config_set_defaults(void);

/**
 * @brief 加载配置：优先 /mnt/UDISK/ws_config.txt，其次 /mnt/SDCARD/ws_config.txt
 * @return 0=加载成功；-1=未找到配置文件（仍保留默认值）
 */
int app_config_load_best(char *used_path, size_t used_path_size);

/**
 * @brief 从指定文件加载配置（会先恢复默认值）
 */
int app_config_load_file(const char *path);

/**
 * @brief 保存配置到指定文件（写入 key=value 全量配置，会覆盖原文件）
 * @return 0=成功；-1=失败
 */
int app_config_save_file(const char *path);

/**
 * @brief 保存配置：优先 /mnt/UDISK/ws_config.txt，其次 /mnt/SDCARD/ws_config.txt
 * @return 0=保存成功；-1=保存失败
 */
int app_config_save_best(char *used_path, size_t used_path_size);

#endif /* APP_LVGL_APP_CONFIG_H */


