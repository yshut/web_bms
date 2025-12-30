#ifndef UI_UDS_H
#define UI_UDS_H

#include "lvgl.h"
#include <stdbool.h>

// UDS诊断UI结构
typedef struct {
    lv_obj_t *container;
    lv_obj_t *btn_back;
    lv_obj_t *btn_start;
    lv_obj_t *btn_stop;
    lv_obj_t *file_path_input;
    lv_obj_t *progress_bar;
    lv_obj_t *status_label;
    lv_obj_t *log_textarea;
} ui_uds_t;

// 创建UDS诊断页面
lv_obj_t* ui_uds_create(lv_obj_t *parent);

// 更新UDS状态
void ui_uds_update_status(const char *status);
void ui_uds_update_progress(int percent);
void ui_uds_add_log(const char *log);

#endif // UI_UDS_H

