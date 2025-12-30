/**
 * @file ui_uds.h
 * @brief UDS诊断界面头文件
 */

#ifndef UI_UDS_H
#define UI_UDS_H

#include "lvgl.h"

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *can_status_label;     // CAN状态标签
    lv_obj_t *total_progress_label; // 总体进度文字
    lv_obj_t *total_progress_bar;   // 总体进度条
    lv_obj_t *segment_progress_label; // 段进度文字
    lv_obj_t *segment_progress_bar; // 段进度条
    lv_obj_t *log_list;             // 日志列表
    // 配置区
    lv_obj_t *channel_dd;           // 通道选择 CAN1/CAN2
    lv_obj_t *bitrate_dd;           // 波特率选择
    lv_obj_t *tx_id_input;          // 发送ID
    lv_obj_t *rx_id_input;          // 接收ID
    lv_obj_t *blk_size_input;       // 块大小
    // 文件区
    lv_obj_t *s19_path_input;       // S19路径
    lv_obj_t *browse_btn;           // 浏览按钮
    // 控制区
    lv_obj_t *start_btn;            // 开始按钮
    lv_obj_t *stop_btn;             // 停止按钮
    // 软键盘
    lv_obj_t *kb_cont;              // 软键盘容器
    lv_obj_t *kb;                   // 软键盘
    lv_obj_t *kb_preview;           // 软键盘预览
    lv_obj_t *kb_hide_btn;          // 软键盘隐藏按钮
} ui_uds_t;

ui_uds_t* ui_uds_create(void);
void ui_uds_destroy(ui_uds_t *uds);

#endif /* UI_UDS_H */
