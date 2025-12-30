#include "ui_home.h"
#include "ui_main.h"
#include "../common/app_config.h"
#include <stdio.h>

static ui_home_t g_ui_home;

// 按钮事件处理
static void btn_can_monitor_event(lv_event_t *e) {
    ui_main_show_page(PAGE_CAN_MONITOR);
}

static void btn_uds_diagnostic_event(lv_event_t *e) {
    ui_main_show_page(PAGE_UDS_DIAGNOSTIC);
}

static void btn_file_manager_event(lv_event_t *e) {
    ui_main_show_page(PAGE_FILE_MANAGER);
}

static void btn_wifi_settings_event(lv_event_t *e) {
    ui_main_show_page(PAGE_WIFI_SETTINGS);
}

lv_obj_t* ui_home_create(lv_obj_t *parent) {
    // 创建主容器
    g_ui_home.container = lv_obj_create(parent);
    lv_obj_set_size(g_ui_home.container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_ui_home.container, lv_color_hex(COLOR_BG_MAIN), 0);
    lv_obj_set_style_border_width(g_ui_home.container, 0, 0);
    lv_obj_set_style_radius(g_ui_home.container, 0, 0);
    lv_obj_set_style_pad_all(g_ui_home.container, 20, 0);

    // 创建标题
    g_ui_home.title_label = lv_label_create(g_ui_home.container);
    lv_label_set_text(g_ui_home.title_label, APP_NAME);
    lv_obj_set_style_text_font(g_ui_home.title_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_ui_home.title_label, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(g_ui_home.title_label, LV_ALIGN_TOP_MID, 0, 20);

    // 创建状态信息区域
    int status_y = 80;
    int status_spacing = 40;

    // 服务器状态
    g_ui_home.server_status_label = lv_label_create(g_ui_home.container);
    lv_label_set_text(g_ui_home.server_status_label, "服务器: 未连接");
    lv_obj_set_style_text_color(g_ui_home.server_status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_pos(g_ui_home.server_status_label, 40, status_y);

    // WiFi 状态
    g_ui_home.wifi_status_label = lv_label_create(g_ui_home.container);
    lv_label_set_text(g_ui_home.wifi_status_label, "WiFi: 未连接");
    lv_obj_set_style_text_color(g_ui_home.wifi_status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_pos(g_ui_home.wifi_status_label, 40, status_y + status_spacing);

    // CAN 状态
    g_ui_home.can_status_label = lv_label_create(g_ui_home.container);
    lv_label_set_text(g_ui_home.can_status_label, "CAN: 未激活");
    lv_obj_set_style_text_color(g_ui_home.can_status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_pos(g_ui_home.can_status_label, 40, status_y + status_spacing * 2);

    // 创建功能按钮网格
    int btn_start_y = 260;
    int btn_width = 200;
    int btn_height = 60;
    int btn_spacing_x = 40;
    int btn_spacing_y = 20;
    int btn_col1_x = (SCREEN_WIDTH - btn_width * 2 - btn_spacing_x) / 2;
    int btn_col2_x = btn_col1_x + btn_width + btn_spacing_x;

    // CAN 监控按钮
    g_ui_home.btn_can_monitor = lv_btn_create(g_ui_home.container);
    lv_obj_set_size(g_ui_home.btn_can_monitor, btn_width, btn_height);
    lv_obj_set_pos(g_ui_home.btn_can_monitor, btn_col1_x, btn_start_y);
    lv_obj_set_style_radius(g_ui_home.btn_can_monitor, 12, 0);
    lv_obj_set_style_bg_color(g_ui_home.btn_can_monitor, lv_color_hex(COLOR_PRIMARY), 0);
    lv_obj_add_event_cb(g_ui_home.btn_can_monitor, btn_can_monitor_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label1 = lv_label_create(g_ui_home.btn_can_monitor);
    lv_label_set_text(label1, "CAN 监控");
    lv_obj_set_style_text_font(label1, &lv_font_montserrat_18, 0);
    lv_obj_center(label1);

    // UDS 诊断按钮
    g_ui_home.btn_uds_diagnostic = lv_btn_create(g_ui_home.container);
    lv_obj_set_size(g_ui_home.btn_uds_diagnostic, btn_width, btn_height);
    lv_obj_set_pos(g_ui_home.btn_uds_diagnostic, btn_col2_x, btn_start_y);
    lv_obj_set_style_radius(g_ui_home.btn_uds_diagnostic, 12, 0);
    lv_obj_set_style_bg_color(g_ui_home.btn_uds_diagnostic, lv_color_hex(COLOR_SECONDARY), 0);
    lv_obj_add_event_cb(g_ui_home.btn_uds_diagnostic, btn_uds_diagnostic_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label2 = lv_label_create(g_ui_home.btn_uds_diagnostic);
    lv_label_set_text(label2, "UDS 诊断");
    lv_obj_set_style_text_font(label2, &lv_font_montserrat_18, 0);
    lv_obj_center(label2);

    // 文件管理按钮
    g_ui_home.btn_file_manager = lv_btn_create(g_ui_home.container);
    lv_obj_set_size(g_ui_home.btn_file_manager, btn_width, btn_height);
    lv_obj_set_pos(g_ui_home.btn_file_manager, btn_col1_x, btn_start_y + btn_height + btn_spacing_y);
    lv_obj_set_style_radius(g_ui_home.btn_file_manager, 12, 0);
    lv_obj_set_style_bg_color(g_ui_home.btn_file_manager, lv_color_hex(COLOR_SUCCESS), 0);
    lv_obj_add_event_cb(g_ui_home.btn_file_manager, btn_file_manager_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label3 = lv_label_create(g_ui_home.btn_file_manager);
    lv_label_set_text(label3, "文件管理");
    lv_obj_set_style_text_font(label3, &lv_font_montserrat_18, 0);
    lv_obj_center(label3);

    // WiFi 设置按钮
    g_ui_home.btn_wifi_settings = lv_btn_create(g_ui_home.container);
    lv_obj_set_size(g_ui_home.btn_wifi_settings, btn_width, btn_height);
    lv_obj_set_pos(g_ui_home.btn_wifi_settings, btn_col2_x, btn_start_y + btn_height + btn_spacing_y);
    lv_obj_set_style_radius(g_ui_home.btn_wifi_settings, 12, 0);
    lv_obj_set_style_bg_color(g_ui_home.btn_wifi_settings, lv_color_hex(COLOR_WARNING), 0);
    lv_obj_add_event_cb(g_ui_home.btn_wifi_settings, btn_wifi_settings_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label4 = lv_label_create(g_ui_home.btn_wifi_settings);
    lv_label_set_text(label4, "WiFi 设置");
    lv_obj_set_style_text_font(label4, &lv_font_montserrat_18, 0);
    lv_obj_center(label4);

    return g_ui_home.container;
}

void ui_home_update_server_status(bool tcp_connected, bool ws_connected) {
    char status[64];
    if (tcp_connected && ws_connected) {
        snprintf(status, sizeof(status), "服务器: %s:%d (已连接)", 
                g_app_config.server_host, g_app_config.server_port);
        lv_obj_set_style_text_color(g_ui_home.server_status_label, lv_color_hex(COLOR_SUCCESS), 0);
    } else if (tcp_connected || ws_connected) {
        snprintf(status, sizeof(status), "服务器: %s:%d (部分连接)", 
                g_app_config.server_host, g_app_config.server_port);
        lv_obj_set_style_text_color(g_ui_home.server_status_label, lv_color_hex(COLOR_WARNING), 0);
    } else {
        snprintf(status, sizeof(status), "服务器: %s:%d (未连接)", 
                g_app_config.server_host, g_app_config.server_port);
        lv_obj_set_style_text_color(g_ui_home.server_status_label, lv_color_hex(COLOR_DANGER), 0);
    }
    lv_label_set_text(g_ui_home.server_status_label, status);
}

void ui_home_update_wifi_status(const char *ssid, bool connected) {
    char status[64];
    if (connected && ssid) {
        snprintf(status, sizeof(status), "WiFi: %s (已连接)", ssid);
        lv_obj_set_style_text_color(g_ui_home.wifi_status_label, lv_color_hex(COLOR_SUCCESS), 0);
    } else {
        snprintf(status, sizeof(status), "WiFi: 未连接");
        lv_obj_set_style_text_color(g_ui_home.wifi_status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    }
    lv_label_set_text(g_ui_home.wifi_status_label, status);
}

void ui_home_update_can_status(bool can1_active, bool can2_active) {
    char status[64];
    if (can1_active && can2_active) {
        snprintf(status, sizeof(status), "CAN: CAN0+CAN1 已激活");
        lv_obj_set_style_text_color(g_ui_home.can_status_label, lv_color_hex(COLOR_SUCCESS), 0);
    } else if (can1_active) {
        snprintf(status, sizeof(status), "CAN: CAN0 已激活");
        lv_obj_set_style_text_color(g_ui_home.can_status_label, lv_color_hex(COLOR_SUCCESS), 0);
    } else if (can2_active) {
        snprintf(status, sizeof(status), "CAN: CAN1 已激活");
        lv_obj_set_style_text_color(g_ui_home.can_status_label, lv_color_hex(COLOR_SUCCESS), 0);
    } else {
        snprintf(status, sizeof(status), "CAN: 未激活");
        lv_obj_set_style_text_color(g_ui_home.can_status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    }
    lv_label_set_text(g_ui_home.can_status_label, status);
}

