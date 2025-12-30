/**
 * @file ui_home.c
 * @brief 主界面UI实现
 */

#include "ui_home.h"
#include "ui_common.h"
#include "ui_wifi.h"
#include "../logic/app_manager.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* 按钮ID定义 */
typedef enum {
    BTN_ID_CAN = 0,
    BTN_ID_UDS,
    BTN_ID_FILE,
    BTN_ID_WIFI,
} home_btn_id_t;

/**
 * @brief 按钮点击事件回调
 */
static void home_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        home_btn_id_t btn_id = (home_btn_id_t)lv_event_get_user_data(e);
        
        const char *page_name = "";
        app_page_t target_page = APP_PAGE_HOME;
        
        switch (btn_id) {
            case BTN_ID_CAN:
                page_name = "CAN监控";
                target_page = APP_PAGE_CAN_MONITOR;
                break;
                
            case BTN_ID_UDS:
                page_name = "UDS诊断";
                target_page = APP_PAGE_UDS;
                break;
                
            case BTN_ID_FILE:
                page_name = "文件管理";
                target_page = APP_PAGE_FILE_MANAGER;
                break;
                
            case BTN_ID_WIFI:
                page_name = "WiFi设置";
                target_page = APP_PAGE_WIFI;
                break;
                
            default:
                log_warn("未知按钮ID: %d", btn_id);
                return;
        }

        log_info("用户点击: %s", page_name);
        
        /* 调用页面切换 */
        app_manager_switch_to_page(target_page);
    }
}

/**
 * @brief 创建功能按钮
 */
static lv_obj_t* create_function_button(lv_obj_t *parent, const char *text, 
                                        const char *icon, home_btn_id_t id)
{
    /* 创建按钮 */
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 220, 100);
    lv_obj_add_style(btn, &g_style_btn_large, 0);
    lv_obj_add_event_cb(btn, home_btn_event_cb, LV_EVENT_CLICKED, (void*)id);
    
    /* 创建按钮内容容器 */
    lv_obj_t *content = lv_obj_create(btn);
    lv_obj_set_size(content, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(content);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    /* 关键：允许点击事件穿透到父按钮 */
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE);
    
    /* 图标 */
    if (icon) {
        lv_obj_t *icon_label = lv_label_create(content);
        lv_label_set_text(icon_label, icon);
        lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(icon_label, lv_color_hex(0x22C55E), 0);
        /* 确保标签不拦截点击事件 */
        lv_obj_clear_flag(icon_label, LV_OBJ_FLAG_CLICKABLE);
    }
    
    /* 文本 */
    lv_obj_t *text_label = lv_label_create(content);
    lv_label_set_text(text_label, text);
    lv_obj_set_style_text_font(text_label, ui_common_get_font(), 0);
    /* 确保标签不拦截点击事件 */
    lv_obj_clear_flag(text_label, LV_OBJ_FLAG_CLICKABLE);
    
    return btn;
}

/**
 * @brief 状态更新定时器回调
 */
static void status_update_timer_cb(lv_timer_t *timer)
{
    ui_home_t *ui = (ui_home_t*)timer->user_data;
    
    /* 更新时间 */
    ui_home_update_time(ui);
    
    /* 更新 WiFi 状态 */
    if (ui_wifi_is_connected()) {
        const char *ssid = ui_wifi_get_connected_ssid();
        const char *ip = ui_wifi_get_ip_address();
        ui_home_update_wifi_status(ui, true, ssid, ip);
    } else {
        ui_home_update_wifi_status(ui, false, NULL, NULL);
    }
}

/**
 * @brief 创建主界面
 */
ui_home_t* ui_home_create(void)
{
    ui_home_t *ui = (ui_home_t*)malloc(sizeof(ui_home_t));
    if (!ui) {
        log_error("分配主界面内存失败");
        return NULL;
    }
    
    /* 创建屏幕 */
    ui->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui->screen, lv_color_white(), 0);
    
    /* ========== 顶部标题区域 ========== */
    lv_obj_t *header = lv_obj_create(ui->screen);
    lv_obj_set_size(header, LV_PCT(100), 80);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x22C55E), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    
    /* 主标题 */
    ui->title_label = lv_label_create(header);
    lv_label_set_text(ui->title_label, "工业控制系统");
    lv_obj_set_style_text_font(ui->title_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(ui->title_label, lv_color_white(), 0);
    lv_obj_align(ui->title_label, LV_ALIGN_CENTER, 0, -10);
    
    /* 副标题 */
    ui->subtitle_label = lv_label_create(header);
    lv_label_set_text(ui->subtitle_label, "车载诊断工具");
    lv_obj_set_style_text_font(ui->subtitle_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(ui->subtitle_label, lv_color_white(), 0);
    lv_obj_align(ui->subtitle_label, LV_ALIGN_CENTER, 0, 15);
    
    /* ========== 功能按钮区域 ========== */
    lv_obj_t *btn_container = lv_obj_create(ui->screen);
    lv_obj_set_size(btn_container, 500, 260);
    lv_obj_align(btn_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, 
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_container, 0, 0);
    
    /* 创建四个功能按钮 */
    ui->can_btn = create_function_button(btn_container, "监控", 
                                         LV_SYMBOL_EYE_OPEN, BTN_ID_CAN);
    ui->uds_btn = create_function_button(btn_container, "刷写诊断", 
                                         LV_SYMBOL_SETTINGS, BTN_ID_UDS);
    ui->file_btn = create_function_button(btn_container, "文件管理", 
                                          LV_SYMBOL_DIRECTORY, BTN_ID_FILE);
    ui->wifi_btn = create_function_button(btn_container, "网络", 
                                          LV_SYMBOL_WIFI, BTN_ID_WIFI);
    
    /* ========== 底部状态栏 ========== */
    lv_obj_t *status_bar = lv_obj_create(ui->screen);
    lv_obj_set_size(status_bar, LV_PCT(100), 60);
    lv_obj_align(status_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0xF5F5F5), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    
    /* 服务器状态 */
    lv_obj_t *server_status_cont = lv_obj_create(status_bar);
    lv_obj_set_size(server_status_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(server_status_cont, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_flex_flow(server_status_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(server_status_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(server_status_cont, 0, 0);
    
    ui->server_status_icon = lv_label_create(server_status_cont);
    lv_label_set_text(ui->server_status_icon, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(ui->server_status_icon, lv_color_hex(0xEF4444), 0);
    
    ui->server_status_label = lv_label_create(server_status_cont);
    lv_label_set_text(ui->server_status_label, " 服务器: 离线");
    lv_obj_set_style_text_font(ui->server_status_label, ui_common_get_font(), 0);
    
    /* WiFi状态 */
    lv_obj_t *wifi_status_cont = lv_obj_create(status_bar);
    lv_obj_set_size(wifi_status_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(wifi_status_cont, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(wifi_status_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(wifi_status_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_status_cont, 0, 0);
    
    ui->wifi_status_icon = lv_label_create(wifi_status_cont);
    lv_label_set_text(ui->wifi_status_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(ui->wifi_status_icon, lv_color_hex(0x9CA3AF), 0);
    
    ui->wifi_status_label = lv_label_create(wifi_status_cont);
    lv_label_set_text(ui->wifi_status_label, " 网络: 离线");
    lv_obj_set_style_text_font(ui->wifi_status_label, ui_common_get_font(), 0);
    
    /* 时间显示 */
    ui->time_label = lv_label_create(status_bar);
    lv_label_set_text(ui->time_label, "00:00:00");
    lv_obj_align(ui->time_label, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_text_font(ui->time_label, ui_common_get_font(), 0);
    
    /* 创建状态更新定时器（每秒更新一次） */
    ui->status_update_timer = lv_timer_create(status_update_timer_cb, 1000, ui);
    
    log_info("主界面创建成功");
    return ui;
}

/**
 * @brief 销毁主界面
 */
void ui_home_destroy(ui_home_t *ui)
{
    if (!ui) return;
    
    if (ui->status_update_timer) {
        lv_timer_del(ui->status_update_timer);
    }
    
    if (ui->screen) {
        lv_obj_del(ui->screen);
    }
    
    free(ui);
    log_info("主界面已销毁");
}

/**
 * @brief 更新服务器连接状态
 */
void ui_home_update_server_status(ui_home_t *ui, bool connected, 
                                   const char *host, uint16_t port)
{
    if (!ui) return;
    
    char buf[128];
    
    if (connected) {
        lv_label_set_text(ui->server_status_icon, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(ui->server_status_icon, lv_color_hex(0x22C55E), 0);
        snprintf(buf, sizeof(buf), " 服务器: %s:%d", host, port);
    } else {
        lv_label_set_text(ui->server_status_icon, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(ui->server_status_icon, lv_color_hex(0xEF4444), 0);
        snprintf(buf, sizeof(buf), " 服务器: 未连接");
    }
    
    lv_label_set_text(ui->server_status_label, buf);
}

/**
 * @brief 更新WiFi连接状态
 */
void ui_home_update_wifi_status(ui_home_t *ui, bool connected, 
                                const char *ssid, const char *ip)
{
    if (!ui) return;
    
    char buf[128];
    
    if (connected) {
        lv_obj_set_style_text_color(ui->wifi_status_icon, lv_color_hex(0x22C55E), 0);
        snprintf(buf, sizeof(buf), " WiFi: %s (%s)", ssid, ip);
    } else {
        lv_obj_set_style_text_color(ui->wifi_status_icon, lv_color_hex(0x9CA3AF), 0);
        snprintf(buf, sizeof(buf), " WiFi: 未连接");
    }
    
    lv_label_set_text(ui->wifi_status_label, buf);
}

/**
 * @brief 更新时间显示
 */
void ui_home_update_time(ui_home_t *ui)
{
    if (!ui) return;
    
    time_t now;
    struct tm *tm_info;
    char time_str[32];
    
    time(&now);
    tm_info = localtime(&now);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    lv_label_set_text(ui->time_label, time_str);
}

/* 全局主界面实例（用于异步更新） */
static ui_home_t *g_ui_home_instance = NULL;

/**
 * @brief 注册主界面实例
 */
void ui_home_register_instance(ui_home_t *ui)
{
    g_ui_home_instance = ui;
}

/**
 * @brief 异步更新服务器状态的回调结构
 */
typedef struct {
    bool connected;
    char host[128];
    uint16_t port;
} server_status_update_data_t;

/**
 * @brief LVGL异步回调函数：更新服务器状态
 */
static void server_status_update_cb(void *user_data)
{
    server_status_update_data_t *data = (server_status_update_data_t *)user_data;
    
    if (g_ui_home_instance) {
        ui_home_update_server_status(g_ui_home_instance, data->connected, data->host, data->port);
    }
    
    free(data);
}

/**
 * @brief 异步更新服务器状态（线程安全）
 */
void ui_home_update_server_status_async(bool connected, const char *host, uint16_t port)
{
    server_status_update_data_t *data = (server_status_update_data_t *)malloc(sizeof(server_status_update_data_t));
    if (!data) return;
    
    data->connected = connected;
    data->port = port;
    strncpy(data->host, host ? host : "", sizeof(data->host) - 1);
    data->host[sizeof(data->host) - 1] = '\0';
    
    lv_async_call(server_status_update_cb, data);
}

