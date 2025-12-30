/**
 * @file ws_client.h
 * @brief WebSocket远程控制客户端
 * @description 基于libwebsockets实现，用于远程监控和控制设备
 */

#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WebSocket连接状态
 */
typedef enum {
    WS_STATE_DISCONNECTED = 0,  /* 未连接 */
    WS_STATE_CONNECTING,        /* 连接中 */
    WS_STATE_CONNECTED,         /* 已连接 */
    WS_STATE_AUTHENTICATED      /* 已认证 */
} ws_state_t;

/**
 * @brief WebSocket客户端配置
 */
typedef struct {
    char host[128];             /* 服务器地址 */
    uint16_t port;              /* 服务器端口 */
    char path[128];             /* WebSocket路径（如"/ws"） */
    bool use_ssl;               /* 是否使用SSL */
    int reconnect_interval_ms;  /* 重连间隔（毫秒） */
    int keepalive_interval_s;   /* 心跳间隔（秒） */
} ws_config_t;

/**
 * @brief WebSocket连接状态回调
 * @param connected 是否已连接
 * @param host 服务器地址
 * @param port 服务器端口
 * @param user_data 用户数据
 */
typedef void (*ws_state_callback_t)(bool connected, const char *host, uint16_t port, void *user_data);

/**
 * @brief 初始化WebSocket客户端
 * @param config 配置参数
 * @return 0成功，-1失败
 */
int ws_client_init(const ws_config_t *config);

/**
 * @brief 反初始化WebSocket客户端
 */
void ws_client_deinit(void);

/**
 * @brief 启动WebSocket连接
 * @return 0成功，-1失败
 */
int ws_client_start(void);

/**
 * @brief 停止WebSocket连接
 */
void ws_client_stop(void);

/**
 * @brief 获取连接状态
 * @return WebSocket连接状态
 */
ws_state_t ws_client_get_state(void);

/**
 * @brief 检查是否已连接
 * @return true=已连接, false=未连接
 */
bool ws_client_is_connected(void);

/**
 * @brief 注册连接状态回调
 * @param callback 回调函数
 * @param user_data 用户数据
 */
void ws_client_register_state_callback(ws_state_callback_t callback, void *user_data);

/**
 * @brief 发送JSON消息
 * @param json_str JSON字符串
 * @return 0成功，-1失败
 */
int ws_client_send_json(const char *json_str);

/**
 * @brief 发送二进制消息
 * @param data 数据指针
 * @param len 数据长度
 * @return 0成功，-1失败
 */
int ws_client_send_binary(const uint8_t *data, size_t len);

/**
 * @brief 上报CAN帧（会自动聚合批量发送）
 * @param channel CAN通道（0或1）
 * @param frame_text CAN帧文本描述
 */
void ws_client_report_can_frame(int channel, const char *frame_text);

/**
 * @brief 上报事件
 * @param event_type 事件类型
 * @param payload JSON格式的事件数据
 */
void ws_client_publish_event(const char *event_type, const char *payload);

/**
 * @brief 获取设备ID
 * @return 设备ID字符串
 */
const char* ws_client_get_device_id(void);

/**
 * @brief 获取服务器地址
 * @param host 输出：服务器地址
 * @param host_size 地址缓冲区大小
 * @param port 输出：服务器端口
 * @return 0成功，-1失败
 */
int ws_client_get_server_info(char *host, size_t host_size, uint16_t *port);

#ifdef __cplusplus
}
#endif

#endif /* WS_CLIENT_H */

