#include "ui_file.h"
#include "ui_main.h"
#include "../common/app_config.h"
#include "../file/file_manager.h"
#include <stdio.h>
#include <string.h>

static ui_file_t g_ui_file;

// 返回按钮事件
static void btn_back_event(lv_event_t *e) {
    ui_main_show_page(PAGE_HOME);
}

// 刷新按钮事件
static void btn_refresh_event(lv_event_t *e) {
    file_manager_refresh();
    ui_file_update_status("刷新完成");
}

// 上传按钮事件
static void btn_upload_event(lv_event_t *e) {
    // TODO: 实现文件上传对话框
    ui_file_update_status("文件上传功能开发中...");
}

// 下载按钮事件
static void btn_download_event(lv_event_t *e) {
    // TODO: 实现文件下载
    ui_file_update_status("文件下载功能开发中...");
}

// 删除按钮事件
static void btn_delete_event(lv_event_t *e) {
    // TODO: 实现文件删除
    ui_file_update_status("文件删除功能开发中...");
}

lv_obj_t* ui_file_create(lv_obj_t *parent) {
    // 创建主容器
    g_ui_file.container = lv_obj_create(parent);
    lv_obj_set_size(g_ui_file.container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_ui_file.container, lv_color_hex(COLOR_BG_MAIN), 0);
    lv_obj_set_style_border_width(g_ui_file.container, 0, 0);
    lv_obj_set_style_radius(g_ui_file.container, 0, 0);
    lv_obj_set_style_pad_all(g_ui_file.container, 10, 0);

    // 创建返回按钮
    g_ui_file.btn_back = lv_btn_create(g_ui_file.container);
    lv_obj_set_size(g_ui_file.btn_back, 80, 40);
    lv_obj_set_pos(g_ui_file.btn_back, 10, 10);
    lv_obj_add_event_cb(g_ui_file.btn_back, btn_back_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(g_ui_file.btn_back);
    lv_label_set_text(back_label, "< 返回");
    lv_obj_center(back_label);

    // 标题
    lv_obj_t *title = lv_label_create(g_ui_file.container);
    lv_label_set_text(title, "文件管理器");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(title, 100, 15);

    int y_pos = 70;

    // 路径标签
    g_ui_file.path_label = lv_label_create(g_ui_file.container);
    lv_label_set_text(g_ui_file.path_label, "路径: /");
    lv_obj_set_pos(g_ui_file.path_label, 20, y_pos);
    lv_obj_set_style_text_color(g_ui_file.path_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);

    // 控制按钮
    int btn_y = y_pos + 40;
    int btn_width = 90;
    int btn_height = 40;
    int btn_spacing = 15;

    // 刷新按钮
    g_ui_file.btn_refresh = lv_btn_create(g_ui_file.container);
    lv_obj_set_size(g_ui_file.btn_refresh, btn_width, btn_height);
    lv_obj_set_pos(g_ui_file.btn_refresh, 20, btn_y);
    lv_obj_set_style_bg_color(g_ui_file.btn_refresh, lv_color_hex(COLOR_PRIMARY), 0);
    lv_obj_add_event_cb(g_ui_file.btn_refresh, btn_refresh_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(g_ui_file.btn_refresh);
    lv_label_set_text(refresh_label, "刷新");
    lv_obj_center(refresh_label);

    // 上传按钮
    g_ui_file.btn_upload = lv_btn_create(g_ui_file.container);
    lv_obj_set_size(g_ui_file.btn_upload, btn_width, btn_height);
    lv_obj_set_pos(g_ui_file.btn_upload, 20 + btn_width + btn_spacing, btn_y);
    lv_obj_set_style_bg_color(g_ui_file.btn_upload, lv_color_hex(COLOR_SUCCESS), 0);
    lv_obj_add_event_cb(g_ui_file.btn_upload, btn_upload_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *upload_label = lv_label_create(g_ui_file.btn_upload);
    lv_label_set_text(upload_label, "上传");
    lv_obj_center(upload_label);

    // 下载按钮
    g_ui_file.btn_download = lv_btn_create(g_ui_file.container);
    lv_obj_set_size(g_ui_file.btn_download, btn_width, btn_height);
    lv_obj_set_pos(g_ui_file.btn_download, 20 + (btn_width + btn_spacing) * 2, btn_y);
    lv_obj_set_style_bg_color(g_ui_file.btn_download, lv_color_hex(COLOR_SECONDARY), 0);
    lv_obj_add_event_cb(g_ui_file.btn_download, btn_download_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *download_label = lv_label_create(g_ui_file.btn_download);
    lv_label_set_text(download_label, "下载");
    lv_obj_center(download_label);

    // 删除按钮
    g_ui_file.btn_delete = lv_btn_create(g_ui_file.container);
    lv_obj_set_size(g_ui_file.btn_delete, btn_width, btn_height);
    lv_obj_set_pos(g_ui_file.btn_delete, 20 + (btn_width + btn_spacing) * 3, btn_y);
    lv_obj_set_style_bg_color(g_ui_file.btn_delete, lv_color_hex(COLOR_DANGER), 0);
    lv_obj_add_event_cb(g_ui_file.btn_delete, btn_delete_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *delete_label = lv_label_create(g_ui_file.btn_delete);
    lv_label_set_text(delete_label, "删除");
    lv_obj_center(delete_label);

    // 文件列表
    int list_y = btn_y + btn_height + 20;
    int list_height = SCREEN_HEIGHT - STATUS_BAR_HEIGHT - list_y - 60;

    lv_obj_t *list_title = lv_label_create(g_ui_file.container);
    lv_label_set_text(list_title, "文件列表:");
    lv_obj_set_pos(list_title, 20, list_y);

    g_ui_file.file_list = lv_list_create(g_ui_file.container);
    lv_obj_set_size(g_ui_file.file_list, 980, list_height);
    lv_obj_set_pos(g_ui_file.file_list, 20, list_y + 30);
    lv_obj_set_style_bg_color(g_ui_file.file_list, lv_color_hex(0xF9FAFB), 0);

    // 状态标签
    g_ui_file.status_label = lv_label_create(g_ui_file.container);
    lv_label_set_text(g_ui_file.status_label, "就绪");
    lv_obj_set_pos(g_ui_file.status_label, 20, SCREEN_HEIGHT - STATUS_BAR_HEIGHT - 30);
    lv_obj_set_style_text_color(g_ui_file.status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);

    return g_ui_file.container;
}

void ui_file_update_list(const char **files, int count) {
    if (!g_ui_file.file_list) return;
    
    // 清空现有列表
    lv_obj_clean(g_ui_file.file_list);
    
    // 添加文件
    for (int i = 0; i < count; i++) {
        if (files[i]) {
            lv_obj_t *btn = lv_list_add_btn(g_ui_file.file_list, LV_SYMBOL_FILE, files[i]);
            lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
        }
    }
}

void ui_file_update_path(const char *path) {
    if (g_ui_file.path_label && path) {
        char text[256];
        snprintf(text, sizeof(text), "路径: %s", path);
        lv_label_set_text(g_ui_file.path_label, text);
    }
}

void ui_file_update_status(const char *status) {
    if (g_ui_file.status_label && status) {
        lv_label_set_text(g_ui_file.status_label, status);
    }
}

