/**
 * @file can_recorder.h
 * @brief CAN报文录制模块 - ASC格式
 * 
 * 功能：
 * - 同时录制CAN0和CAN1报文
 * - 使用ASC（AECII）格式
 * - 支持文件自动分段
 * - 后台异步写入
 */

#ifndef CAN_RECORDER_H
#define CAN_RECORDER_H

#include <stdbool.h>
#include <stdint.h>
#include "can_handler.h"

/**
 * @brief 录制器配置
 */
typedef struct {
    bool record_can0;          /* 是否录制CAN0 */
    bool record_can1;          /* 是否录制CAN1 */
    char record_dir[256];      /* 录制文件目录 */
    uint64_t max_file_size;    /* 单个文件最大大小（字节），默认40MB */
    int flush_interval_ms;     /* 缓冲刷新间隔（毫秒），默认200ms */
} can_recorder_config_t;

/**
 * @brief 录制器统计信息
 */
typedef struct {
    uint64_t total_frames;     /* 总录制帧数 */
    uint64_t can0_frames;      /* CAN0录制帧数 */
    uint64_t can1_frames;      /* CAN1录制帧数 */
    uint64_t bytes_written;    /* 已写入字节数 */
    uint32_t file_count;       /* 文件分段数 */
    char current_file[256];    /* 当前文件路径 */
} can_recorder_stats_t;

/**
 * @brief 初始化录制器
 * @param config 配置参数，可为NULL使用默认配置
 * @return 0成功，-1失败
 */
int can_recorder_init(const can_recorder_config_t *config);

/**
 * @brief 清理录制器
 */
void can_recorder_deinit(void);

/**
 * @brief 开始录制
 * @return 0成功，-1失败
 */
int can_recorder_start(void);

/**
 * @brief 停止录制
 */
void can_recorder_stop(void);

/**
 * @brief 检查是否正在录制
 * @return true正在录制，false已停止
 */
bool can_recorder_is_recording(void);

/**
 * @brief 获取当前录制文件名
 * @return 文件名（相对路径），如果未录制则返回NULL
 */
const char* can_recorder_get_filename(void);

/**
 * @brief 获取录制统计信息
 * @param stats 统计信息输出
 */
void can_recorder_get_stats(can_recorder_stats_t *stats);

/**
 * @brief CAN帧回调（由can_handler调用）
 * @param frame CAN帧
 * @param user_data 用户数据（未使用）
 */
void can_recorder_frame_callback(int channel, const can_frame_t *frame, void *user_data);

/**
 * @brief 设置录制通道
 * @param record_can0 是否录制CAN0
 * @param record_can1 是否录制CAN1
 */
void can_recorder_set_channels(bool record_can0, bool record_can1);

#endif /* CAN_RECORDER_H */

