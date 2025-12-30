#ifndef UI_WIFI_H
#define UI_WIFI_H

#include "lvgl.h"
#include <stdbool.h>

// WiFi设置UI结构
typedef struct {
    lv_obj_t *container;
    lv_obj_t *btn_back;
    lv_obj_t *btn_scan;
    lv_obj_t *btn_connect;
    lv_obj_t *btn_disconnect;
    lv_obj_t *ssid_input;
    lv_obj_t *password_input;
    lv_obj_t *wifi_list;
    lv_obj_t *status_label;
    lv_obj_t *current_ssid_label;
} ui_wifi_t;

// 创建WiFi设置页面
lv_obj_t* ui_wifi_create(lv_obj_t *parent);

// 更新WiFi列表
void ui_wifi_update_list(const char **ssids, int *signal_strengths, int count);

// 更新连接状态
void ui_wifi_update_connection_status(const char *ssid, bool connected);

// 更新状态
void ui_wifi_update_status(const char *status);

#endif // UI_WIFI_H

