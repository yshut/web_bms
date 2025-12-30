/**
 * @file ws_command_handler.h
 * @brief WebSocket命令处理器
 */

#ifndef WS_COMMAND_HANDLER_H
#define WS_COMMAND_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化命令处理器
 * @return 0成功，-1失败
 */
int ws_command_handler_init(void);

/**
 * @brief 反初始化命令处理器
 */
void ws_command_handler_deinit(void);

/**
 * @brief 处理接收到的JSON命令
 * @param json_str JSON字符串
 * @return 0成功，-1失败
 */
int ws_command_handler_process(const char *json_str);

/**
 * @brief 发送OK响应
 * @param request_id 请求ID（可为NULL）
 * @param extra_data 额外数据（JSON格式，可为NULL）
 */
void ws_command_send_ok(const char *request_id, const char *extra_data);

/**
 * @brief 发送ERROR响应
 * @param request_id 请求ID（可为NULL）
 * @param error_msg 错误消息
 */
void ws_command_send_error(const char *request_id, const char *error_msg);

#ifdef __cplusplus
}
#endif

#endif /* WS_COMMAND_HANDLER_H */

