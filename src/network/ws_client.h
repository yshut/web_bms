#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

// WebSocket客户端回调
typedef void (*ws_message_callback_t)(const char *message, int length);
typedef void (*ws_connection_callback_t)(bool connected);

// 初始化WebSocket客户端
int ws_client_init(void);

// 连接到WebSocket服务器
int ws_client_connect(const char *host, uint16_t port, const char *path);

// 断开连接
void ws_client_disconnect(void);

// 发送消息
int ws_client_send(const char *message, int length);

// 设置回调
void ws_client_set_message_callback(ws_message_callback_t callback);
void ws_client_set_connection_callback(ws_connection_callback_t callback);

// 获取连接状态
bool ws_client_is_connected(void);

#endif // WS_CLIENT_H

