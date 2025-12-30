#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "lvgl.h"
#include <stdbool.h>

// 页面索引
typedef enum {
    PAGE_HOME = 0,
    PAGE_CAN_MONITOR,
    PAGE_UDS_DIAGNOSTIC,
    PAGE_FILE_MANAGER,
    PAGE_WIFI_SETTINGS,
    PAGE_COUNT
} page_index_t;

// UI 主控制器结构
typedef struct {
    lv_obj_t *main_cont;        // 主容器
    lv_obj_t *status_bar;       // 状态栏
    lv_obj_t *content_area;     // 内容区域
    lv_obj_t *pages[PAGE_COUNT]; // 各个页面容器
    lv_obj_t *time_label;       // 时间标签
    lv_obj_t *status_label;     // 状态标签
    page_index_t current_page;  // 当前页面
} ui_main_t;

// 全局 UI 实例
extern ui_main_t g_ui_main;

// UI 初始化和控制函数
void ui_main_init(void);
void ui_main_show_page(page_index_t page);
void ui_main_update_status(const char *status);
void ui_main_update_time(const char *time);
void ui_main_set_connection_status(bool tcp_connected, bool ws_connected);

// 创建状态栏
void ui_create_status_bar(lv_obj_t *parent);

// 工具函数
lv_obj_t* ui_create_button(lv_obj_t *parent, const char *text, lv_event_cb_t event_cb);
lv_obj_t* ui_create_label(lv_obj_t *parent, const char *text);
lv_obj_t* ui_create_textarea(lv_obj_t *parent, const char *placeholder);

#endif // UI_MAIN_H

