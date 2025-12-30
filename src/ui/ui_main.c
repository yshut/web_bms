#include "ui_main.h"
#include "ui_home.h"
#include "ui_can.h"
#include "ui_uds.h"
#include "ui_file.h"
#include "ui_wifi.h"
#include "../common/app_config.h"
#include <stdio.h>
#include <string.h>

// 全局 UI 实例
ui_main_t g_ui_main;

// 状态栏高度
#define STATUS_BAR_HEIGHT 40

void ui_create_status_bar(lv_obj_t *parent) {
    // 创建状态栏
    g_ui_main.status_bar = lv_obj_create(parent);
    lv_obj_set_size(g_ui_main.status_bar, SCREEN_WIDTH, STATUS_BAR_HEIGHT);
    lv_obj_set_pos(g_ui_main.status_bar, 0, 0);
    lv_obj_set_style_bg_color(g_ui_main.status_bar, lv_color_hex(0xF3F4F6), 0);
    lv_obj_set_style_border_width(g_ui_main.status_bar, 0, 0);
    lv_obj_set_style_radius(g_ui_main.status_bar, 0, 0);
    lv_obj_clear_flag(g_ui_main.status_bar, LV_OBJ_FLAG_SCROLLABLE);

    // 创建时间标签（左侧）
    g_ui_main.time_label = lv_label_create(g_ui_main.status_bar);
    lv_label_set_text(g_ui_main.time_label, "00:00:00");
    lv_obj_set_style_text_color(g_ui_main.time_label, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    lv_obj_align(g_ui_main.time_label, LV_ALIGN_LEFT_MID, 10, 0);

    // 创建状态标签（右侧）
    g_ui_main.status_label = lv_label_create(g_ui_main.status_bar);
    lv_label_set_text(g_ui_main.status_label, "就绪");
    lv_obj_set_style_text_color(g_ui_main.status_label, lv_color_hex(COLOR_SUCCESS), 0);
    lv_obj_align(g_ui_main.status_label, LV_ALIGN_RIGHT_MID, -10, 0);
}

void ui_main_init(void) {
    // 获取屏幕对象
    lv_obj_t *screen = lv_scr_act();

    // 创建主容器
    g_ui_main.main_cont = lv_obj_create(screen);
    lv_obj_set_size(g_ui_main.main_cont, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(g_ui_main.main_cont, 0, 0);
    lv_obj_set_style_bg_color(g_ui_main.main_cont, lv_color_hex(COLOR_BG_MAIN), 0);
    lv_obj_set_style_border_width(g_ui_main.main_cont, 0, 0);
    lv_obj_set_style_radius(g_ui_main.main_cont, 0, 0);
    lv_obj_set_style_pad_all(g_ui_main.main_cont, 0, 0);
    lv_obj_clear_flag(g_ui_main.main_cont, LV_OBJ_FLAG_SCROLLABLE);

    // 创建状态栏
    ui_create_status_bar(g_ui_main.main_cont);

    // 创建内容区域
    g_ui_main.content_area = lv_obj_create(g_ui_main.main_cont);
    lv_obj_set_size(g_ui_main.content_area, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT);
    lv_obj_set_pos(g_ui_main.content_area, 0, STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_color(g_ui_main.content_area, lv_color_hex(COLOR_BG_MAIN), 0);
    lv_obj_set_style_border_width(g_ui_main.content_area, 0, 0);
    lv_obj_set_style_radius(g_ui_main.content_area, 0, 0);
    lv_obj_set_style_pad_all(g_ui_main.content_area, 0, 0);
    lv_obj_clear_flag(g_ui_main.content_area, LV_OBJ_FLAG_SCROLLABLE);

    // 初始化各个页面为 NULL
    for (int i = 0; i < PAGE_COUNT; i++) {
        g_ui_main.pages[i] = NULL;
    }

    // 创建主页
    g_ui_main.pages[PAGE_HOME] = ui_home_create(g_ui_main.content_area);
    
    // 其他页面将在切换时创建（延迟加载）
    g_ui_main.current_page = PAGE_HOME;
}

void ui_main_show_page(page_index_t page) {
    if (page >= PAGE_COUNT) return;

    // 隐藏当前页面
    if (g_ui_main.pages[g_ui_main.current_page]) {
        lv_obj_add_flag(g_ui_main.pages[g_ui_main.current_page], LV_OBJ_FLAG_HIDDEN);
    }

    // 如果目标页面还未创建，则创建它
    if (g_ui_main.pages[page] == NULL) {
        switch (page) {
            case PAGE_HOME:
                g_ui_main.pages[page] = ui_home_create(g_ui_main.content_area);
                break;
            case PAGE_CAN_MONITOR:
                g_ui_main.pages[page] = ui_can_create(g_ui_main.content_area);
                break;
            case PAGE_UDS_DIAGNOSTIC:
                g_ui_main.pages[page] = ui_uds_create(g_ui_main.content_area);
                break;
            case PAGE_FILE_MANAGER:
                g_ui_main.pages[page] = ui_file_create(g_ui_main.content_area);
                break;
            case PAGE_WIFI_SETTINGS:
                g_ui_main.pages[page] = ui_wifi_create(g_ui_main.content_area);
                break;
            default:
                break;
        }
    }

    // 显示目标页面
    if (g_ui_main.pages[page]) {
        lv_obj_clear_flag(g_ui_main.pages[page], LV_OBJ_FLAG_HIDDEN);
    }

    g_ui_main.current_page = page;
}

void ui_main_update_status(const char *status) {
    if (g_ui_main.status_label) {
        lv_label_set_text(g_ui_main.status_label, status);
    }
}

void ui_main_update_time(const char *time) {
    if (g_ui_main.time_label) {
        lv_label_set_text(g_ui_main.time_label, time);
    }
}

void ui_main_set_connection_status(bool tcp_connected, bool ws_connected) {
    char status[64];
    if (tcp_connected && ws_connected) {
        snprintf(status, sizeof(status), "服务器: 已连接");
        lv_obj_set_style_text_color(g_ui_main.status_label, lv_color_hex(COLOR_SUCCESS), 0);
    } else if (tcp_connected || ws_connected) {
        snprintf(status, sizeof(status), "服务器: 部分连接");
        lv_obj_set_style_text_color(g_ui_main.status_label, lv_color_hex(COLOR_WARNING), 0);
    } else {
        snprintf(status, sizeof(status), "服务器: 未连接");
        lv_obj_set_style_text_color(g_ui_main.status_label, lv_color_hex(COLOR_DANGER), 0);
    }
    ui_main_update_status(status);
}

// 工具函数实现
lv_obj_t* ui_create_button(lv_obj_t *parent, const char *text, lv_event_cb_t event_cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 100, 48);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_PRIMARY), 0);
    
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    
    if (event_cb) {
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
    }
    
    return btn;
}

lv_obj_t* ui_create_label(lv_obj_t *parent, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT_PRIMARY), 0);
    return label;
}

lv_obj_t* ui_create_textarea(lv_obj_t *parent, const char *placeholder) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_set_style_radius(ta, 4, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0xD1D5DB), 0);
    return ta;
}

