#ifndef CAN_WORKER_H
#define CAN_WORKER_H

#include <stdint.h>
#include <stdbool.h>

// CAN工作器回调
typedef void (*can_message_callback_t)(const char *message);

// 初始化CAN工作器
int can_worker_init(void);

// 启动CAN工作器
int can_worker_start(bool can1_enabled, bool can2_enabled, uint32_t bitrate1, uint32_t bitrate2);

// 停止CAN工作器
void can_worker_stop(void);

// 扫描CAN接口
void can_worker_scan(void);

// 发送CAN帧
int can_worker_send_frame(const char *frame_str);

// 设置消息回调
void can_worker_set_callback(can_message_callback_t callback);

// 获取CAN状态
bool can_worker_is_running(void);

#endif // CAN_WORKER_H

