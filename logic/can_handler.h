/**
 * @file can_handler.h
 * @brief CAN总线处理头文件
 */

#ifndef CAN_HANDLER_H
#define CAN_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

/* CAN帧结构 */
typedef struct {
    uint32_t can_id;        /* CAN标识符 */
    uint8_t can_dlc;        /* 数据长度 */
    uint8_t data[8];        /* 数据 */
    bool is_extended;       /* 是否为扩展帧 */
    uint8_t channel;        /* 源通道: 0=can0(CAN1), 1=can1(CAN2) */
    uint64_t timestamp_us;  /* 时间戳（微秒） */
} can_frame_t;

/* CAN统计信息 */
typedef struct {
    uint32_t rx_count;      /* 接收计数 */
    uint32_t tx_count;      /* 发送计数 */
    uint32_t error_count;   /* 错误计数 */
    uint32_t drop_count;    /* 丢弃计数 */
} can_stats_t;

/* CAN帧回调函数类型 (channel: 0=can0, 1=can1) */
typedef void (*can_frame_callback_t)(int channel, const can_frame_t *frame, void *user_data);

/**
 * @brief 初始化CAN处理器
 * @param interface CAN接口名（如"can0"）
 * @param bitrate 波特率
 * @return 0成功，-1失败
 */
int can_handler_init(const char *interface, uint32_t bitrate);

/**
 * @brief 同时初始化can0与can1，双通道接收
 */
int can_handler_init_dual(uint32_t bitrate0, uint32_t bitrate1);

/**
 * @brief 清理CAN处理器
 */
void can_handler_deinit(void);

/**
 * @brief 启动CAN接收
 * @return 0成功，-1失败
 */
int can_handler_start(void);

/**
 * @brief 停止CAN接收
 */
void can_handler_stop(void);

/**
 * @brief 发送CAN帧
 * @param frame CAN帧
 * @return 0成功，-1失败
 */
int can_handler_send(const can_frame_t *frame);

/**
 * @brief 在指定接口上发送CAN帧（无需切换当前接收通道）
 */
int can_handler_send_on(const char *interface, const can_frame_t *frame);

/**
 * @brief 注册帧接收回调
 * @param callback 回调函数
 * @param user_data 用户数据
 */
void can_handler_register_callback(can_frame_callback_t callback, void *user_data);

/**
 * @brief 获取统计信息
 * @param stats 统计信息结构体指针
 */
void can_handler_get_stats(can_stats_t *stats);

/**
 * @brief 检查CAN是否正在运行
 * @return true运行中，false已停止
 */
bool can_handler_is_running(void);

/**
 * @brief 配置指定CAN接口波特率（不创建socket）
 * @param interface 接口名（can0/can1）
 * @param bitrate 波特率
 * @return 0成功，-1失败
 */
int can_handler_configure(const char *interface, uint32_t bitrate);

/**
 * @brief 获取当前CAN0波特率
 * @return 当前波特率（bps），如果未初始化返回0
 */
uint32_t can_handler_get_bitrate(void);

/**
 * @brief 获取双通道模式下的波特率
 * @param bitrate0 输出CAN0波特率的指针
 * @param bitrate1 输出CAN1波特率的指针
 */
void can_handler_get_bitrate_dual(uint32_t *bitrate0, uint32_t *bitrate1);

#endif /* CAN_HANDLER_H */
