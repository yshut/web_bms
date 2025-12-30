#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

// TCP客户端回调
typedef void (*tcp_message_callback_t)(const char *message, int length);
typedef void (*tcp_connection_callback_t)(bool connected);

// 初始化TCP客户端
int tcp_client_init(void);

// 连接到服务器
int tcp_client_connect(const char *host, uint16_t port);

// 断开连接
void tcp_client_disconnect(void);

// 发送消息
int tcp_client_send(const char *message, int length);

// 设置回调
void tcp_client_set_message_callback(tcp_message_callback_t callback);
void tcp_client_set_connection_callback(tcp_connection_callback_t callback);

// 获取连接状态
bool tcp_client_is_connected(void);

#endif // TCP_CLIENT_H

