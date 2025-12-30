/**
 * @file ui_websocket.h
 * @brief WebSocket配置UI界面
 */

#ifndef UI_WEBSOCKET_H
#define UI_WEBSOCKET_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建WebSocket配置界面
 * @param parent 父容器
 * @return UI对象指针
 */
lv_obj_t* ui_websocket_create(lv_obj_t *parent);

/**
 * @brief 销毁WebSocket配置界面
 */
void ui_websocket_destroy(void);

/**
 * @brief 更新连接状态显示
 * @param connected 是否已连接
 * @param host 服务器地址
 * @param port 服务器端口
 */
void ui_websocket_update_status(bool connected, const char *host, uint16_t port);

#ifdef __cplusplus
}
#endif

#endif /* UI_WEBSOCKET_H */

