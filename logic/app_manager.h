/**
 * @file app_manager.h
 * @brief 应用管理器 - 负责页面切换和全局状态管理
 */

#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 应用页面枚举
 */
typedef enum {
    APP_PAGE_HOME = 0,          // 主页
    APP_PAGE_CAN_MONITOR,       // CAN监控
    APP_PAGE_UDS,               // UDS诊断
    APP_PAGE_FILE_MANAGER,      // 文件管理
    APP_PAGE_WIFI,              // WiFi设置
    APP_PAGE_WEBSOCKET,         // WebSocket远程控制
    APP_PAGE_MAX,
} app_page_t;

/**
 * @brief 网络连接状态
 */
typedef struct {
    bool server_connected;      // 服务器连接状态
    char server_host[64];       // 服务器地址
    uint16_t server_port;       // 服务器端口
    
    bool wifi_connected;        // WiFi连接状态
    char wifi_ssid[64];         // WiFi SSID
    char wifi_ip[32];           // WiFi IP地址
} network_status_t;

/**
 * @brief CAN状态
 */
typedef struct {
    bool can0_active;           // CAN0活动状态
    bool can1_active;           // CAN1活动状态
    uint32_t can0_frame_count;  // CAN0帧计数
    uint32_t can1_frame_count;  // CAN1帧计数
    int can0_bitrate;           // CAN0比特率
    int can1_bitrate;           // CAN1比特率
} can_status_t;

/**
 * @brief 初始化应用管理器
 * @return int 成功返回0，失败返回负数
 */
int app_manager_init(void);

/**
 * @brief 反初始化应用管理器
 */
void app_manager_deinit(void);

/**
 * @brief 切换到指定页面
 * @param page 目标页面
 * @return int 成功返回0，失败返回负数
 */
int app_manager_switch_to_page(app_page_t page);

/**
 * @brief 返回主页
 */
void app_manager_go_home(void);

/**
 * @brief 获取当前页面
 * @return app_page_t 当前页面
 */
app_page_t app_manager_get_current_page(void);

/**
 * @brief 获取网络状态
 * @param status 状态结构体指针
 */
void app_manager_get_network_status(network_status_t *status);

/**
 * @brief 获取CAN状态
 * @param status 状态结构体指针
 */
void app_manager_get_can_status(can_status_t *status);

/**
 * @brief 更新服务器连接状态
 * @param connected 是否已连接
 * @param host 服务器地址
 * @param port 服务器端口
 */
void app_manager_update_server_status(bool connected, const char *host, uint16_t port);

/**
 * @brief 更新WiFi连接状态
 * @param connected 是否已连接
 * @param ssid WiFi SSID
 * @param ip IP地址
 */
void app_manager_update_wifi_status(bool connected, const char *ssid, const char *ip);

#endif /* APP_MANAGER_H */

