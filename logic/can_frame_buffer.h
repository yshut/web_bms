/**
 * @file can_frame_buffer.h
 * @brief CAN帧环形缓冲区 - 用于存储最近接收的CAN帧供WebSocket查询
 */

#ifndef CAN_FRAME_BUFFER_H
#define CAN_FRAME_BUFFER_H

#include "can_handler.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化CAN帧缓冲区
 * @param max_frames 最大存储帧数
 * @return 0成功，-1失败
 */
int can_frame_buffer_init(int max_frames);

/**
 * @brief 清理CAN帧缓冲区
 */
void can_frame_buffer_deinit(void);

/**
 * @brief 添加一帧到缓冲区
 * @param channel CAN通道（0=can0, 1=can1）
 * @param frame CAN帧
 */
void can_frame_buffer_add(int channel, const can_frame_t *frame);

/**
 * @brief 清空缓冲区
 */
void can_frame_buffer_clear(void);

/**
 * @brief 获取最近的N帧，转换为JSON数组
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @param limit 最多获取的帧数
 * @return 实际获取的帧数，-1表示失败
 * 
 * JSON格式: [{"id":123,"data":[1,2,3,4,5,6,7,8],"timestamp":1234567890.123,"iface":"can0"}]
 */
int can_frame_buffer_get_json(char *buffer, int buffer_size, int limit);

/**
 * @brief 获取缓冲区中的帧数量
 * @return 帧数量
 */
int can_frame_buffer_get_count(void);

#ifdef __cplusplus
}
#endif

#endif // CAN_FRAME_BUFFER_H

