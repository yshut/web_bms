#ifndef UI_FILE_H
#define UI_FILE_H

#include "lvgl.h"
#include <stdbool.h>

// 文件管理器UI结构
typedef struct {
    lv_obj_t *container;
    lv_obj_t *btn_back;
    lv_obj_t *btn_refresh;
    lv_obj_t *btn_upload;
    lv_obj_t *btn_download;
    lv_obj_t *btn_delete;
    lv_obj_t *path_label;
    lv_obj_t *file_list;
    lv_obj_t *status_label;
} ui_file_t;

// 创建文件管理器页面
lv_obj_t* ui_file_create(lv_obj_t *parent);

// 更新文件列表
void ui_file_update_list(const char **files, int count);

// 更新路径
void ui_file_update_path(const char *path);

// 更新状态
void ui_file_update_status(const char *status);

#endif // UI_FILE_H

