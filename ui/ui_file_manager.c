/**
 * @file ui_file_manager.c
 * @brief 文件管理界面实现（完整功能版）
 */

#include "ui_file_manager.h"
#include "ui_common.h"
#include "../logic/app_manager.h"
#include "../utils/logger.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define BASE_PATH "/mnt"
#define MAX_PATH_LEN 512
#define MAX_NAME_LEN 128

// 全局文件管理器指针（用于输入对话框回调）
static ui_file_manager_t *g_file_manager = NULL;

// 前向声明
static void show_input_dialog(ui_file_manager_t *fm, const char *title, const char *placeholder, void (*callback)(const char *));
static void close_input_dialog(ui_file_manager_t *fm);
static void refresh_file_list(ui_file_manager_t *fm);
static void file_item_event_cb(lv_event_t *e);

// ==================== 工具函数 ====================

// 递归删除目录
static int remove_directory(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) {
        return remove(path);
    }
    
    struct dirent *entry;
    char filepath[MAX_PATH_LEN];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        
        if (entry->d_type == DT_DIR) {
            remove_directory(filepath);
        } else {
            remove(filepath);
        }
    }
    
    closedir(dir);
    return rmdir(path);
}

// ==================== 输入对话框 ====================

static void input_dialog_ok_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    
    ui_file_manager_t *fm = g_file_manager;
    if (!fm || !fm->input_dialog) return;
    
    // 获取输入的文本
    const char *text = lv_textarea_get_text(fm->input_textarea);
    if (text && strlen(text) > 0) {
        // 调用回调函数
        if (fm->input_callback) {
            fm->input_callback(text);
        }
    }
    
    close_input_dialog(fm);
}

static void input_dialog_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    
    ui_file_manager_t *fm = g_file_manager;
    if (fm) {
        close_input_dialog(fm);
    }
}

static void show_input_dialog(ui_file_manager_t *fm, const char *title, const char *placeholder, void (*callback)(const char *))
{
    if (!fm || !fm->screen) return;
    
    log_info("显示输入对话框: %s", title);
    
    // 如果已有对话框，先关闭
    if (fm->input_dialog) {
        close_input_dialog(fm);
    }
    
    fm->input_callback = callback;
    
    // 创建全屏对话框容器（在文件管理器屏幕上创建）
    fm->input_dialog = lv_obj_create(fm->screen);
    lv_obj_set_size(fm->input_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(fm->input_dialog, lv_color_hex(0xF0F0F0), 0);
    lv_obj_clear_flag(fm->input_dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // 确保对话框在最上层
    lv_obj_move_foreground(fm->input_dialog);
    
    // 标题
    lv_obj_t *title_label = lv_label_create(fm->input_dialog);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, ui_common_get_font(), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);
    
    // 输入框
    fm->input_textarea = lv_textarea_create(fm->input_dialog);
    lv_obj_set_size(fm->input_textarea, LV_PCT(90), 50);
    lv_obj_align(fm->input_textarea, LV_ALIGN_TOP_MID, 0, 60);
    lv_textarea_set_one_line(fm->input_textarea, true);
    lv_textarea_set_placeholder_text(fm->input_textarea, placeholder);
    lv_obj_set_style_text_font(fm->input_textarea, ui_common_get_font(), 0);
    
    // 创建 LVGL 键盘
    lv_obj_t *kb = lv_keyboard_create(fm->input_dialog);
    lv_obj_set_size(kb, LV_PCT(100), LV_PCT(50));
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, fm->input_textarea);
    
    // 按钮容器（在键盘上方）
    lv_obj_t *btn_cont = lv_obj_create(fm->input_dialog);
    lv_obj_set_size(btn_cont, LV_PCT(90), 60);
    lv_obj_align(btn_cont, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // 确定按钮
    lv_obj_t *ok_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(ok_btn, 200, 45);
    lv_obj_add_event_cb(ok_btn, input_dialog_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x4CAF50), 0);
    lv_obj_t *ok_label = lv_label_create(ok_btn);
    lv_label_set_text(ok_label, "确定");
    lv_obj_set_style_text_font(ok_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(ok_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(ok_label);
    
    // 取消按钮
    lv_obj_t *cancel_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(cancel_btn, 200, 45);
    lv_obj_add_event_cb(cancel_btn, input_dialog_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0xF44336), 0);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "取消");
    lv_obj_set_style_text_font(cancel_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(cancel_label);
    
    log_info("输入对话框创建完成");
}

static void close_input_dialog(ui_file_manager_t *fm)
{
    if (fm && fm->input_dialog) {
        lv_obj_del(fm->input_dialog);
        fm->input_dialog = NULL;
        fm->input_textarea = NULL;
        fm->input_callback = NULL;
    }
}

// ==================== 文件操作回调 ====================

// 新建文件夹回调
static void create_folder_callback(const char *name)
{
    ui_file_manager_t *fm = g_file_manager;
    if (!fm || !name || strlen(name) == 0) return;
    
    char new_path[MAX_PATH_LEN];
    snprintf(new_path, sizeof(new_path), "%s/%s", fm->current_path, name);
    
    if (mkdir(new_path, 0755) == 0) {
        log_info("创建文件夹成功: %s", new_path);
        if (fm->status_label) {
            lv_label_set_text(fm->status_label, "状态: 文件夹创建成功");
        }
        refresh_file_list(fm);
    } else {
        log_error("创建文件夹失败: %s (错误: %s)", new_path, strerror(errno));
        if (fm->status_label) {
            char msg[128];
            snprintf(msg, sizeof(msg), "状态: 创建失败 - %s", strerror(errno));
            lv_label_set_text(fm->status_label, msg);
        }
    }
}

// 新建文件回调
static void create_file_callback(const char *name)
{
    ui_file_manager_t *fm = g_file_manager;
    if (!fm || !name || strlen(name) == 0) return;
    
    char new_path[MAX_PATH_LEN];
    snprintf(new_path, sizeof(new_path), "%s/%s", fm->current_path, name);
    
    FILE *fp = fopen(new_path, "w");
    if (fp) {
        fclose(fp);
        log_info("创建文件成功: %s", new_path);
        if (fm->status_label) {
            lv_label_set_text(fm->status_label, "状态: 文件创建成功");
        }
        refresh_file_list(fm);
    } else {
        log_error("创建文件失败: %s (错误: %s)", new_path, strerror(errno));
        if (fm->status_label) {
            char msg[128];
            snprintf(msg, sizeof(msg), "状态: 创建失败 - %s", strerror(errno));
            lv_label_set_text(fm->status_label, msg);
        }
    }
}

// 重命名回调
static void rename_callback(const char *new_name)
{
    ui_file_manager_t *fm = g_file_manager;
    if (!fm || !new_name || strlen(new_name) == 0 || !fm->selected_item_name) return;
    
    char old_path[MAX_PATH_LEN];
    char new_path[MAX_PATH_LEN];
    snprintf(old_path, sizeof(old_path), "%s/%s", fm->current_path, fm->selected_item_name);
    snprintf(new_path, sizeof(new_path), "%s/%s", fm->current_path, new_name);
    
    if (rename(old_path, new_path) == 0) {
        log_info("重命名成功: %s -> %s", old_path, new_path);
        if (fm->status_label) {
            lv_label_set_text(fm->status_label, "状态: 重命名成功");
        }
        refresh_file_list(fm);
    } else {
        log_error("重命名失败: %s (错误: %s)", old_path, strerror(errno));
        if (fm->status_label) {
            char msg[128];
            snprintf(msg, sizeof(msg), "状态: 重命名失败 - %s", strerror(errno));
            lv_label_set_text(fm->status_label, msg);
        }
    }
    
    // 清除选中项
    if (fm->selected_item_name) {
        free(fm->selected_item_name);
        fm->selected_item_name = NULL;
    }
}

// ==================== 按钮事件回调 ====================

// 返回主页
static void back_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        log_info("从文件管理返回主页");
        app_manager_switch_to_page(APP_PAGE_HOME);
    }
}

// 上一级目录
static void up_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ui_file_manager_t *fm = (ui_file_manager_t *)lv_event_get_user_data(e);
        if (!fm) return;
        
        // 检查是否已经在根目录
        if (strcmp(fm->current_path, BASE_PATH) == 0) {
            log_info("已经在根目录");
            if (fm->status_label) {
                lv_label_set_text(fm->status_label, "状态: 已在根目录");
            }
            return;
        }
        
        // 获取父目录
        char *last_slash = strrchr(fm->current_path, '/');
        if (last_slash && last_slash != fm->current_path) {
            *last_slash = '\0';
            if (strlen(fm->current_path) == 0) {
                strcpy(fm->current_path, BASE_PATH);
            }
        } else {
            strcpy(fm->current_path, BASE_PATH);
        }
        
        log_info("切换到目录: %s", fm->current_path);
        refresh_file_list(fm);
    }
}

// 刷新文件列表
static void refresh_file_list(ui_file_manager_t *fm)
{
    if (!fm || !fm->file_list) return;
    
    log_info("刷新文件列表: %s", fm->current_path);
    
    // 清空文件列表
    lv_obj_clean(fm->file_list);
    
    // 清除选中状态（刷新后对象会被删除）
    fm->selected_item_obj = NULL;
        
        // 更新路径标签
        if (fm->path_label) {
        char path_text[MAX_PATH_LEN + 10];
        snprintf(path_text, sizeof(path_text), "路径: %s", fm->current_path);
        lv_label_set_text(fm->path_label, path_text);
    }
    
    // 读取目录内容
    DIR *dir = opendir(fm->current_path);
    if (!dir) {
        log_error("无法打开目录: %s (错误: %s)", fm->current_path, strerror(errno));
        lv_obj_t *error_item = lv_list_add_text(fm->file_list, "[错误] 无法读取目录");
        if (error_item) {
            lv_obj_set_style_text_font(error_item, ui_common_get_font(), 0);
            lv_obj_set_style_text_color(error_item, lv_color_hex(0xFF0000), 0);
        }
        return;
    }
    
    struct dirent *entry;
    int item_count = 0;
    
    // 先添加文件夹
    rewinddir(dir);
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            // 跳过 . 和 ..
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            lv_obj_t *item = lv_list_add_btn(fm->file_list, LV_SYMBOL_DIRECTORY, entry->d_name);
            if (item) {
                lv_obj_set_style_text_font(item, ui_common_get_font(), 0);
                // 为每个列表项添加点击事件
                lv_obj_add_event_cb(item, file_item_event_cb, LV_EVENT_CLICKED, fm);
                item_count++;
            }
        }
    }
    
    // 再添加文件
    rewinddir(dir);
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            lv_obj_t *item = lv_list_add_btn(fm->file_list, LV_SYMBOL_FILE, entry->d_name);
            if (item) {
                lv_obj_set_style_text_font(item, ui_common_get_font(), 0);
                // 为每个列表项添加点击事件
                lv_obj_add_event_cb(item, file_item_event_cb, LV_EVENT_CLICKED, fm);
                item_count++;
            }
        }
    }
    
    closedir(dir);
    
    // 如果目录为空
    if (item_count == 0) {
        lv_obj_t *empty_item = lv_list_add_text(fm->file_list, "目录为空");
        if (empty_item) {
            lv_obj_set_style_text_font(empty_item, ui_common_get_font(), 0);
            lv_obj_set_style_text_color(empty_item, lv_color_hex(0x888888), 0);
        }
    }
    
    if (fm->status_label) {
        char status_text[128];
        snprintf(status_text, sizeof(status_text), "状态: 共 %d 项", item_count);
        lv_label_set_text(fm->status_label, status_text);
    }
    
    log_info("刷新完成，共 %d 项", item_count);
}

static void refresh_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_file_manager_t *fm = (ui_file_manager_t *)lv_event_get_user_data(e);
        refresh_file_list(fm);
    }
}

// 新建文件夹
static void new_folder_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        log_info("新建文件夹");
        ui_file_manager_t *fm = (ui_file_manager_t *)lv_event_get_user_data(e);
        if (!fm) return;
        
        show_input_dialog(fm, "新建文件夹", "请输入文件夹名称", create_folder_callback);
    }
}

// 新建文件
static void new_file_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        log_info("新建文件");
        ui_file_manager_t *fm = (ui_file_manager_t *)lv_event_get_user_data(e);
        if (!fm) return;
        
        show_input_dialog(fm, "新建文件", "请输入文件名", create_file_callback);
    }
}

// 删除选中项
static void delete_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        log_info("删除选中项");
        ui_file_manager_t *fm = (ui_file_manager_t *)lv_event_get_user_data(e);
        if (!fm || !fm->selected_item_name) {
            if (fm && fm->status_label) {
                lv_label_set_text(fm->status_label, "状态: 请先选择要删除的项");
            }
            return;
        }
        
        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", fm->current_path, fm->selected_item_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            int result;
            if (S_ISDIR(st.st_mode)) {
                result = remove_directory(full_path);
            } else {
                result = remove(full_path);
            }
            
            if (result == 0) {
                log_info("删除成功: %s", full_path);
                if (fm->status_label) {
                    lv_label_set_text(fm->status_label, "状态: 删除成功");
                }
                free(fm->selected_item_name);
                fm->selected_item_name = NULL;
                refresh_file_list(fm);
            } else {
                log_error("删除失败: %s (错误: %s)", full_path, strerror(errno));
                if (fm->status_label) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "状态: 删除失败 - %s", strerror(errno));
                    lv_label_set_text(fm->status_label, msg);
                }
            }
        }
    }
}

// 重命名
static void rename_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        log_info("重命名");
        ui_file_manager_t *fm = (ui_file_manager_t *)lv_event_get_user_data(e);
        if (!fm || !fm->selected_item_name) {
            if (fm && fm->status_label) {
                lv_label_set_text(fm->status_label, "状态: 请先选择要重命名的项");
            }
            return;
        }
        
        show_input_dialog(fm, "重命名", fm->selected_item_name, rename_callback);
    }
}

// 文件列表项点击事件 - 双击（500ms内两次点击）打开目录
static void file_item_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    
    lv_obj_t *item = lv_event_get_target(e);
    ui_file_manager_t *fm = (ui_file_manager_t *)lv_event_get_user_data(e);
    
    if (!fm || !item) return;
    
    // 获取按钮中的文本（遍历子对象找到标签）
    const char *text = NULL;
    uint32_t child_cnt = lv_obj_get_child_cnt(item);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(item, i);
        if (child && lv_obj_check_type(child, &lv_label_class)) {
            text = lv_label_get_text(child);
            if (text && strlen(text) > 0 && strcmp(text, LV_SYMBOL_DIRECTORY) != 0 && strcmp(text, LV_SYMBOL_FILE) != 0) {
                break; // 找到文本标签（不是图标）
            }
        }
    }
    
    if (!text) {
        log_warn("无法获取列表项文本");
        return;
    }
    
    // 获取当前时间
    uint32_t now = lv_tick_get();
    uint32_t time_diff = now - fm->last_click_time;
    
    // 检测双击：500ms内点击同一项
    bool is_double_click = false;
    if (fm->last_click_item && strcmp(fm->last_click_item, text) == 0 && time_diff < 500) {
        is_double_click = true;
        log_info("检测到双击: %s", text);
    }
    
    if (is_double_click) {
        // 双击：打开目录或显示文件信息
        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", fm->current_path, text);
        
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            // 是目录，进入
            strncpy(fm->current_path, full_path, MAX_PATH_LEN - 1);
            log_info("进入目录: %s", fm->current_path);
            
            // 清除选中项和双击状态
            if (fm->selected_item_name) {
                free(fm->selected_item_name);
                fm->selected_item_name = NULL;
            }
            if (fm->selected_item_obj) {
                lv_obj_set_style_bg_color(fm->selected_item_obj, lv_color_hex(0xFFFFFF), 0);
                fm->selected_item_obj = NULL;
            }
            if (fm->last_click_item) {
                free(fm->last_click_item);
                fm->last_click_item = NULL;
            }
            fm->last_click_time = 0;
            
            refresh_file_list(fm);
        } else {
            // 是文件，显示信息
            if (fm->status_label) {
                char info[256];
                snprintf(info, sizeof(info), "状态: 文件 %s (大小: %lld 字节)", text, (long long)st.st_size);
                lv_label_set_text(fm->status_label, info);
            }
        }
    } else {
        // 单击：选中项
        log_info("选中: %s", text);
        
        // 清除之前选中项的高亮
        if (fm->selected_item_obj) {
            lv_obj_set_style_bg_color(fm->selected_item_obj, lv_color_hex(0xFFFFFF), 0);
        }
        
        // 保存选中项
        if (fm->selected_item_name) {
            free(fm->selected_item_name);
        }
        fm->selected_item_name = strdup(text);
        fm->selected_item_obj = item;
        
        // 高亮显示选中项
        lv_obj_set_style_bg_color(item, lv_color_hex(0xD0E8FF), 0);
        
        if (fm->status_label) {
            char status_text[MAX_PATH_LEN];
            snprintf(status_text, sizeof(status_text), "状态: 已选中 %s", text);
            lv_label_set_text(fm->status_label, status_text);
        }
    }
    
    // 更新双击检测状态
    if (fm->last_click_item) {
        free(fm->last_click_item);
    }
    fm->last_click_item = strdup(text);
    fm->last_click_time = now;
}

// ==================== UI 创建 ====================

ui_file_manager_t* ui_file_manager_create(void)
{
    log_info("创建文件管理界面...");
    
    ui_file_manager_t *fm = malloc(sizeof(ui_file_manager_t));
    if (!fm) return NULL;
    
    // 初始化
    memset(fm, 0, sizeof(ui_file_manager_t));
    strncpy(fm->current_path, BASE_PATH, MAX_PATH_LEN - 1);
    fm->transfer_enabled = false;
    fm->selected_item_name = NULL;
    fm->selected_item_obj = NULL;
    fm->last_click_time = 0;
    fm->last_click_item = NULL;
    fm->input_dialog = NULL;
    fm->input_textarea = NULL;
    fm->input_callback = NULL;
    
    g_file_manager = fm;
    
    fm->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(fm->screen, lv_color_hex(0xF0F0F0), 0);
    
    /* 顶部标题栏 */
    lv_obj_t *header = lv_obj_create(fm->screen);
    lv_obj_set_size(header, LV_PCT(100), 60);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 80, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " 主页");
    lv_obj_set_style_text_font(back_label, ui_common_get_font(), 0);
    lv_obj_center(back_label);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "文件管理");
    lv_obj_set_style_text_font(title, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(title);
    
    /* 工具栏 */
    lv_obj_t *toolbar = lv_obj_create(fm->screen);
    lv_obj_set_size(toolbar, LV_PCT(95), 60);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 65);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(toolbar, 5, 0);
    lv_obj_set_style_pad_column(toolbar, 5, 0);
    
    // 上一级按钮
    lv_obj_t *up_btn = lv_btn_create(toolbar);
    lv_obj_set_size(up_btn, 70, 40);
    lv_obj_add_event_cb(up_btn, up_btn_event_cb, LV_EVENT_CLICKED, fm);
    lv_obj_t *up_label = lv_label_create(up_btn);
    lv_label_set_text(up_label, LV_SYMBOL_UP " 上级");
    lv_obj_set_style_text_font(up_label, ui_common_get_font(), 0);
    lv_obj_center(up_label);
    
    // 刷新按钮
    fm->refresh_btn = lv_btn_create(toolbar);
    lv_obj_set_size(fm->refresh_btn, 70, 40);
    lv_obj_add_event_cb(fm->refresh_btn, refresh_btn_event_cb, LV_EVENT_CLICKED, fm);
    lv_obj_t *refresh_label = lv_label_create(fm->refresh_btn);
    lv_label_set_text(refresh_label, LV_SYMBOL_REFRESH " 刷新");
    lv_obj_set_style_text_font(refresh_label, ui_common_get_font(), 0);
    lv_obj_center(refresh_label);
    
    // 新建文件夹按钮
    lv_obj_t *new_folder_btn = lv_btn_create(toolbar);
    lv_obj_set_size(new_folder_btn, 70, 40);
    lv_obj_add_event_cb(new_folder_btn, new_folder_btn_event_cb, LV_EVENT_CLICKED, fm);
    lv_obj_t *new_folder_label = lv_label_create(new_folder_btn);
    lv_label_set_text(new_folder_label, LV_SYMBOL_DIRECTORY " 新建");
    lv_obj_set_style_text_font(new_folder_label, ui_common_get_font(), 0);
    lv_obj_center(new_folder_label);
    
    // 新建文件按钮
    lv_obj_t *new_file_btn = lv_btn_create(toolbar);
    lv_obj_set_size(new_file_btn, 70, 40);
    lv_obj_add_event_cb(new_file_btn, new_file_btn_event_cb, LV_EVENT_CLICKED, fm);
    lv_obj_t *new_file_label = lv_label_create(new_file_btn);
    lv_label_set_text(new_file_label, LV_SYMBOL_FILE " 文件");
    lv_obj_set_style_text_font(new_file_label, ui_common_get_font(), 0);
    lv_obj_center(new_file_label);
    
    // 删除按钮
    lv_obj_t *delete_btn = lv_btn_create(toolbar);
    lv_obj_set_size(delete_btn, 70, 40);
    lv_obj_add_event_cb(delete_btn, delete_btn_event_cb, LV_EVENT_CLICKED, fm);
    lv_obj_t *delete_label = lv_label_create(delete_btn);
    lv_label_set_text(delete_label, LV_SYMBOL_TRASH " 删除");
    lv_obj_set_style_text_font(delete_label, ui_common_get_font(), 0);
    lv_obj_center(delete_label);
    
    // 重命名按钮
    lv_obj_t *rename_btn = lv_btn_create(toolbar);
    lv_obj_set_size(rename_btn, 70, 40);
    lv_obj_add_event_cb(rename_btn, rename_btn_event_cb, LV_EVENT_CLICKED, fm);
    lv_obj_t *rename_label = lv_label_create(rename_btn);
    lv_label_set_text(rename_label, LV_SYMBOL_EDIT " 重命名");
    lv_obj_set_style_text_font(rename_label, ui_common_get_font(), 0);
    lv_obj_center(rename_label);
    
    /* 路径栏 */
    lv_obj_t *path_cont = lv_obj_create(fm->screen);
    lv_obj_set_size(path_cont, LV_PCT(95), 50);
    lv_obj_align(path_cont, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_set_style_bg_color(path_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(path_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    fm->path_label = lv_label_create(path_cont);
    char path_text[MAX_PATH_LEN + 10];
    snprintf(path_text, sizeof(path_text), "路径: %s", fm->current_path);
    lv_label_set_text(fm->path_label, path_text);
    lv_obj_set_style_text_font(fm->path_label, ui_common_get_font(), 0);
    lv_obj_align(fm->path_label, LV_ALIGN_LEFT_MID, 10, 0);
    
    /* 文件列表 */
    fm->file_list = lv_list_create(fm->screen);
    lv_obj_set_size(fm->file_list, LV_PCT(95), LV_PCT(100) - 290);
    lv_obj_align(fm->file_list, LV_ALIGN_TOP_MID, 0, 185);
    lv_obj_set_style_bg_color(fm->file_list, lv_color_hex(0xFFFFFF), 0);
    
    // 注意：点击事件会在创建每个列表项时添加
    
    /* 状态栏 */
    lv_obj_t *status_cont = lv_obj_create(fm->screen);
    lv_obj_set_size(status_cont, LV_PCT(95), 60);
    lv_obj_align(status_cont, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(status_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(status_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    fm->status_label = lv_label_create(status_cont);
    lv_label_set_text(fm->status_label, "状态: 就绪");
    lv_obj_set_style_text_font(fm->status_label, ui_common_get_font(), 0);
    lv_obj_align(fm->status_label, LV_ALIGN_LEFT_MID, 10, 0);
    
    // 自动刷新一次显示文件
    refresh_file_list(fm);
    
    log_info("File manager UI created");
    return fm;
}

void ui_file_manager_destroy(ui_file_manager_t *fm)
{
    if (!fm) return;
    
    log_info("开始销毁文件管理界面...");
    
    // 关闭输入对话框
    if (fm->input_dialog) {
        close_input_dialog(fm);
    }
    
    // 释放选中项名称
    if (fm->selected_item_name) {
        free(fm->selected_item_name);
        fm->selected_item_name = NULL;
    }
    
    // 释放双击检测数据
    if (fm->last_click_item) {
        free(fm->last_click_item);
        fm->last_click_item = NULL;
    }
    
    // 清除全局指针
    if (g_file_manager == fm) {
        g_file_manager = NULL;
    }
    
    // 释放内存
    free(fm);
    
    log_info("文件管理器界面已销毁");
}
