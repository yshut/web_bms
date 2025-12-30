/**
 * @file ui_websocket.c
 * @brief WebSocket配置UI界面实现
 */

#include "ui_websocket.h"
#include "ui_common.h"
#include "../logic/ws_client.h"
#include "../logic/app_manager.h"
#include "../utils/logger.h"
#include "../utils/app_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* WebSocket UI结构体 */
typedef struct {
    lv_obj_t *main_cont;          /* 主容器 */
    lv_obj_t *host_input;         /* 服务器地址输入框 */
    lv_obj_t *port_input;         /* 端口输入框 */
    lv_obj_t *status_label;       /* 状态标签 */
    lv_obj_t *start_btn;          /* 启动按钮 */
    lv_obj_t *stop_btn;           /* 停止按钮 */
    lv_obj_t *device_id_label;    /* 设备ID标签 */
    bool is_connected;            /* 连接状态 */
} ui_websocket_t;

static ui_websocket_t *g_ws_ui = NULL;

/* 前向声明 */
static void back_btn_event_cb(lv_event_t *e);
static void start_btn_event_cb(lv_event_t *e);
static void stop_btn_event_cb(lv_event_t *e);

/**
 * @brief 返回按钮事件回调
 */
static void back_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        log_info("返回主页");
        app_manager_switch_to_page(APP_PAGE_HOME);
    }
}

/**
 * @brief 启动按钮事件回调
 */
static void start_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (!g_ws_ui) return;
        
        const char *host_text = lv_textarea_get_text(g_ws_ui->host_input);
        const char *port_text = lv_textarea_get_text(g_ws_ui->port_input);
        
        if (!host_text || strlen(host_text) == 0) {
            lv_label_set_text(g_ws_ui->status_label, "错误：请输入服务器地址");
            return;
        }
        
        uint16_t port = (uint16_t)atoi(port_text);
        if (port == 0) {
            port = g_app_config.ws_port ? g_app_config.ws_port : 5052;
        }
        
        log_info("启动WebSocket连接: %s:%d", host_text, port);

        /* 更新全局配置，并全量写回 ws_config.txt（保证所有配置项都在同一个文件里） */
        strncpy(g_app_config.ws_host, host_text, sizeof(g_app_config.ws_host) - 1);
        g_app_config.ws_host[sizeof(g_app_config.ws_host) - 1] = '\0';
        g_app_config.ws_port = port;
        {
            char saved_path[256] = {0};
            if (app_config_save_best(saved_path, sizeof(saved_path)) == 0) {
                log_info("配置已保存: %s", saved_path);
            } else {
                log_warn("配置保存失败（可能未挂载 UDISK/SDCARD 或只读）");
            }
        }

        /* 停止旧连接（若存在），再用新配置重建连接 */
        ws_client_stop();
        ws_client_deinit();

        // 配置并启动WebSocket客户端（使用统一配置）
        ws_config_t config = {
            .port = port,
            .use_ssl = g_app_config.ws_use_ssl,
            .reconnect_interval_ms = (int)g_app_config.ws_reconnect_interval_ms,
            .keepalive_interval_s = (int)g_app_config.ws_keepalive_interval_s,
        };
        strncpy(config.host, host_text, sizeof(config.host) - 1);
        config.host[sizeof(config.host) - 1] = '\0';
        strncpy(config.path, g_app_config.ws_path[0] ? g_app_config.ws_path : "/ws", sizeof(config.path) - 1);
        config.path[sizeof(config.path) - 1] = '\0';
        
        if (ws_client_init(&config) == 0) {
            if (ws_client_start() == 0) {
                lv_label_set_text(g_ws_ui->status_label, "正在连接...");
                lv_obj_add_state(g_ws_ui->start_btn, LV_STATE_DISABLED);
                lv_obj_clear_state(g_ws_ui->stop_btn, LV_STATE_DISABLED);
            } else {
                lv_label_set_text(g_ws_ui->status_label, "启动失败");
            }
        } else {
            lv_label_set_text(g_ws_ui->status_label, "初始化失败");
        }
    }
}

/**
 * @brief 停止按钮事件回调
 */
static void stop_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (!g_ws_ui) return;
        
        log_info("停止WebSocket连接");
        
        ws_client_stop();
        ws_client_deinit();
        
        lv_label_set_text(g_ws_ui->status_label, "已断开");
        lv_obj_clear_state(g_ws_ui->start_btn, LV_STATE_DISABLED);
        lv_obj_add_state(g_ws_ui->stop_btn, LV_STATE_DISABLED);
        
        g_ws_ui->is_connected = false;
    }
}

/**
 * @brief 创建WebSocket配置界面
 */
lv_obj_t* ui_websocket_create(lv_obj_t *parent)
{
    log_info("创建WebSocket配置界面");
    
    if (g_ws_ui) {
        log_warn("WebSocket UI已存在");
        return g_ws_ui->main_cont;
    }
    
    g_ws_ui = (ui_websocket_t*)lv_mem_alloc(sizeof(ui_websocket_t));
    if (!g_ws_ui) {
        log_error("分配内存失败");
        return NULL;
    }
    memset(g_ws_ui, 0, sizeof(ui_websocket_t));
    
    /* 主容器 */
    g_ws_ui->main_cont = lv_obj_create(parent);
    lv_obj_set_size(g_ws_ui->main_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_ws_ui->main_cont, lv_color_hex(0x202020), 0);
    lv_obj_clear_flag(g_ws_ui->main_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    /* 标题栏 */
    lv_obj_t *title_bar = lv_obj_create(g_ws_ui->main_cont);
    lv_obj_set_size(title_bar, LV_PCT(100), 60);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x303030), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 0, 0);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    /* 返回按钮 */
    lv_obj_t *back_btn = lv_btn_create(title_bar);
    lv_obj_set_size(back_btn, 80, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "返回");
    lv_obj_set_style_text_font(back_label, ui_common_get_font(), 0);
    lv_obj_center(back_label);
    
    /* 标题 */
    lv_obj_t *title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "WebSocket远程控制");
    lv_obj_set_style_text_font(title_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(title_label);
    
    /* 内容区域 */
    lv_obj_t *content = lv_obj_create(g_ws_ui->main_cont);
    lv_obj_set_size(content, LV_PCT(95), LV_PCT(100) - 80);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x282828), 0);
    lv_obj_set_style_pad_all(content, 20, 0);
    
    int y_offset = 0;
    
    /* 设备ID显示 */
    lv_obj_t *device_id_cont = lv_obj_create(content);
    lv_obj_set_size(device_id_cont, LV_PCT(100), 60);
    lv_obj_align(device_id_cont, LV_ALIGN_TOP_MID, 0, y_offset);
    lv_obj_set_style_bg_color(device_id_cont, lv_color_hex(0x404040), 0);
    lv_obj_clear_flag(device_id_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *device_id_title = lv_label_create(device_id_cont);
    lv_label_set_text(device_id_title, "设备ID:");
    lv_obj_set_style_text_font(device_id_title, ui_common_get_font(), 0);
    lv_obj_align(device_id_title, LV_ALIGN_LEFT_MID, 10, -10);
    
    g_ws_ui->device_id_label = lv_label_create(device_id_cont);
    lv_label_set_text(g_ws_ui->device_id_label, ws_client_get_device_id());
    lv_obj_set_style_text_font(g_ws_ui->device_id_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(g_ws_ui->device_id_label, lv_color_hex(0x00FF00), 0);
    lv_obj_align(g_ws_ui->device_id_label, LV_ALIGN_LEFT_MID, 10, 10);
    
    y_offset += 80;
    
    /* 服务器地址输入 */
    lv_obj_t *host_label = lv_label_create(content);
    lv_label_set_text(host_label, "服务器地址:");
    lv_obj_set_style_text_font(host_label, ui_common_get_font(), 0);
    lv_obj_align(host_label, LV_ALIGN_TOP_LEFT, 0, y_offset);
    
    y_offset += 30;
    
    g_ws_ui->host_input = lv_textarea_create(content);
    lv_obj_set_size(g_ws_ui->host_input, LV_PCT(100), 50);
    lv_obj_align(g_ws_ui->host_input, LV_ALIGN_TOP_LEFT, 0, y_offset);
    lv_textarea_set_one_line(g_ws_ui->host_input, true);
    lv_textarea_set_max_length(g_ws_ui->host_input, 64);
    lv_obj_set_style_text_font(g_ws_ui->host_input, ui_common_get_font(), 0);
    
    /* 使用统一配置（已由 main.c 启动时加载 /mnt/UDISK/ws_config.txt） */
    const char *saved_host = g_app_config.ws_host[0] ? g_app_config.ws_host : "192.168.100.1";
    uint16_t saved_port = g_app_config.ws_port ? g_app_config.ws_port : 5052;
    lv_textarea_set_text(g_ws_ui->host_input, saved_host);
    
    y_offset += 70;
    
    /* 端口输入 */
    lv_obj_t *port_label = lv_label_create(content);
    lv_label_set_text(port_label, "端口:");
    lv_obj_set_style_text_font(port_label, ui_common_get_font(), 0);
    lv_obj_align(port_label, LV_ALIGN_TOP_LEFT, 0, y_offset);
    
    y_offset += 30;
    
    g_ws_ui->port_input = lv_textarea_create(content);
    lv_obj_set_size(g_ws_ui->port_input, 150, 50);
    lv_obj_align(g_ws_ui->port_input, LV_ALIGN_TOP_LEFT, 0, y_offset);
    lv_textarea_set_one_line(g_ws_ui->port_input, true);
    lv_textarea_set_max_length(g_ws_ui->port_input, 5);
    lv_textarea_set_accepted_chars(g_ws_ui->port_input, "0123456789");
    lv_obj_set_style_text_font(g_ws_ui->port_input, ui_common_get_font(), 0);
    
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)saved_port);
    lv_textarea_set_text(g_ws_ui->port_input, port_str);
    
    y_offset += 70;
    
    /* 启动/停止按钮 */
    g_ws_ui->start_btn = lv_btn_create(content);
    lv_obj_set_size(g_ws_ui->start_btn, 150, 50);
    lv_obj_align(g_ws_ui->start_btn, LV_ALIGN_TOP_LEFT, 0, y_offset);
    lv_obj_add_event_cb(g_ws_ui->start_btn, start_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(g_ws_ui->start_btn, lv_color_hex(0x008000), 0);
    
    lv_obj_t *start_label = lv_label_create(g_ws_ui->start_btn);
    lv_label_set_text(start_label, "启动连接");
    lv_obj_set_style_text_font(start_label, ui_common_get_font(), 0);
    lv_obj_center(start_label);
    
    g_ws_ui->stop_btn = lv_btn_create(content);
    lv_obj_set_size(g_ws_ui->stop_btn, 150, 50);
    lv_obj_align(g_ws_ui->stop_btn, LV_ALIGN_TOP_LEFT, 170, y_offset);
    lv_obj_add_event_cb(g_ws_ui->stop_btn, stop_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(g_ws_ui->stop_btn, lv_color_hex(0x800000), 0);
    lv_obj_add_state(g_ws_ui->stop_btn, LV_STATE_DISABLED);  // 初始禁用
    
    lv_obj_t *stop_label = lv_label_create(g_ws_ui->stop_btn);
    lv_label_set_text(stop_label, "停止连接");
    lv_obj_set_style_text_font(stop_label, ui_common_get_font(), 0);
    lv_obj_center(stop_label);
    
    y_offset += 70;
    
    /* 连接状态显示 */
    g_ws_ui->status_label = lv_label_create(content);
    lv_label_set_text(g_ws_ui->status_label, "状态: 未连接");
    lv_obj_set_style_text_font(g_ws_ui->status_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(g_ws_ui->status_label, lv_color_hex(0xFFFF00), 0);
    lv_obj_align(g_ws_ui->status_label, LV_ALIGN_TOP_LEFT, 0, y_offset);
    
    log_info("WebSocket配置界面创建成功");
    
    return g_ws_ui->main_cont;
}

/**
 * @brief 销毁WebSocket配置界面
 */
void ui_websocket_destroy(void)
{
    if (!g_ws_ui) return;
    
    log_info("销毁WebSocket配置界面");
    
    if (g_ws_ui->main_cont) {
        lv_obj_del(g_ws_ui->main_cont);
    }
    
    lv_mem_free(g_ws_ui);
    g_ws_ui = NULL;
}

/**
 * @brief 更新连接状态显示
 */
void ui_websocket_update_status(bool connected, const char *host, uint16_t port)
{
    if (!g_ws_ui || !g_ws_ui->status_label) return;
    
    g_ws_ui->is_connected = connected;
    
    char status_text[256];
    if (connected) {
        snprintf(status_text, sizeof(status_text), 
                "状态: 已连接到 %s:%d", host, port);
        lv_label_set_text(g_ws_ui->status_label, status_text);
        lv_obj_set_style_text_color(g_ws_ui->status_label, lv_color_hex(0x00FF00), 0);
    } else {
        snprintf(status_text, sizeof(status_text), 
                "状态: 已断开 (上次连接: %s:%d)", host, port);
        lv_label_set_text(g_ws_ui->status_label, status_text);
        lv_obj_set_style_text_color(g_ws_ui->status_label, lv_color_hex(0xFF0000), 0);
    }
}

