#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

// WiFi管理器回调
typedef void (*wifi_scan_callback_t)(const char **ssids, int *signal_strengths, int count);
typedef void (*wifi_status_callback_t)(const char *ssid, bool connected);

// 初始化WiFi管理器
int wifi_manager_init(void);

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

// 设置回调
void wifi_manager_set_scan_callback(wifi_scan_callback_t callback);
void wifi_manager_set_status_callback(wifi_status_callback_t callback);

#endif // WIFI_MANAGER_H

