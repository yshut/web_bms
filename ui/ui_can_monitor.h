/**
 * @file ui_can_monitor.h
 * @brief CAN监控界面头文件
 */

#ifndef UI_CAN_MONITOR_H
#define UI_CAN_MONITOR_H

#include "lvgl.h"

/* CAN监控界面结构 */
typedef struct {
    lv_obj_t *screen;           /* 屏幕对象 */
    lv_obj_t *list;             /* CAN消息列表 */
    lv_obj_t *stats_label;      /* 统计标签 */
    lv_obj_t *status_label;     /* 状态标签 */
    lv_obj_t *start_btn;        /* 启动按钮 */
    lv_obj_t *stop_btn;         /* 停止按钮 */
    lv_obj_t *clear_btn;        /* 清除按钮 */
    lv_obj_t *id_input;         /* CAN ID输入框 */
    lv_obj_t *data_input;       /* CAN数据输入框 */
    lv_timer_t *update_timer;   /* 更新定时器 */
    lv_obj_t *channel_dd;       /* 通道选择 can0/can1 */
    lv_obj_t *bitrate0_dd;      /* CAN0波特率选择 */
    lv_obj_t *bitrate1_dd;      /* CAN1波特率选择 */
    lv_obj_t *tx_channel_dd;    /* 发送通道选择 */
    lv_obj_t *kb_cont;          /* 软键盘容器 */
    lv_obj_t *kb;               /* 软键盘对象 */
    lv_obj_t *kb_preview;       /* 软键盘上方预览输入框 */
    lv_obj_t *kb_hide_btn;      /* 软键盘隐藏按钮 */
    
    /* 录制相关控件 */
    lv_obj_t *record_btn;       /* 录制开始/停止按钮 */
    lv_obj_t *record_status_label; /* 录制状态标签 */
    lv_obj_t *record_can0_cb;   /* 录制CAN0复选框 */
    lv_obj_t *record_can1_cb;   /* 录制CAN1复选框 */
} ui_can_monitor_t;

/**
 * @brief 创建CAN监控界面
 * @return CAN监控界面指针
 */
ui_can_monitor_t* ui_can_monitor_create(void);

/**
 * @brief 销毁CAN监控界面
 * @param can_mon CAN监控界面指针
 */
void ui_can_monitor_destroy(ui_can_monitor_t *can_mon);

/**
 * @brief 清除CAN消息列表（异步调用，线程安全）
 */
void ui_can_monitor_clear_messages_async(void);

#endif /* UI_CAN_MONITOR_H */
