#ifndef UI_HOME_H
#define UI_HOME_H

#include "lvgl.h"
#include <stdbool.h>

// 主页UI结构
typedef struct {
    lv_obj_t *container;
    lv_obj_t *title_label;
    lv_obj_t *server_status_label;
    lv_obj_t *wifi_status_label;
    lv_obj_t *can_status_label;
    
    // 功能按钮
    lv_obj_t *btn_can_monitor;
    lv_obj_t *btn_uds_diagnostic;
    lv_obj_t *btn_file_manager;
    lv_obj_t *btn_wifi_settings;
} ui_home_t;

// 创建主页
lv_obj_t* ui_home_create(lv_obj_t *parent);

// 更新主页状态
void ui_home_update_server_status(bool tcp_connected, bool ws_connected);
void ui_home_update_wifi_status(const char *ssid, bool connected);
void ui_home_update_can_status(bool can1_active, bool can2_active);

#endif // UI_HOME_H

