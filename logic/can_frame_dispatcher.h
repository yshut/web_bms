/**
 * @file can_frame_dispatcher.h
 * @brief CAN帧分发器 - 将CAN帧分发到多个目标
 */

#ifndef CAN_FRAME_DISPATCHER_H
#define CAN_FRAME_DISPATCHER_H

#include "can_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 全局CAN帧分发器回调
 * @param channel CAN通道（0或1）
 * @param frame CAN帧数据
 * @param user_data 用户数据（未使用）
 */
void can_frame_dispatcher_callback(int channel, const can_frame_t *frame, void *user_data);

/**
 * @brief 注册UI回调（由UI页面调用）
 * @param callback 回调函数（NULL表示注销）
 * @param user_data 用户数据
 */
void can_frame_dispatcher_register_ui_callback(can_frame_callback_t callback, void *user_data);

/**
 * @brief 注册 CAN-MQTT 引擎回调
 * @param callback 回调函数（NULL表示注销）
 * @param user_data 用户数据
 */
void can_frame_dispatcher_register_engine_callback(can_frame_callback_t callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif // CAN_FRAME_DISPATCHER_H

