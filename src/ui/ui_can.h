#ifndef UI_CAN_H
#define UI_CAN_H

#include "lvgl.h"
#include <stdbool.h>

// CAN监控UI结构
typedef struct {
    lv_obj_t *container;
    lv_obj_t *btn_back;
    lv_obj_t *btn_scan;
    lv_obj_t *btn_start;
    lv_obj_t *btn_stop;
    lv_obj_t *btn_clear;
    lv_obj_t *btn_send;
    
    // CAN接口选择
    lv_obj_t *can1_checkbox;
    lv_obj_t *can2_checkbox;
    
    // 比特率输入
    lv_obj_t *bitrate_can1_input;
    lv_obj_t *bitrate_can2_input;
    
    // CAN消息输入
    lv_obj_t *can_msg_input;
    
    // 消息显示区域
    lv_obj_t *msg_list;
    
    // 转发开关
    lv_obj_t *forward_switch;
    
    // 状态标签
    lv_obj_t *status_label;
} ui_can_t;

// 创建CAN监控页面
lv_obj_t* ui_can_create(lv_obj_t *parent);

// 添加CAN消息到显示列表
void ui_can_add_message(const char *message);

// 清除消息列表
void ui_can_clear_messages(void);

// 更新状态
void ui_can_update_status(const char *status);

#endif // UI_CAN_H

