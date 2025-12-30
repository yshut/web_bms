/**
 * @file ui_file_manager.h
 * @brief 文件管理界面头文件
 */

#ifndef UI_FILE_MANAGER_H
#define UI_FILE_MANAGER_H

#include "lvgl.h"
#include <stdbool.h>

typedef struct {
    lv_obj_t *screen;           // 主屏幕
    lv_obj_t *path_label;       // 路径标签
    lv_obj_t *file_list;        // 文件列表
    lv_obj_t *status_label;     // 状态标签
    lv_obj_t *refresh_btn;      // 刷新按钮
    char current_path[512];     // 当前路径
    bool transfer_enabled;      // 传输服务状态
    char *selected_item_name;   // 当前选中项名称
    lv_obj_t *selected_item_obj; // 当前选中项对象
    
    // 双击检测
    uint32_t last_click_time;   // 上次点击时间（毫秒）
    char *last_click_item;      // 上次点击的项名称
    
    // 输入对话框相关
    lv_obj_t *input_dialog;     // 输入对话框
    lv_obj_t *input_textarea;   // 输入框
    void (*input_callback)(const char *);  // 输入回调函数
} ui_file_manager_t;

ui_file_manager_t* ui_file_manager_create(void);
void ui_file_manager_destroy(ui_file_manager_t *fm);

#endif /* UI_FILE_MANAGER_H */
