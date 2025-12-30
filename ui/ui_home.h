/**
 * @file ui_home.h
 * @brief 主界面UI - 对应QT中的HomePage
 */

#ifndef UI_HOME_H
#define UI_HOME_H

#include "lvgl.h"
#include <stdbool.h>

/**
 * @brief 主界面UI结构体
 */
typedef struct {
    lv_obj_t *screen;              // 主屏幕对象
    lv_obj_t *title_label;         // 标题标签
    lv_obj_t *subtitle_label;      // 副标题标签
    
    /* 功能按钮 */
    lv_obj_t *can_btn;             // CAN监控按钮
    lv_obj_t *uds_btn;             // UDS诊断按钮
    lv_obj_t *file_btn;            // 文件管理按钮
    lv_obj_t *wifi_btn;            // WiFi设置按钮
    
    /* 状态显示 */
    lv_obj_t *server_status_icon;  // 服务器连接状态图标
    lv_obj_t *server_status_label; // 服务器连接状态文本
    lv_obj_t *wifi_status_icon;    // WiFi连接状态图标
    lv_obj_t *wifi_status_label;   // WiFi连接状态文本
    lv_obj_t *time_label;          // 时间显示标签
    
    /* 定时器 */
    lv_timer_t *status_update_timer;  // 状态更新定时器
} ui_home_t;

/**
 * @brief 创建主界面
 * @return ui_home_t* 主界面结构体指针，失败返回NULL
 */
ui_home_t* ui_home_create(void);

/**
 * @brief 销毁主界面
 * @param ui 主界面结构体指针
 */
void ui_home_destroy(ui_home_t *ui);

/**
 * @brief 更新服务器连接状态
 * @param ui 主界面结构体指针
 * @param connected 是否已连接
 * @param host 服务器地址
 * @param port 服务器端口
 */
void ui_home_update_server_status(ui_home_t *ui, bool connected, 
                                   const char *host, uint16_t port);

/**
 * @brief 更新WiFi连接状态
 * @param ui 主界面结构体指针
 * @param connected 是否已连接
 * @param ssid WiFi名称
 * @param ip IP地址
 */
void ui_home_update_wifi_status(ui_home_t *ui, bool connected, 
                                const char *ssid, const char *ip);

/**
 * @brief 更新时间显示
 * @param ui 主界面结构体指针
 */
void ui_home_update_time(ui_home_t *ui);

#endif /* UI_HOME_H */

