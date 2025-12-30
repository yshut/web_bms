#include "ui_uds.h"
#include "ui_main.h"
#include "../common/app_config.h"
#include "../uds/uds_handler.h"
#include <stdio.h>
#include <string.h>

static ui_uds_t g_ui_uds;

/* ========== 线程安全的回调桥接 ========== */
typedef struct {
    int percent;
} uds_src_prog_msg_t;

typedef struct {
    char *line;
} uds_src_log_msg_t;

static void uds_src_apply_progress(void *p)
{
    uds_src_prog_msg_t *m = (uds_src_prog_msg_t*)p;
    if (m) {
        ui_uds_update_progress(m->percent);
        free(m);
    }
}

static void uds_src_append_log(void *p)
{
    uds_src_log_msg_t *m = (uds_src_log_msg_t*)p;
    if (m) {
        if (m->line) {
            /* 截断并限制日志长度，避免内存增长 */
            char buf[256];
            buf[0] = '\0';
            strncat(buf, m->line, sizeof(buf) - 1);
            ui_uds_add_log(buf);
            free(m->line);
        }
        free(m);
    }
}

static void uds_src_progress_cb(int percent)
{
    uds_src_prog_msg_t *msg = (uds_src_prog_msg_t*)malloc(sizeof(uds_src_prog_msg_t));
    if (!msg) return;
    msg->percent = percent;
    lv_async_call(uds_src_apply_progress, msg);
}

static void uds_src_log_cb(const char *line)
{
    uds_src_log_msg_t *msg = (uds_src_log_msg_t*)malloc(sizeof(uds_src_log_msg_t));
    if (!msg) return;
    msg->line = NULL;
    if (line) {
        size_t len = strlen(line);
        if (len > 255) len = 255;
        msg->line = (char*)malloc(len + 1);
        if (msg->line) { memcpy(msg->line, line, len); msg->line[len] = '\0'; }
    }
    lv_async_call(uds_src_append_log, msg);
}

// 返回按钮事件
static void btn_back_event(lv_event_t *e) {
    ui_main_show_page(PAGE_HOME);
}

// 启动按钮事件
static void btn_start_event(lv_event_t *e) {
    const char *file_path = lv_textarea_get_text(g_ui_uds.file_path_input);
    if (file_path && strlen(file_path) > 0) {
        /* 注册回调，确保进度/日志能显示到界面 */
        uds_handler_set_progress_callback(uds_src_progress_cb);
        uds_handler_set_log_callback(uds_src_log_cb);
        uds_handler_start(file_path);
        ui_uds_update_status("UDS下载已启动");
    } else {
        ui_uds_update_status("请输入文件路径");
    }
}

// 停止按钮事件
static void btn_stop_event(lv_event_t *e) {
    uds_handler_stop();
    ui_uds_update_status("UDS下载已停止");
}

lv_obj_t* ui_uds_create(lv_obj_t *parent) {
    // 创建主容器
    g_ui_uds.container = lv_obj_create(parent);
    lv_obj_set_size(g_ui_uds.container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_ui_uds.container, lv_color_hex(COLOR_BG_MAIN), 0);
    lv_obj_set_style_border_width(g_ui_uds.container, 0, 0);
    lv_obj_set_style_radius(g_ui_uds.container, 0, 0);
    lv_obj_set_style_pad_all(g_ui_uds.container, 10, 0);

    // 创建返回按钮
    g_ui_uds.btn_back = lv_btn_create(g_ui_uds.container);
    lv_obj_set_size(g_ui_uds.btn_back, 80, 40);
    lv_obj_set_pos(g_ui_uds.btn_back, 10, 10);
    lv_obj_add_event_cb(g_ui_uds.btn_back, btn_back_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(g_ui_uds.btn_back);
    lv_label_set_text(back_label, "< 返回");
    lv_obj_center(back_label);

    // 标题
    lv_obj_t *title = lv_label_create(g_ui_uds.container);
    lv_label_set_text(title, "UDS 诊断与固件下载");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(title, 100, 15);

    int y_pos = 70;

    // 文件路径标签
    lv_obj_t *file_label = lv_label_create(g_ui_uds.container);
    lv_label_set_text(file_label, "固件文件路径:");
    lv_obj_set_pos(file_label, 20, y_pos);

    // 文件路径输入框
    g_ui_uds.file_path_input = lv_textarea_create(g_ui_uds.container);
    lv_obj_set_size(g_ui_uds.file_path_input, 600, 40);
    lv_obj_set_pos(g_ui_uds.file_path_input, 20, y_pos + 30);
    lv_textarea_set_placeholder_text(g_ui_uds.file_path_input, "/path/to/firmware.bin");
    lv_textarea_set_one_line(g_ui_uds.file_path_input, true);

    // 控制按钮
    int btn_y = y_pos + 90;
    int btn_width = 100;
    int btn_height = 48;
    int btn_spacing = 20;

    // 启动按钮
    g_ui_uds.btn_start = lv_btn_create(g_ui_uds.container);
    lv_obj_set_size(g_ui_uds.btn_start, btn_width, btn_height);
    lv_obj_set_pos(g_ui_uds.btn_start, 20, btn_y);
    lv_obj_set_style_bg_color(g_ui_uds.btn_start, lv_color_hex(COLOR_SUCCESS), 0);
    lv_obj_add_event_cb(g_ui_uds.btn_start, btn_start_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *start_label = lv_label_create(g_ui_uds.btn_start);
    lv_label_set_text(start_label, "开始下载");
    lv_obj_center(start_label);

    // 停止按钮
    g_ui_uds.btn_stop = lv_btn_create(g_ui_uds.container);
    lv_obj_set_size(g_ui_uds.btn_stop, btn_width, btn_height);
    lv_obj_set_pos(g_ui_uds.btn_stop, 20 + btn_width + btn_spacing, btn_y);
    lv_obj_set_style_bg_color(g_ui_uds.btn_stop, lv_color_hex(COLOR_DANGER), 0);
    lv_obj_add_event_cb(g_ui_uds.btn_stop, btn_stop_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stop_label = lv_label_create(g_ui_uds.btn_stop);
    lv_label_set_text(stop_label, "停止");
    lv_obj_center(stop_label);

    // 进度条
    int progress_y = btn_y + btn_height + 30;
    lv_obj_t *progress_label = lv_label_create(g_ui_uds.container);
    lv_label_set_text(progress_label, "下载进度:");
    lv_obj_set_pos(progress_label, 20, progress_y);

    g_ui_uds.progress_bar = lv_bar_create(g_ui_uds.container);
    lv_obj_set_size(g_ui_uds.progress_bar, 800, 30);
    lv_obj_set_pos(g_ui_uds.progress_bar, 20, progress_y + 30);
    lv_bar_set_value(g_ui_uds.progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_ui_uds.progress_bar, lv_color_hex(COLOR_PRIMARY), LV_PART_INDICATOR);

    // 状态标签
    int status_y = progress_y + 70;
    g_ui_uds.status_label = lv_label_create(g_ui_uds.container);
    lv_label_set_text(g_ui_uds.status_label, "就绪");
    lv_obj_set_pos(g_ui_uds.status_label, 20, status_y);
    lv_obj_set_style_text_color(g_ui_uds.status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);

    // 日志文本区域
    int log_y = status_y + 30;
    lv_obj_t *log_label = lv_label_create(g_ui_uds.container);
    lv_label_set_text(log_label, "操作日志:");
    lv_obj_set_pos(log_label, 20, log_y);

    g_ui_uds.log_textarea = lv_textarea_create(g_ui_uds.container);
    lv_obj_set_size(g_ui_uds.log_textarea, 980, 150);
    lv_obj_set_pos(g_ui_uds.log_textarea, 20, log_y + 30);
    lv_textarea_set_text(g_ui_uds.log_textarea, "");
    lv_obj_set_style_bg_color(g_ui_uds.log_textarea, lv_color_hex(0xF9FAFB), 0);

    return g_ui_uds.container;
}

void ui_uds_update_status(const char *status) {
    if (g_ui_uds.status_label && status) {
        lv_label_set_text(g_ui_uds.status_label, status);
    }
}

void ui_uds_update_progress(int percent) {
    if (g_ui_uds.progress_bar) {
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        lv_bar_set_value(g_ui_uds.progress_bar, percent, LV_ANIM_OFF);
    }
}

void ui_uds_add_log(const char *log) {
    if (g_ui_uds.log_textarea && log) {
        const char *current = lv_textarea_get_text(g_ui_uds.log_textarea);
        char new_text[4096];
        snprintf(new_text, sizeof(new_text), "%s%s\n", current, log);
        lv_textarea_set_text(g_ui_uds.log_textarea, new_text);
        
        // 滚动到底部
        lv_obj_scroll_to_y(g_ui_uds.log_textarea, LV_COORD_MAX, LV_ANIM_ON);
    }
}

