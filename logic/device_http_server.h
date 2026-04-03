/**
 * @file device_http_server.h
 * @brief 板端轻量 HTTP 服务器
 *
 * 提供一个运行在 T113-S3 上的 Web 配置界面，用于管理 CAN→MQTT 规则。
 * 默认监听 0.0.0.0:8080，可通过 ws_config.txt 中 http_port=8080 更改。
 *
 * 访问方式：http://<device_ip>:8080/
 */

#ifndef DEVICE_HTTP_SERVER_H
#define DEVICE_HTTP_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 HTTP 服务器（在独立线程中运行）
 * @param port 监听端口，0 表示使用默认 8080
 * @return 0 成功，-1 失败
 */
int device_http_server_start(uint16_t port);

/**
 * @brief 停止 HTTP 服务器
 */
void device_http_server_stop(void);

/**
 * @brief 服务器是否正在运行
 */
bool device_http_server_is_running(void);

/**
 * @brief 获取服务器监听端口
 */
uint16_t device_http_server_get_port(void);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_HTTP_SERVER_H */
