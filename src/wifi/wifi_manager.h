#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

// WiFi管理器回调
typedef void (*wifi_scan_callback_t)(const char **ssids, int *signal_strengths, int count);
typedef void (*wifi_status_callback_t)(const char *ssid, bool connected);

typedef struct {
    bool associated;
    bool has_ip;
    bool gateway_reachable;
    bool cloud_reachable;
    bool auto_reconnect_enabled;
    char iface[16];
    char current_ssid[64];
    char current_ip[64];
    char gateway[64];
} wifi_runtime_status_t;

// 初始化WiFi管理器
int wifi_manager_init(void);

// 启动/停止后台自动重连
int wifi_manager_start(void);
void wifi_manager_stop(void);

// 扫描WiFi网络
int wifi_manager_scan(void);

// 连接WiFi
int wifi_manager_connect(const char *ssid, const char *password);

// 断开WiFi
int wifi_manager_disconnect(void);

// 获取当前连接状态
bool wifi_manager_is_connected(void);

// 获取当前SSID
const char* wifi_manager_get_current_ssid(void);

// 获取运行时状态
int wifi_manager_get_status(wifi_runtime_status_t *status);

// 控制后台自动重连开关（运行时）
void wifi_manager_set_auto_reconnect_paused(bool paused);

// 设置回调
void wifi_manager_set_scan_callback(wifi_scan_callback_t callback);
void wifi_manager_set_status_callback(wifi_status_callback_t callback);

#endif // WIFI_MANAGER_H
