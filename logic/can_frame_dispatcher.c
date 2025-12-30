/**
 * @file can_frame_dispatcher.c
 * @brief CAN帧分发器 - 将CAN帧分发到多个目标（录制器、WebSocket、UI、缓冲区）
 */

#include "can_frame_dispatcher.h"
#include "can_recorder.h"
#include "can_frame_buffer.h"
#include "ws_client.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>

/* UI回调（由UI页面注册） */
static can_frame_callback_t g_ui_callback = NULL;
static void *g_ui_callback_user_data = NULL;

/* 调试计数器 */
static int g_frame_count = 0;

/**
 * @brief 全局CAN帧分发器回调
 */
void can_frame_dispatcher_callback(int channel, const can_frame_t *frame, void *user_data)
{
    (void)user_data;
    
    if (!frame) {
        return;
    }
    
    /* 调试日志：每100帧输出一次 */
    g_frame_count++;
    if (g_frame_count % 100 == 1) {
        log_info("CAN帧分发器：已处理 %d 帧 (通道:%d, ID:0x%03X)", 
                 g_frame_count, channel, frame->can_id & 0x1FFFFFFF);
    }
    
    /* 0. 添加到帧缓冲区（用于WebSocket查询）*/
    can_frame_buffer_add(channel, frame);
    
    /* 1. 分发到录制器 */
    can_recorder_frame_callback(channel, frame, NULL);
    
    /* 2. 分发到WebSocket */
    // 获取当前时间戳
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm_info = localtime(&tv.tv_sec);
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d.%03ld",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             tv.tv_usec / 1000);
    
    // 格式化CAN帧为服务器期望的格式
    char frame_text[160];
    char data_str[32] = {0};
    char *p = data_str;
    for (int i = 0; i < frame->can_dlc && i < 8; i++) {
        p += snprintf(p, sizeof(data_str) - (p - data_str), "%02X ", frame->data[i]);
    }
    // 移除末尾空格
    if (p > data_str && *(p-1) == ' ') {
        *(p-1) = '\0';
    }
    
    // 判断是否为扩展帧
    bool is_extended = (frame->can_id & 0x80000000) != 0;
    uint32_t id = frame->can_id & 0x1FFFFFFF;
    
    // 通道名称
    const char *channel_name = (channel == 1) ? "CAN2" : "CAN1";
    
    // 格式：[时间] 通道 ID DLC 数据
    if (is_extended) {
        snprintf(frame_text, sizeof(frame_text),
                 "[%s] %s ID:0x%08X DLC:%d 数据:%s",
                 time_str, channel_name, id, frame->can_dlc, data_str);
    } else {
        snprintf(frame_text, sizeof(frame_text),
                 "[%s] %s ID:0x%03X DLC:%d 数据:%s",
                 time_str, channel_name, id, frame->can_dlc, data_str);
    }
    
    ws_client_report_can_frame(channel, frame_text);
    
    /* 3. 分发到UI回调（如果已注册） */
    if (g_ui_callback) {
        g_ui_callback(channel, frame, g_ui_callback_user_data);
    }
}

/**
 * @brief 注册UI回调
 */
void can_frame_dispatcher_register_ui_callback(can_frame_callback_t callback, void *user_data)
{
    g_ui_callback = callback;
    g_ui_callback_user_data = user_data;
    
    if (callback) {
        log_info("CAN帧分发器: UI回调已注册");
    } else {
        log_info("CAN帧分发器: UI回调已注销");
    }
}

