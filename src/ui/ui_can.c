#include "ui_can.h"
#include "ui_main.h"
#include "../common/app_config.h"
#include "../can/can_worker.h"
#include <stdio.h>
#include <string.h>

static ui_can_t g_ui_can;

// 返回按钮事件
static void btn_back_event(lv_event_t *e) {
    ui_main_show_page(PAGE_HOME);
}

// 扫描按钮事件
static void btn_scan_event(lv_event_t *e) {
    can_worker_scan();
    ui_can_update_status("扫描CAN接口...");
}

// 启动按钮事件
static void btn_start_event(lv_event_t *e) {
    // 获取比特率
    uint32_t bitrate1 = 500000; // 默认值
    uint32_t bitrate2 = 500000;
    
    const char *text1 = lv_textarea_get_text(g_ui_can.bitrate_can1_input);
    const char *text2 = lv_textarea_get_text(g_ui_can.bitrate_can2_input);
    
    if (text1 && strlen(text1) > 0) {
        bitrate1 = atoi(text1);
    }
    if (text2 && strlen(text2) > 0) {
        bitrate2 = atoi(text2);
    }
    
    // 启动CAN
    bool can1_enabled = lv_obj_get_state(g_ui_can.can1_checkbox) & LV_STATE_CHECKED;
    bool can2_enabled = lv_obj_get_state(g_ui_can.can2_checkbox) & LV_STATE_CHECKED;
    
    can_worker_start(can1_enabled, can2_enabled, bitrate1, bitrate2);
    ui_can_update_status("CAN已启动");
}

// 停止按钮事件
static void btn_stop_event(lv_event_t *e) {
    can_worker_stop();
    ui_can_update_status("CAN已停止");
}

// 清除按钮事件
static void btn_clear_event(lv_event_t *e) {
    ui_can_clear_messages();
}

// 发送按钮事件
static void btn_send_event(lv_event_t *e) {
    const char *msg = lv_textarea_get_text(g_ui_can.can_msg_input);
    if (msg && strlen(msg) > 0) {
        can_worker_send_frame(msg);
        char status[128];
        snprintf(status, sizeof(status), "已发送: %s", msg);
        ui_can_update_status(status);
    }
}

lv_obj_t* ui_can_create(lv_obj_t *parent) {
    // 创建主容器
    g_ui_can.container = lv_obj_create(parent);
    lv_obj_set_size(g_ui_can.container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_ui_can.container, lv_color_hex(COLOR_BG_MAIN), 0);
    lv_obj_set_style_border_width(g_ui_can.container, 0, 0);
    lv_obj_set_style_radius(g_ui_can.container, 0, 0);
    lv_obj_set_style_pad_all(g_ui_can.container, 10, 0);

    // 创建返回按钮
    g_ui_can.btn_back = lv_btn_create(g_ui_can.container);
    lv_obj_set_size(g_ui_can.btn_back, 80, 40);
    lv_obj_set_pos(g_ui_can.btn_back, 10, 10);
    lv_obj_add_event_cb(g_ui_can.btn_back, btn_back_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(g_ui_can.btn_back);
    lv_label_set_text(back_label, "< 返回");
    lv_obj_center(back_label);

    // 标题
    lv_obj_t *title = lv_label_create(g_ui_can.container);
    lv_label_set_text(title, "CAN 总线监控");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(title, 100, 15);

    // 控制区域（左侧）
    int ctrl_x = 10;
    int ctrl_y = 60;
    int ctrl_spacing = 50;

    // CAN1 复选框
    g_ui_can.can1_checkbox = lv_checkbox_create(g_ui_can.container);
    lv_checkbox_set_text(g_ui_can.can1_checkbox, "CAN0");
    lv_obj_set_pos(g_ui_can.can1_checkbox, ctrl_x, ctrl_y);
    lv_obj_add_state(g_ui_can.can1_checkbox, LV_STATE_CHECKED);

    // CAN1 比特率输入
    g_ui_can.bitrate_can1_input = lv_textarea_create(g_ui_can.container);
    lv_obj_set_size(g_ui_can.bitrate_can1_input, 120, 40);
    lv_obj_set_pos(g_ui_can.bitrate_can1_input, ctrl_x + 100, ctrl_y - 5);
    lv_textarea_set_placeholder_text(g_ui_can.bitrate_can1_input, "500000");
    lv_textarea_set_one_line(g_ui_can.bitrate_can1_input, true);
    lv_textarea_set_text(g_ui_can.bitrate_can1_input, "500000");

    // CAN2 复选框
    g_ui_can.can2_checkbox = lv_checkbox_create(g_ui_can.container);
    lv_checkbox_set_text(g_ui_can.can2_checkbox, "CAN1");
    lv_obj_set_pos(g_ui_can.can2_checkbox, ctrl_x, ctrl_y + ctrl_spacing);
    lv_obj_add_state(g_ui_can.can2_checkbox, LV_STATE_CHECKED);

    // CAN2 比特率输入
    g_ui_can.bitrate_can2_input = lv_textarea_create(g_ui_can.container);
    lv_obj_set_size(g_ui_can.bitrate_can2_input, 120, 40);
    lv_obj_set_pos(g_ui_can.bitrate_can2_input, ctrl_x + 100, ctrl_y + ctrl_spacing - 5);
    lv_textarea_set_placeholder_text(g_ui_can.bitrate_can2_input, "500000");
    lv_textarea_set_one_line(g_ui_can.bitrate_can2_input, true);
    lv_textarea_set_text(g_ui_can.bitrate_can2_input, "500000");

    // 控制按钮
    int btn_y = ctrl_y + ctrl_spacing * 2;
    int btn_width = 70;
    int btn_height = 40;
    int btn_spacing = 10;

    // 扫描按钮
    g_ui_can.btn_scan = lv_btn_create(g_ui_can.container);
    lv_obj_set_size(g_ui_can.btn_scan, btn_width, btn_height);
    lv_obj_set_pos(g_ui_can.btn_scan, ctrl_x, btn_y);
    lv_obj_add_event_cb(g_ui_can.btn_scan, btn_scan_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_label = lv_label_create(g_ui_can.btn_scan);
    lv_label_set_text(scan_label, "扫描");
    lv_obj_center(scan_label);

    // 启动按钮
    g_ui_can.btn_start = lv_btn_create(g_ui_can.container);
    lv_obj_set_size(g_ui_can.btn_start, btn_width, btn_height);
    lv_obj_set_pos(g_ui_can.btn_start, ctrl_x + btn_width + btn_spacing, btn_y);
    lv_obj_set_style_bg_color(g_ui_can.btn_start, lv_color_hex(COLOR_SUCCESS), 0);
    lv_obj_add_event_cb(g_ui_can.btn_start, btn_start_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *start_label = lv_label_create(g_ui_can.btn_start);
    lv_label_set_text(start_label, "启动");
    lv_obj_center(start_label);

    // 停止按钮
    g_ui_can.btn_stop = lv_btn_create(g_ui_can.container);
    lv_obj_set_size(g_ui_can.btn_stop, btn_width, btn_height);
    lv_obj_set_pos(g_ui_can.btn_stop, ctrl_x + (btn_width + btn_spacing) * 2, btn_y);
    lv_obj_set_style_bg_color(g_ui_can.btn_stop, lv_color_hex(COLOR_DANGER), 0);
    lv_obj_add_event_cb(g_ui_can.btn_stop, btn_stop_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stop_label = lv_label_create(g_ui_can.btn_stop);
    lv_label_set_text(stop_label, "停止");
    lv_obj_center(stop_label);

    // 清除按钮
    g_ui_can.btn_clear = lv_btn_create(g_ui_can.container);
    lv_obj_set_size(g_ui_can.btn_clear, btn_width, btn_height);
    lv_obj_set_pos(g_ui_can.btn_clear, ctrl_x, btn_y + btn_height + btn_spacing);
    lv_obj_set_style_bg_color(g_ui_can.btn_clear, lv_color_hex(COLOR_WARNING), 0);
    lv_obj_add_event_cb(g_ui_can.btn_clear, btn_clear_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *clear_label = lv_label_create(g_ui_can.btn_clear);
    lv_label_set_text(clear_label, "清除");
    lv_obj_center(clear_label);

    // CAN消息输入框
    int input_y = btn_y + btn_height * 2 + btn_spacing * 2;
    lv_obj_t *msg_label = lv_label_create(g_ui_can.container);
    lv_label_set_text(msg_label, "发送消息:");
    lv_obj_set_pos(msg_label, ctrl_x, input_y);

    g_ui_can.can_msg_input = lv_textarea_create(g_ui_can.container);
    lv_obj_set_size(g_ui_can.can_msg_input, 200, 40);
    lv_obj_set_pos(g_ui_can.can_msg_input, ctrl_x, input_y + 25);
    lv_textarea_set_placeholder_text(g_ui_can.can_msg_input, "123#0102030405060708");
    lv_textarea_set_one_line(g_ui_can.can_msg_input, true);

    // 发送按钮
    g_ui_can.btn_send = lv_btn_create(g_ui_can.container);
    lv_obj_set_size(g_ui_can.btn_send, btn_width, btn_height);
    lv_obj_set_pos(g_ui_can.btn_send, ctrl_x + 210, input_y + 25);
    lv_obj_set_style_bg_color(g_ui_can.btn_send, lv_color_hex(COLOR_PRIMARY), 0);
    lv_obj_add_event_cb(g_ui_can.btn_send, btn_send_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *send_label = lv_label_create(g_ui_can.btn_send);
    lv_label_set_text(send_label, "发送");
    lv_obj_center(send_label);

    // 消息显示区域（右侧）
    int list_x = 310;
    int list_width = SCREEN_WIDTH - list_x - 20;
    int list_height = SCREEN_HEIGHT - STATUS_BAR_HEIGHT - 120;

    lv_obj_t *list_title = lv_label_create(g_ui_can.container);
    lv_label_set_text(list_title, "CAN 消息");
    lv_obj_set_pos(list_title, list_x, ctrl_y);

    g_ui_can.msg_list = lv_list_create(g_ui_can.container);
    lv_obj_set_size(g_ui_can.msg_list, list_width, list_height);
    lv_obj_set_pos(g_ui_can.msg_list, list_x, ctrl_y + 30);
    lv_obj_set_style_bg_color(g_ui_can.msg_list, lv_color_hex(0xF9FAFB), 0);

    // 状态标签
    g_ui_can.status_label = lv_label_create(g_ui_can.container);
    lv_label_set_text(g_ui_can.status_label, "就绪");
    lv_obj_set_pos(g_ui_can.status_label, 10, SCREEN_HEIGHT - STATUS_BAR_HEIGHT - 30);
    lv_obj_set_style_text_color(g_ui_can.status_label, lv_color_hex(COLOR_TEXT_SECONDARY), 0);

    return g_ui_can.container;
}

void ui_can_add_message(const char *message) {
    if (g_ui_can.msg_list && message) {
        lv_obj_t *btn = lv_list_add_btn(g_ui_can.msg_list, NULL, message);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
        
        // 自动滚动到底部
        lv_obj_scroll_to_y(g_ui_can.msg_list, LV_COORD_MAX, LV_ANIM_ON);
    }
}

void ui_can_clear_messages(void) {
    if (g_ui_can.msg_list) {
        lv_obj_clean(g_ui_can.msg_list);
    }
}

void ui_can_update_status(const char *status) {
    if (g_ui_can.status_label && status) {
        lv_label_set_text(g_ui_can.status_label, status);
    }
}

