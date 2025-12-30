/**
 * @file ui_wifi.h
 * @brief WiFi设置界面头文件
 */

#ifndef UI_WIFI_H
#define UI_WIFI_H

#include "lvgl.h"
#include <stdbool.h>

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *status_label;        // 状态标签
    lv_obj_t *network_list;        // 网络列表
    
    // 密码输入对话框
    lv_obj_t *password_dialog;     // 密码对话框
    lv_obj_t *password_textarea;   // 密码输入框
} ui_wifi_t;

ui_wifi_t* ui_wifi_create(void);
void ui_wifi_destroy(ui_wifi_t *wifi);

// WiFi 状态查询函数
bool ui_wifi_is_connected(void);
const char* ui_wifi_get_connected_ssid(void);
const char* ui_wifi_get_ip_address(void);

#endif /* UI_WIFI_H */
