#include "ui_wifi.h"
#include "ui_main.h"
#include "../common/app_config.h"
#include "../wifi/wifi_manager.h"
#include <stdio.h>
#include <string.h>

static ui_wifi_t g_ui_wifi;

// 返回按钮事件
static void btn_back_event(lv_event_t *e) {
    ui_main_show_page(PAGE_HOME);
}

// 扫描按钮事件
static void btn_scan_event(lv_event_t *e) {
    wifi_manager_scan();
    ui_wifi_update_status("正在扫描WiFi...");
}

// 连接按钮事件
static void btn_connect_event(lv_event_t *e) {
    const char *ssid = lv_textarea_get_text(g_ui_wifi.ssid_input);
    const char *password = lv_textarea_get_text(g_ui_wifi.password_input);
    
    if (ssid && strlen(ssid) > 0) {
        wifi_manager_connect(ssid, password);
        char status[128];
        snprintf(status, sizeof(status), "正在连接到: %s", ssid);
        ui_wifi_update_status(status);
    } else {
        ui_wifi_update_status("请输入SSID");
    }
}

// 断开按钮事件
static void btn_disconnect_event(lv_event_t *e) {
    wifi_manager_disconnect();
    ui_wifi_update_status("已断开WiFi连接");
}

lv_obj_t* ui_wifi_create(lv_obj_t *parent) {
    // 创建主容器
    g_ui_wifi.container = lv_obj_create(parent);
    lv_obj_set_size(g_ui_wifi.container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_ui_wifi.container, lv_color_hex(COLOR_BG_MAIN), 0);
    lv_obj_set_style_border_width(g_ui_wifi.container, 0, 0);
    lv_obj_set_style_radius(g_ui_wifi.container, 0, 0);
    lv_obj_set_style_pad_all(g_ui_wifi.container, 10, 0);

    // 创建返回按钮
    g_ui_wifi.btn_back = lv_btn_create(g_ui_wifi.container);
    lv_obj_set_size(g_ui_wifi.btn_back, 80, 40);
    lv_obj_set_pos(g_ui_wifi.btn_back, 10, 10);
    lv_obj_add_event_cb(g_ui_wifi.btn_back, btn_back_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(g_ui_wifi.btn_back);
    lv_label_set_text(back_label, "< 返回");
    lv_obj_center(back_label);

    // 标题
    lv_obj_t *title = lv_label_create(g_ui_wifi.container);
    lv_label_set_text(title, "WiFi 设置");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(title, 100, 15);

    int y_pos = 70;

    // 当前连接状态
    g_ui_wifi.current_ssid_label = lv_label_create(g_ui_wifi.container);
    lv_label_set_text(g_ui_wifi.current_ssid_label, "当前连接: 未连接");
    lv_obj_set_pos(g_ui_wifi.current_ssid_label, 20, y_pos);
    lv_obj_set_style_text_color(g_ui_wifi.current_ssid_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);

    // SSID 输入
    y_pos += 40;
    lv_obj_t *ssid_label = lv_label_create(g_ui_wifi.container);
    lv_label_set_text(ssid_label, "SSID:");
    lv_obj_set_pos(ssid_label, 20, y_pos);

    g_ui_wifi.ssid_input = lv_textarea_create(g_ui_wifi.container);
    lv_obj_set_size(g_ui_wifi.ssid_input, 300, 40);
    lv_obj_set_pos(g_ui_wifi.ssid_input, 100, y_pos - 5);
    lv_textarea_set_placeholder_text(g_ui_wifi.ssid_input, "输入WiFi名称");
    lv_textarea_set_one_line(g_ui_wifi.ssid_input, true);

    // 密码输入
    y_pos += 50;
    lv_obj_t *pwd_label = lv_label_create(g_ui_wifi.container);
    lv_label_set_text(pwd_label, "密码:");
    lv_obj_set_pos(pwd_label, 20, y_pos);

    g_ui_wifi.password_input = lv_textarea_create(g_ui_wifi.container);
    lv_obj_set_size(g_ui_wifi.password_input, 300, 40);
    lv_obj_set_pos(g_ui_wifi.password_input, 100, y_pos - 5);
    lv_textarea_set_placeholder_text(g_ui_wifi.password_input, "输入密码");
    lv_textarea_set_one_line(g_ui_wifi.password_input, true);
    lv_textarea_set_password_mode(g_ui_wifi.password_input, true);

    // 控制按钮
    y_pos += 60;
    int btn_width = 90;
    int btn_height = 40;
    int btn_spacing = 15;

    // 扫描按钮
    g_ui_wifi.btn_scan = lv_btn_create(g_ui_wifi.container);
    lv_obj_set_size(g_ui_wifi.btn_scan, btn_width, btn_height);
    lv_obj_set_pos(g_ui_wifi.btn_scan, 20, y_pos);
    lv_obj_set_style_bg_color(g_ui_wifi.btn_scan, lv_color_hex(COLOR_PRIMARY), 0);
    lv_obj_add_event_cb(g_ui_wifi.btn_scan, btn_scan_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_label = lv_label_create(g_ui_wifi.btn_scan);
    lv_label_set_text(scan_label, "扫描");
    lv_obj_center(scan_label);

    // 连接按钮
    g_ui_wifi.btn_connect = lv_btn_create(g_ui_wifi.container);
    lv_obj_set_size(g_ui_wifi.btn_connect, btn_width, btn_height);
    lv_obj_set_pos(g_ui_wifi.btn_connect, 20 + btn_width + btn_spacing, y_pos);
    lv_obj_set_style_bg_color(g_ui_wifi.btn_connect, lv_color_hex(COLOR_SUCCESS), 0);
    lv_obj_add_event_cb(g_ui_wifi.btn_connect, btn_connect_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *connect_label = lv_label_create(g_ui_wifi.btn_connect);
    lv_label_set_text(connect_label, "连接");
    lv_obj_center(connect_label);

    // 断开按钮
    g_ui_wifi.btn_disconnect = lv_btn_create(g_ui_wifi.container);
    lv_obj_set_size(g_ui_wifi.btn_disconnect, btn_width, btn_height);
    lv_obj_set_pos(g_ui_wifi.btn_disconnect, 20 + (btn_width + btn_spacing) * 2, y_pos);
    lv_obj_set_style_bg_color(g_ui_wifi.btn_disconnect, lv_color_hex(COLOR_DANGER), 0);
    lv_obj_add_event_cb(g_ui_wifi.btn_disconnect, btn_disconnect_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *disconnect_label = lv_label_create(g_ui_wifi.btn_disconnect);
    lv_label_set_text(disconnect_label, "断开");
    lv_obj_center(disconnect_label);

    // WiFi 列表
    y_pos += 60;
    lv_obj_t *list_title = lv_label_create(g_ui_wifi.container);
    lv_label_set_text(list_title, "可用网络:");
    lv_obj_set_pos(list_title, 20, y_pos);

    int list_height = SCREEN_HEIGHT - STATUS_BAR_HEIGHT - y_pos - 80;
    g_ui_wifi.wifi_list = lv_list_create(g_ui_wifi.container);
    lv_obj_set_size(g_ui_wifi.wifi_list, 480, list_height);
    lv_obj_set_pos(g_ui_wifi.wifi_list, 20, y_pos + 30);
    lv_obj_set_style_bg_color(g_ui_wifi.wifi_list, lv_color_hex(0xF9FAFB), 0);

    // 状态标签
    g_ui_wifi.status_label = lv_label_create(g_ui_wifi.container);
    lv_label_set_text(g_ui_wifi.status_label, "就绪");
    lv_obj_set_pos(g_ui_wifi.status_label, 20, SCREEN_HEIGHT - STATUS_BAR_HEIGHT - 30);
    lv_obj_set_style_text_color(g_ui_wifi.status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);

    return g_ui_wifi.container;
}

void ui_wifi_update_list(const char **ssids, int *signal_strengths, int count) {
    if (!g_ui_wifi.wifi_list) return;
    
    // 清空现有列表
    lv_obj_clean(g_ui_wifi.wifi_list);
    
    // 添加WiFi网络
    for (int i = 0; i < count; i++) {
        if (ssids[i]) {
            char text[128];
            const char *signal_icon;
            
            // 根据信号强度选择图标
            if (signal_strengths && signal_strengths[i] > -50) {
                signal_icon = LV_SYMBOL_WIFI;
            } else if (signal_strengths && signal_strengths[i] > -70) {
                signal_icon = LV_SYMBOL_WIFI;
            } else {
                signal_icon = LV_SYMBOL_WIFI;
            }
            
            if (signal_strengths) {
                snprintf(text, sizeof(text), "%s (%d dBm)", ssids[i], signal_strengths[i]);
            } else {
                snprintf(text, sizeof(text), "%s", ssids[i]);
            }
            
            lv_obj_t *btn = lv_list_add_btn(g_ui_wifi.wifi_list, signal_icon, text);
            lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        }
    }
}

void ui_wifi_update_connection_status(const char *ssid, bool connected) {
    if (g_ui_wifi.current_ssid_label) {
        char text[128];
        if (connected && ssid) {
            snprintf(text, sizeof(text), "当前连接: %s", ssid);
            lv_obj_set_style_text_color(g_ui_wifi.current_ssid_label, lv_color_hex(COLOR_SUCCESS), 0);
        } else {
            snprintf(text, sizeof(text), "当前连接: 未连接");
            lv_obj_set_style_text_color(g_ui_wifi.current_ssid_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        }
        lv_label_set_text(g_ui_wifi.current_ssid_label, text);
    }
}

void ui_wifi_update_status(const char *status) {
    if (g_ui_wifi.status_label && status) {
        lv_label_set_text(g_ui_wifi.status_label, status);
    }
}

