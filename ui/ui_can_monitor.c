/**
 * @file ui_can_monitor.c
 * @brief CAN监控界面实现 - 完整版本
 */

#include "ui_can_monitor.h"
#include "ui_common.h"
#include "../logic/can_handler.h"
#include "../logic/can_recorder.h"
#include "../logic/app_manager.h"
#include "../utils/logger.h"
#include "../utils/ring_buffer.h"
#include "../utils/frame_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <stdbool.h>
#include <linux/can.h>  /* CAN_EFF_FLAG, CAN_EFF_MASK, CAN_SFF_MASK */

#define MAX_CAN_MESSAGES 50  /* 最多显示的CAN消息数（内存优化）*/

static ui_can_monitor_t *g_can_monitor = NULL;
static frame_queue_t *s_rx_q = NULL;   /* 后台线程入队，UI定时器出队 */

#define RX_RING_CAPACITY   (sizeof(can_frame_t) * 2048)
#define RX_QUEUE_CAPACITY  2048
#define DRAIN_PER_TICK     64

/* 输入框聚焦时，自动滚动到视图内 */
static void input_focus_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED || lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_scroll_to_view_recursive(obj, LV_ANIM_OFF);
        /* 显示软键盘并绑定到当前输入框 */
        if (g_can_monitor && g_can_monitor->kb) {
            lv_keyboard_set_textarea(g_can_monitor->kb, obj);
            lv_obj_clear_flag(g_can_monitor->kb_cont, LV_OBJ_FLAG_HIDDEN);
            if (g_can_monitor->kb_preview) {
                const char *cur = lv_textarea_get_text(obj);
                lv_textarea_set_text(g_can_monitor->kb_preview, cur ? cur : "");
            }
        }
    }
}

/* 输入内容变化时，更新软键盘上方显示 */
static void input_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    if (!g_can_monitor || !g_can_monitor->kb_preview) return;
    const char *cur = lv_textarea_get_text(obj);
    lv_textarea_set_text(g_can_monitor->kb_preview, cur ? cur : "");
}

/* 隐藏软键盘 */
static void kb_hide_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (!g_can_monitor) return;
    lv_obj_add_flag(g_can_monitor->kb_cont, LV_OBJ_FLAG_HIDDEN);
    if (g_can_monitor->kb) {
        lv_keyboard_set_textarea(g_can_monitor->kb, NULL);
    }
}

/**
 * @brief 返回按钮事件回调
 */
static void back_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        log_info("Back to home from CAN monitor");
        app_manager_switch_to_page(APP_PAGE_HOME);
    }
}

/**
 * @brief 配置按钮事件
 */
static void config_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        log_info("Configure CAN");
        ui_can_monitor_t *can_mon = (ui_can_monitor_t *)lv_event_get_user_data(e);
        if (!can_mon) return;

        // 读取下拉框选择
        char ch_buf[8];
        lv_dropdown_get_selected_str(can_mon->channel_dd, ch_buf, sizeof(ch_buf));
        /* QT映射：CAN1->can0, CAN2->can1 */
        const char *sel_if = (!strcmp(ch_buf, "CAN2")) ? "can1" : "can0";

        char br0_buf[16], br1_buf[16];
        lv_dropdown_get_selected_str(can_mon->bitrate0_dd, br0_buf, sizeof(br0_buf));
        lv_dropdown_get_selected_str(can_mon->bitrate1_dd, br1_buf, sizeof(br1_buf));
        uint32_t br0 = (uint32_t)strtoul(br0_buf, NULL, 10);
        uint32_t br1 = (uint32_t)strtoul(br1_buf, NULL, 10);

        // 如果在运行，先停止
        if (can_handler_is_running()) {
            can_handler_stop();
            can_handler_deinit();
        }

        // 同步配置can0/can1波特率
        int rc0 = can_handler_configure("can0", br0);
        int rc1 = can_handler_configure("can1", br1);

        if (can_mon->status_label) {
            char status[128];
            snprintf(status, sizeof(status),
                     "Status: Config OK (can0 %u, can1 %u), Active: %s",
                     rc0==0?br0:0, rc1==0?br1:0, sel_if);
            lv_label_set_text(can_mon->status_label, status);
        }
    }
}

/**
 * @brief 发送CAN帧按钮事件
 */
static void send_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        log_info("Send CAN frame");
        ui_can_monitor_t *can_mon = (ui_can_monitor_t *)lv_event_get_user_data(e);
        if (!can_mon || !can_mon->id_input || !can_mon->data_input) return;
        
        // 获取ID和数据
        const char *id_str = lv_textarea_get_text(can_mon->id_input);
        const char *data_str = lv_textarea_get_text(can_mon->data_input);
        
        // 解析ID
        uint32_t can_id = 0;
        if (sscanf(id_str, "%x", &can_id) != 1) {
            log_error("Invalid CAN ID format");
        return;
    }
    
        // 解析数据
        can_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        
        // 判断是标准帧还是扩展帧（标准帧ID范围：0-0x7FF，扩展帧：0x800-0x1FFFFFFF）
        if (can_id > 0x7FF) {
            frame.is_extended = true;
            frame.can_id = can_id & 0x1FFFFFFF;  // 确保ID在有效范围内
        } else {
            frame.is_extended = false;
            frame.can_id = can_id & 0x7FF;  // 确保ID在有效范围内
        }
        frame.can_dlc = 0;
        
        const char *p = data_str;
        while (*p && frame.can_dlc < 8) {
            while (*p == ' ') p++;
            if (!*p) break;
            
            unsigned int byte_val;
            if (sscanf(p, "%2x", &byte_val) == 1) {
                frame.data[frame.can_dlc++] = (uint8_t)byte_val;
                p += 2;
            } else {
                break;
            }
        }
        
        // 确定发送通道：auto=当前接收通道，否则按Tx CH
        char tx_buf[8];
        lv_dropdown_get_selected_str(can_mon->tx_channel_dd, tx_buf, sizeof(tx_buf));
        const char *tx_if = NULL;
        if (!strcmp(tx_buf, "auto")) {
            // 使用当前运行接口
            tx_if = NULL; // NULL 表示用当前can_handler socket发送
        } else if (!strcmp(tx_buf, "CAN2")) {
            tx_if = "can1"; // QT映射 CAN2->can1
        } else {
            tx_if = "can0"; // QT映射 CAN1->can0
        }

        int send_rc = -1;
        if (tx_if == NULL) {
            send_rc = can_handler_send(&frame);
        } else {
            send_rc = can_handler_send_on(tx_if, &frame);
        }

        if (send_rc == 0) {
            // 确定发送通道名称
            const char *channel_name = "CAN1"; // 默认CAN1
            if (tx_if != NULL) {
                if (!strcmp(tx_if, "can1")) {
                    channel_name = "CAN2";
                } else {
                    channel_name = "CAN1";
                }
            } else if (!strcmp(tx_buf, "CAN2")) {
                channel_name = "CAN2";
            }
            
            log_info("CAN frame sent: %s ID=0x%03X DLC=%d", channel_name, frame.can_id, frame.can_dlc);
            
            // 添加到消息列表
            if (g_can_monitor && g_can_monitor->list) {
                char msg_buf[256];  // 增加缓冲区大小
                
                // 获取当前时间戳
                struct timeval tv;
                gettimeofday(&tv, NULL);
                struct tm *tm_info = localtime(&tv.tv_sec);
                char time_str[32];
                snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d.%03ld",
                         tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                         tv.tv_usec / 1000);
                
                // 判断是否为扩展帧（使用本地frame的标志）
                bool is_extended = frame.is_extended;
                uint32_t id = frame.can_id;  // 已经是纯ID
                
                // 构建数据字符串
                char data_str[32];
                char *p = data_str;
                for (int i = 0; i < frame.can_dlc && i < 8; i++) {
                    int written = snprintf(p, sizeof(data_str) - (p - data_str), "%02X ", frame.data[i]);
                    if (written > 0 && written < (int)(sizeof(data_str) - (p - data_str))) {
                        p += written;
                    } else {
                        break;  // 防止溢出
                    }
                }
                if (p > data_str && *(p-1) == ' ') {
                    *(p-1) = '\0';  // 移除末尾空格
                } else {
                    *p = '\0';
                }
                
                // 格式：[时间] [TX] 通道 ID:[ID值] [数据]
                if (is_extended) {
                    snprintf(msg_buf, sizeof(msg_buf), "[%s] [TX] %s ID:0x%08X [%s]", 
                             time_str, channel_name, id, data_str);
                } else {
                    snprintf(msg_buf, sizeof(msg_buf), "[%s] [TX] %s ID:0x%03X [%s]", 
                             time_str, channel_name, id, data_str);
                }
                
                lv_obj_t *item = lv_list_add_text(g_can_monitor->list, msg_buf);
                if (item) {
                    lv_obj_set_style_text_font(item, ui_common_get_font(), 0);
                    lv_obj_set_style_text_color(item, lv_color_hex(0x00AA00), 0);
                }
    
                // 限制消息数量
    uint32_t child_cnt = lv_obj_get_child_cnt(g_can_monitor->list);
    if (child_cnt > MAX_CAN_MESSAGES) {
        lv_obj_t *first_child = lv_obj_get_child(g_can_monitor->list, 0);
                    if (first_child) lv_obj_del(first_child);
                }
            }
        } else {
            log_error("Failed to send CAN frame");
            if (can_mon->status_label) {
                lv_label_set_text(can_mon->status_label, "Status: TX queue busy");
            }
        }
    }
}

/**
 * @brief CAN帧接收回调
 */
static void can_frame_received_callback(int channel, const can_frame_t *frame, void *user_data)
{
    (void)user_data;
    
    /* 在接收线程中，仅入队到UI显示，不做任何LVGL操作 */
    /* 录制器和WebSocket上报由分发器自动处理 */
    if (s_rx_q) {
        if (!frame_queue_push(s_rx_q, frame)) {
            log_warn("UI frame queue full, drop frame ID=0x%X (channel=%d)", frame->can_id, channel);
        }
    }
}

/**
 * @brief 统计信息更新定时器回调
 */
static void update_stats_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    
    if (!g_can_monitor || !g_can_monitor->stats_label) {
        return;
    }
    
    can_stats_t stats;
    can_handler_get_stats(&stats);
    
    char stats_buf[128];
    snprintf(stats_buf, sizeof(stats_buf), 
             "RX: %u  TX: %u  Error: %u", 
             stats.rx_count, stats.tx_count, stats.error_count);
    
    lv_label_set_text(g_can_monitor->stats_label, stats_buf);

    /* 从环形缓冲区批量取出RX帧并更新UI（在LVGL上下文内） */
    if (s_rx_q && g_can_monitor->list) {
        int drained = 0;
        while (drained < DRAIN_PER_TICK) {
            can_frame_t fr;
            if (!frame_queue_pop(s_rx_q, &fr)) break;

            // 数据有效性检查：防止读取到损坏的数据
            if (fr.can_dlc > 8) {
                log_warn("Invalid DLC detected: %u, dropping corrupted frame", fr.can_dlc);
                continue;  // 跳过损坏的帧
            }
            if (fr.channel > 1) {
                log_warn("Invalid channel detected: %u, dropping corrupted frame", fr.channel);
                continue;  // 跳过损坏的帧
            }

            // 再次确保ID被正确提取（防止环形缓冲区数据损坏）
            bool is_extended = fr.is_extended;
            uint32_t id = fr.can_id;
            
            // 根据帧类型进行ID掩码处理
            if (is_extended) {
                // 扩展帧：确保ID在29位范围内（0x1FFFFFFF）
                id = id & 0x1FFFFFFF;
                // 如果ID以0x8开头，可能是数据损坏，尝试修复
                if ((id & 0xF0000000) == 0x80000000) {
                    log_warn("Suspicious extended ID detected: 0x%08X, fixing", id);
                    id = id & 0x1FFFFFFF;  // 去除可能的错误标志位
                }
            } else {
                // 标准帧：确保ID在11位范围内（0x7FF）
                id = id & 0x7FF;
            }

            char msg_buf[256];  // 增加缓冲区大小
            
            // 获取当前时间戳
            struct timeval tv;
            gettimeofday(&tv, NULL);
            struct tm *tm_info = localtime(&tv.tv_sec);
            char time_str[32];
            snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d.%03ld",
                     tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                     tv.tv_usec / 1000);
            
            // 通道名称
            const char *channel_name = (fr.channel == 1) ? "CAN2" : "CAN1";
            
            // 构建数据字符串
            char data_str[32];
            char *p = data_str;
            for (int i = 0; i < fr.can_dlc && i < 8; i++) {
                int written = snprintf(p, sizeof(data_str) - (p - data_str), "%02X ", fr.data[i]);
                if (written > 0 && written < (int)(sizeof(data_str) - (p - data_str))) {
                    p += written;
                } else {
                    break;  // 防止溢出
                }
            }
            if (p > data_str && *(p-1) == ' ') {
                *(p-1) = '\0';  // 移除末尾空格
            } else {
                *p = '\0';
            }
            
            // 格式：[时间] [RX] 通道 ID:[ID值] [数据]
            if (is_extended) {
                snprintf(msg_buf, sizeof(msg_buf), "[%s] [RX] %s ID:0x%08X [%s]", 
                         time_str, channel_name, id, data_str);
            } else {
                snprintf(msg_buf, sizeof(msg_buf), "[%s] [RX] %s ID:0x%03X [%s]", 
                         time_str, channel_name, id, data_str);
            }
            
            lv_obj_t *item = lv_list_add_text(g_can_monitor->list, msg_buf);
            if (item) {
                lv_obj_set_style_text_font(item, ui_common_get_font(), 0);
            }
            uint32_t child_cnt = lv_obj_get_child_cnt(g_can_monitor->list);
            if (child_cnt > MAX_CAN_MESSAGES) {
                lv_obj_t *first_child = lv_obj_get_child(g_can_monitor->list, 0);
                if (first_child) lv_obj_del(first_child);
            }
            drained++;
        }
    }
}

/**
 * @brief 启动按钮事件回调
 */
static void start_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        log_info("Start CAN monitoring");
        ui_can_monitor_t *can_mon = g_can_monitor;
        if (!can_mon) return;

        // 读取两路波特率
        char br0_buf[16], br1_buf[16];
        lv_dropdown_get_selected_str(can_mon->bitrate0_dd, br0_buf, sizeof(br0_buf));
        lv_dropdown_get_selected_str(can_mon->bitrate1_dd, br1_buf, sizeof(br1_buf));
        uint32_t br0 = (uint32_t)strtoul(br0_buf, NULL, 10);
        uint32_t br1 = (uint32_t)strtoul(br1_buf, NULL, 10);

        // 双通道初始化
        if (can_handler_init_dual(br0, br1) < 0) {
            log_error("CAN handler init failed");
            return;
        }
        
        // 先注册全局分发器（确保WebSocket和录制功能正常）
        extern void can_frame_dispatcher_callback(int channel, const can_frame_t *frame, void *user_data);
        can_handler_register_callback(can_frame_dispatcher_callback, NULL);
        
        // 再注册UI回调到分发器
        extern void can_frame_dispatcher_register_ui_callback(can_frame_callback_t callback, void *user_data);
        can_frame_dispatcher_register_ui_callback(can_frame_received_callback, NULL);
        
        // 启动接收
        if (can_handler_start() < 0) {
            log_error("Start CAN receive failed");
            can_handler_deinit();
            return;
        }
        
        log_info("CAN监控：已注册分发器和UI回调");
        
        // 更新按钮状态
        lv_obj_add_state(can_mon->start_btn, LV_STATE_DISABLED);
        lv_obj_clear_state(can_mon->stop_btn, LV_STATE_DISABLED);
        if (can_mon->status_label) {
            char status[128];
            snprintf(status, sizeof(status), "Status: Running (CAN1 %u, CAN2 %u)", br0, br1);
            lv_label_set_text(can_mon->status_label, status);
        }
    }
}

/**
 * @brief 停止按钮事件回调
 */
static void stop_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        log_info("Stop CAN monitoring");
        
        // 先注销UI回调
        extern void can_frame_dispatcher_register_ui_callback(can_frame_callback_t callback, void *user_data);
        can_frame_dispatcher_register_ui_callback(NULL, NULL);
        
        // 停止CAN
        can_handler_stop();
        can_handler_deinit();
        
        log_info("CAN监控：已停止并注销UI回调");
        
        // 更新按钮状态
        ui_can_monitor_t *can_mon = g_can_monitor;
        if (!can_mon) return;
        lv_obj_clear_state(can_mon->start_btn, LV_STATE_DISABLED);
        lv_obj_add_state(can_mon->stop_btn, LV_STATE_DISABLED);
        if (can_mon->status_label) {
            lv_label_set_text(can_mon->status_label, "Status: Stopped");
        }
    }
}

/**
 * @brief 清除按钮事件回调
 */
static void clear_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        log_info("Clear CAN messages");
        
        if (g_can_monitor && g_can_monitor->list) {
            lv_obj_clean(g_can_monitor->list);
        }
    }
}

/**
 * @brief 录制按钮事件回调
 */
static void record_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        ui_can_monitor_t *can_mon = (ui_can_monitor_t *)lv_event_get_user_data(e);
        if (!can_mon) return;
        
        if (!can_recorder_is_recording()) {
            // 开始录制
            bool record_can0 = lv_obj_has_state(can_mon->record_can0_cb, LV_STATE_CHECKED);
            bool record_can1 = lv_obj_has_state(can_mon->record_can1_cb, LV_STATE_CHECKED);
            
            if (!record_can0 && !record_can1) {
                log_warn("至少选择一个CAN通道进行录制");
                if (can_mon->record_status_label) {
                    lv_label_set_text(can_mon->record_status_label, "请至少选择一个通道");
                    lv_obj_set_style_text_color(can_mon->record_status_label, lv_color_hex(0xFF0000), 0);
                }
                return;
            }
            
            can_recorder_set_channels(record_can0, record_can1);
            
            if (can_recorder_start() == 0) {
                log_info("开始录制CAN报文 (CAN0:%d, CAN1:%d)", record_can0, record_can1);
                
                // 更新按钮文本
                if (can_mon->record_btn) {
                    lv_obj_t *label = lv_obj_get_child(can_mon->record_btn, 0);
                    if (label) lv_label_set_text(label, LV_SYMBOL_STOP " 停止录制");
                    lv_obj_set_style_bg_color(can_mon->record_btn, lv_color_hex(0xFF0000), 0);
                }
                
                // 更新状态标签
                if (can_mon->record_status_label) {
                    lv_label_set_text(can_mon->record_status_label, "录制中...");
                    lv_obj_set_style_text_color(can_mon->record_status_label, lv_color_hex(0x00FF00), 0);
                }
                
                // 禁用通道选择
                lv_obj_add_state(can_mon->record_can0_cb, LV_STATE_DISABLED);
                lv_obj_add_state(can_mon->record_can1_cb, LV_STATE_DISABLED);
            } else {
                log_error("开始录制失败");
                if (can_mon->record_status_label) {
                    lv_label_set_text(can_mon->record_status_label, "录制失败");
                    lv_obj_set_style_text_color(can_mon->record_status_label, lv_color_hex(0xFF0000), 0);
                }
            }
        } else {
            // 停止录制
            can_recorder_stop();
            log_info("停止录制CAN报文");
            
            // 获取录制统计
            can_recorder_stats_t stats;
            can_recorder_get_stats(&stats);
            log_info("录制统计: 总帧数=%llu, CAN0=%llu, CAN1=%llu, 文件=%u", 
                     stats.total_frames, stats.can0_frames, stats.can1_frames, stats.file_count);
            
            // 更新按钮文本
            if (can_mon->record_btn) {
                lv_obj_t *label = lv_obj_get_child(can_mon->record_btn, 0);
                if (label) lv_label_set_text(label, LV_SYMBOL_SAVE " 开始录制");
                lv_obj_set_style_bg_color(can_mon->record_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
            }
            
            // 更新状态标签
            if (can_mon->record_status_label) {
                char status_text[128];
                snprintf(status_text, sizeof(status_text), "已停止 (共%llu帧)", stats.total_frames);
                lv_label_set_text(can_mon->record_status_label, status_text);
                lv_obj_set_style_text_color(can_mon->record_status_label, lv_color_hex(0x808080), 0);
            }
            
            // 恢复通道选择
            lv_obj_clear_state(can_mon->record_can0_cb, LV_STATE_DISABLED);
            lv_obj_clear_state(can_mon->record_can1_cb, LV_STATE_DISABLED);
        }
    }
}

/**
 * @brief 创建CAN监控界面
 */
ui_can_monitor_t* ui_can_monitor_create(void)
{
    log_info("Creating CAN monitor UI...");
    
    ui_can_monitor_t *can_mon = malloc(sizeof(ui_can_monitor_t));
    if (!can_mon) {
        log_error("Failed to allocate CAN monitor UI");
        return NULL;
    }
    memset(can_mon, 0, sizeof(ui_can_monitor_t));
    
    // 创建屏幕
    can_mon->screen = lv_obj_create(NULL);
    if (!can_mon->screen) {
        log_error("Failed to create CAN monitor screen");
        free(can_mon);
        return NULL;
    }
    
    lv_obj_set_style_bg_color(can_mon->screen, lv_color_hex(0xF0F0F0), 0);
    
    /* ========== 顶部标题栏 ========== */
    lv_obj_t *header = lv_obj_create(can_mon->screen);
    lv_obj_set_size(header, LV_PCT(100), 50);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // 返回按钮
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 70, 35);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, can_mon);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_font(back_label, ui_common_get_font(), 0);
    lv_obj_center(back_label);
    lv_obj_clear_flag(back_label, LV_OBJ_FLAG_CLICKABLE);  // 重要：防止事件被标签拦截
    
    // 标题
    lv_obj_t *title_label = lv_label_create(header);
    lv_label_set_text(title_label, "CAN Monitor");
    lv_obj_set_style_text_font(title_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(title_label);
    
    /* ========== 可滚动主体区域 ========== */
    lv_obj_t *body = lv_obj_create(can_mon->screen);
    int32_t body_h = lv_obj_get_height(can_mon->screen) - 55;
    if (body_h < 50) body_h = 50;
    lv_obj_set_size(body, LV_PCT(100), body_h);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_style_pad_all(body, 6, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_style_pad_bottom(body, 100, 0); /* 加大底部空白，保证发送区完全可见 */
    lv_obj_set_style_pad_left(body, 0, 0);
    lv_obj_set_style_pad_right(body, 0, 0);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF); /* 隐藏右侧滚动条 */
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_outline_width(body, 0, 0);

    /* ========== 配置区域 ========== */
    lv_obj_t *config_cont = lv_obj_create(body);
    lv_obj_set_size(config_cont, LV_PCT(100), 95);
    lv_obj_set_style_bg_color(config_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(config_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(config_cont, 0, 0);
    
    // 配置按钮
    lv_obj_t *config_btn = lv_btn_create(config_cont);
    lv_obj_set_size(config_btn, 80, 30);
    lv_obj_align(config_btn, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_add_event_cb(config_btn, config_btn_event_cb, LV_EVENT_CLICKED, can_mon);
    lv_obj_t *config_label = lv_label_create(config_btn);
    lv_label_set_text(config_label, "Config");
    lv_obj_set_style_text_font(config_label, ui_common_get_font(), 0);
    lv_obj_center(config_label);
    lv_obj_clear_flag(config_label, LV_OBJ_FLAG_CLICKABLE);
    // 确保容器与下拉可点击
    lv_obj_clear_flag(config_cont, LV_OBJ_FLAG_CLICKABLE);
    
    // 通道选择
    lv_obj_t *ch_label = lv_label_create(config_cont);
    lv_label_set_text(ch_label, "Channel:");
    lv_obj_set_style_text_font(ch_label, ui_common_get_font(), 0);
    lv_obj_align(ch_label, LV_ALIGN_TOP_LEFT, 95, 5);
    can_mon->channel_dd = lv_dropdown_create(config_cont);
    lv_dropdown_set_options_static(can_mon->channel_dd, "CAN1\nCAN2");
    lv_obj_set_size(can_mon->channel_dd, 90, 32);
    lv_obj_align(can_mon->channel_dd, LV_ALIGN_TOP_LEFT, 160, 0);
    
    // CAN0波特率
    lv_obj_t *br0_label = lv_label_create(config_cont);
    lv_label_set_text(br0_label, "CAN1:");
    lv_obj_set_style_text_font(br0_label, ui_common_get_font(), 0);
    lv_obj_align(br0_label, LV_ALIGN_TOP_LEFT, 270, 5);
    can_mon->bitrate0_dd = lv_dropdown_create(config_cont);
    lv_dropdown_set_options_static(can_mon->bitrate0_dd, "125000\n250000\n500000\n1000000");
    lv_dropdown_set_selected(can_mon->bitrate0_dd, 2); // 500000默认
    lv_obj_set_size(can_mon->bitrate0_dd, 110, 32);
    lv_obj_align(can_mon->bitrate0_dd, LV_ALIGN_TOP_LEFT, 320, 0);
    
    // CAN1波特率
    lv_obj_t *br1_label = lv_label_create(config_cont);
    lv_label_set_text(br1_label, "CAN2:");
    lv_obj_set_style_text_font(br1_label, ui_common_get_font(), 0);
    lv_obj_align(br1_label, LV_ALIGN_TOP_LEFT, 450, 5);
    can_mon->bitrate1_dd = lv_dropdown_create(config_cont);
    lv_dropdown_set_options_static(can_mon->bitrate1_dd, "125000\n250000\n500000\n1000000");
    lv_dropdown_set_selected(can_mon->bitrate1_dd, 2);
    lv_obj_set_size(can_mon->bitrate1_dd, 110, 32);
    lv_obj_align(can_mon->bitrate1_dd, LV_ALIGN_TOP_LEFT, 500, 0);
    
    // 发送通道选择
    lv_obj_t *tx_label = lv_label_create(config_cont);
    lv_label_set_text(tx_label, "Tx CH:");
    lv_obj_set_style_text_font(tx_label, ui_common_get_font(), 0);
    lv_obj_align(tx_label, LV_ALIGN_TOP_LEFT, 640, 5);
    can_mon->tx_channel_dd = lv_dropdown_create(config_cont);
    lv_dropdown_set_options_static(can_mon->tx_channel_dd, "auto\nCAN1\nCAN2");
    lv_obj_set_size(can_mon->tx_channel_dd, 90, 32);
    lv_obj_align(can_mon->tx_channel_dd, LV_ALIGN_TOP_LEFT, 690, 0);

    // 状态标签
    can_mon->status_label = lv_label_create(config_cont);
    lv_label_set_text(can_mon->status_label, "Status: Not configured");
    lv_obj_set_style_text_font(can_mon->status_label, ui_common_get_font(), 0);
    lv_obj_align(can_mon->status_label, LV_ALIGN_LEFT_MID, 95, 55);
    
    /* ========== 统计信息栏 ========== */
    lv_obj_t *stats_cont = lv_obj_create(body);
    lv_obj_set_size(stats_cont, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(stats_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(stats_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(stats_cont, 0, 0);
    
    can_mon->stats_label = lv_label_create(stats_cont);
    lv_label_set_text(can_mon->stats_label, "RX: 0  TX: 0  Error: 0");
    lv_obj_set_style_text_font(can_mon->stats_label, ui_common_get_font(), 0);
    lv_obj_center(can_mon->stats_label);
    
    /* ========== 控制按钮 ========== */
    lv_obj_t *btn_cont = lv_obj_create(body);
    lv_obj_set_size(btn_cont, LV_PCT(100), 50);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    
    // 启动按钮
    can_mon->start_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(can_mon->start_btn, 85, 35);
    lv_obj_add_event_cb(can_mon->start_btn, start_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *start_label = lv_label_create(can_mon->start_btn);
    lv_label_set_text(start_label, LV_SYMBOL_PLAY " Start");
    lv_obj_set_style_text_font(start_label, ui_common_get_font(), 0);
    lv_obj_center(start_label);
    lv_obj_clear_flag(start_label, LV_OBJ_FLAG_CLICKABLE);
    
    // 停止按钮
    can_mon->stop_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(can_mon->stop_btn, 85, 35);
    lv_obj_add_event_cb(can_mon->stop_btn, stop_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(can_mon->stop_btn, LV_STATE_DISABLED);
    lv_obj_t *stop_label = lv_label_create(can_mon->stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP " Stop");
    lv_obj_set_style_text_font(stop_label, ui_common_get_font(), 0);
    lv_obj_center(stop_label);
    lv_obj_clear_flag(stop_label, LV_OBJ_FLAG_CLICKABLE);
    
    // 清除按钮
    can_mon->clear_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(can_mon->clear_btn, 85, 35);
    lv_obj_add_event_cb(can_mon->clear_btn, clear_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *clear_label = lv_label_create(can_mon->clear_btn);
    lv_label_set_text(clear_label, LV_SYMBOL_TRASH " Clear");
    lv_obj_set_style_text_font(clear_label, ui_common_get_font(), 0);
    lv_obj_center(clear_label);
    lv_obj_clear_flag(clear_label, LV_OBJ_FLAG_CLICKABLE);
    
    /* ========== 录制控制区域 ========== */
    lv_obj_t *record_cont = lv_obj_create(body);
    lv_obj_set_size(record_cont, LV_PCT(100), 50);
    lv_obj_set_style_bg_color(record_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(record_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(record_cont, 0, 0);
    
    // 录制按钮
    can_mon->record_btn = lv_btn_create(record_cont);
    lv_obj_set_size(can_mon->record_btn, 100, 35);
    lv_obj_align(can_mon->record_btn, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_add_event_cb(can_mon->record_btn, record_btn_event_cb, LV_EVENT_CLICKED, can_mon);
    lv_obj_t *record_label = lv_label_create(can_mon->record_btn);
    lv_label_set_text(record_label, LV_SYMBOL_SAVE " 开始录制");
    lv_obj_set_style_text_font(record_label, ui_common_get_font(), 0);
    lv_obj_center(record_label);
    lv_obj_clear_flag(record_label, LV_OBJ_FLAG_CLICKABLE);
    
    // CAN0复选框
    can_mon->record_can0_cb = lv_checkbox_create(record_cont);
    lv_checkbox_set_text(can_mon->record_can0_cb, "CAN0");
    lv_obj_set_style_text_font(can_mon->record_can0_cb, ui_common_get_font(), 0);
    lv_obj_align(can_mon->record_can0_cb, LV_ALIGN_LEFT_MID, 115, 0);
    lv_obj_add_state(can_mon->record_can0_cb, LV_STATE_CHECKED); // 默认选中
    
    // CAN1复选框
    can_mon->record_can1_cb = lv_checkbox_create(record_cont);
    lv_checkbox_set_text(can_mon->record_can1_cb, "CAN1");
    lv_obj_set_style_text_font(can_mon->record_can1_cb, ui_common_get_font(), 0);
    lv_obj_align(can_mon->record_can1_cb, LV_ALIGN_LEFT_MID, 215, 0);
    lv_obj_add_state(can_mon->record_can1_cb, LV_STATE_CHECKED); // 默认选中
    
    // 录制状态标签
    can_mon->record_status_label = lv_label_create(record_cont);
    lv_label_set_text(can_mon->record_status_label, "未录制");
    lv_obj_set_style_text_font(can_mon->record_status_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(can_mon->record_status_label, lv_color_hex(0x808080), 0);
    lv_obj_align(can_mon->record_status_label, LV_ALIGN_LEFT_MID, 320, 0);
    
    /* ========== CAN消息列表 ========== */
    can_mon->list = lv_list_create(body);
    lv_obj_set_size(can_mon->list, LV_PCT(100), 210); /* 减少高度给录制区域腾出空间 */
    lv_obj_set_style_bg_color(can_mon->list, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(can_mon->list, LV_SCROLLBAR_MODE_OFF); /* 隐藏列表滚动条 */
    lv_obj_set_style_border_width(can_mon->list, 0, 0);
    lv_obj_set_style_pad_left(can_mon->list, 6, 0);
    lv_obj_set_style_pad_right(can_mon->list, 6, 0);
    
    /* 添加欢迎提示信息，让用户知道报文显示区域已就绪 */
    lv_obj_t *welcome_item = lv_list_add_text(can_mon->list, "========== CAN报文监控 ==========");
    if (welcome_item) {
        lv_obj_set_style_text_font(welcome_item, ui_common_get_font(), 0);
        lv_obj_set_style_text_color(welcome_item, lv_color_hex(0x808080), 0);
    }
    lv_obj_t *ready_item = lv_list_add_text(can_mon->list, "系统已自动检测波特率并启动接收...");
    if (ready_item) {
        lv_obj_set_style_text_font(ready_item, ui_common_get_font(), 0);
        lv_obj_set_style_text_color(ready_item, lv_color_hex(0x0080FF), 0);
    }
    
    /* ========== 发送帧区域 ========== */
    lv_obj_t *send_cont = lv_obj_create(body);
    lv_obj_set_size(send_cont, LV_PCT(100), 96);
    lv_obj_set_style_bg_color(send_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(send_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(send_cont, 0, 0);
    
    // ID输入
    lv_obj_t *id_label = lv_label_create(send_cont);
    lv_label_set_text(id_label, "ID:");
    lv_obj_set_style_text_font(id_label, ui_common_get_font(), 0);
    lv_obj_align(id_label, LV_ALIGN_TOP_LEFT, 5, 8);
    
    can_mon->id_input = lv_textarea_create(send_cont);
    lv_obj_set_size(can_mon->id_input, 100, 30);
    lv_obj_align(can_mon->id_input, LV_ALIGN_TOP_LEFT, 30, 3);
    lv_textarea_set_one_line(can_mon->id_input, true);
    lv_textarea_set_text(can_mon->id_input, "123");
    lv_textarea_set_max_length(can_mon->id_input, 8);  // 支持扩展帧ID（最多8位16进制）
    lv_obj_add_event_cb(can_mon->id_input, input_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(can_mon->id_input, input_focus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(can_mon->id_input, input_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Data输入
    lv_obj_t *data_label = lv_label_create(send_cont);
    lv_label_set_text(data_label, "Data:");
    lv_obj_set_style_text_font(data_label, ui_common_get_font(), 0);
    lv_obj_align(data_label, LV_ALIGN_TOP_LEFT, 5, 43);
    
    can_mon->data_input = lv_textarea_create(send_cont);
    lv_obj_set_size(can_mon->data_input, 190, 30);
    lv_obj_align(can_mon->data_input, LV_ALIGN_TOP_LEFT, 45, 38);
    lv_textarea_set_one_line(can_mon->data_input, true);
    lv_textarea_set_text(can_mon->data_input, "01 02 03 04");
    lv_obj_add_event_cb(can_mon->data_input, input_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(can_mon->data_input, input_focus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(can_mon->data_input, input_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // 发送按钮
    lv_obj_t *send_btn = lv_btn_create(send_cont);
    lv_obj_set_size(send_btn, 70, 60);
    lv_obj_align(send_btn, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_add_event_cb(send_btn, send_btn_event_cb, LV_EVENT_CLICKED, can_mon);
    lv_obj_t *send_label = lv_label_create(send_btn);
    lv_label_set_text(send_label, LV_SYMBOL_UP "\nSend");
    lv_obj_set_style_text_font(send_label, ui_common_get_font(), 0);
    lv_obj_center(send_label);
    lv_obj_clear_flag(send_label, LV_OBJ_FLAG_CLICKABLE);
    
    // 创建接收队列（帧级）
    if (!s_rx_q) {
        s_rx_q = frame_queue_create(RX_QUEUE_CAPACITY);
    }

    // 创建更新定时器
    can_mon->update_timer = lv_timer_create(update_stats_timer_cb, 100, NULL);
    
    // 保存全局指针
    g_can_monitor = can_mon;
    
    /* ========== 软键盘（全局） ========== */
    can_mon->kb_cont = lv_obj_create(can_mon->screen);
    lv_disp_t *disp = lv_disp_get_default();
    int32_t ver_res = disp ? lv_disp_get_ver_res(disp) : 600;
    int32_t kb_h = (ver_res * 45) / 100; /* 约45%屏高，更易点按 */
    if (kb_h < 240) kb_h = 240;
    if (kb_h > 340) kb_h = 340;
    lv_obj_set_size(can_mon->kb_cont, LV_PCT(100), kb_h);
    lv_obj_align(can_mon->kb_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(can_mon->kb_cont, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_border_width(can_mon->kb_cont, 0, 0);
    lv_obj_add_flag(can_mon->kb_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(can_mon->kb_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(can_mon->kb_cont, LV_FLEX_FLOW_COLUMN);

    /* 预览输入区域 */
    can_mon->kb_preview = lv_textarea_create(can_mon->kb_cont);
    lv_obj_set_size(can_mon->kb_preview, LV_PCT(100), 36);
    lv_textarea_set_one_line(can_mon->kb_preview, true);
    lv_textarea_set_placeholder_text(can_mon->kb_preview, "Current input...");
    lv_obj_clear_flag(can_mon->kb_preview, LV_OBJ_FLAG_CLICKABLE);

    /* 隐藏按钮 */
    can_mon->kb_hide_btn = lv_btn_create(can_mon->kb_cont);
    lv_obj_set_size(can_mon->kb_hide_btn, 60, 32);
    lv_obj_align(can_mon->kb_hide_btn, LV_ALIGN_TOP_RIGHT, -6, 2);
    lv_obj_add_event_cb(can_mon->kb_hide_btn, kb_hide_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *kb_hide_label = lv_label_create(can_mon->kb_hide_btn);
    lv_label_set_text(kb_hide_label, "Hide");
    lv_obj_center(kb_hide_label);

    can_mon->kb = lv_keyboard_create(can_mon->kb_cont);
    lv_obj_set_size(can_mon->kb, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(can_mon->kb, 1);
    /* 放大键帽与字体，增大间距，便于点按 */
    lv_obj_set_style_text_font(can_mon->kb, ui_common_get_font(), LV_PART_ITEMS);
    lv_obj_set_style_pad_all(can_mon->kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_row(can_mon->kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_column(can_mon->kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_min_height(can_mon->kb, 48, LV_PART_ITEMS);
    lv_obj_set_style_pad_gap(can_mon->kb, 6, LV_PART_MAIN);
    lv_obj_center(can_mon->kb);
    lv_keyboard_set_mode(can_mon->kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    
    // 同步当前波特率到UI下拉框
    uint32_t current_bitrate0 = 0, current_bitrate1 = 0;
    can_handler_get_bitrate_dual(&current_bitrate0, &current_bitrate1);
    
    if (current_bitrate0 > 0) {
        // 根据当前波特率设置下拉框选中项
        uint16_t sel_idx0 = 2; // 默认500000
        if (current_bitrate0 == 125000) sel_idx0 = 0;
        else if (current_bitrate0 == 250000) sel_idx0 = 1;
        else if (current_bitrate0 == 500000) sel_idx0 = 2;
        else if (current_bitrate0 == 1000000) sel_idx0 = 3;
        lv_dropdown_set_selected(can_mon->bitrate0_dd, sel_idx0);
        log_info("同步CAN0波特率: %u bps -> 下拉框索引 %u", current_bitrate0, sel_idx0);
    }
    
    if (current_bitrate1 > 0) {
        uint16_t sel_idx1 = 2; // 默认500000
        if (current_bitrate1 == 125000) sel_idx1 = 0;
        else if (current_bitrate1 == 250000) sel_idx1 = 1;
        else if (current_bitrate1 == 500000) sel_idx1 = 2;
        else if (current_bitrate1 == 1000000) sel_idx1 = 3;
        lv_dropdown_set_selected(can_mon->bitrate1_dd, sel_idx1);
        log_info("同步CAN1波特率: %u bps -> 下拉框索引 %u", current_bitrate1, sel_idx1);
    }
    
    /* 注册UI回调到CAN帧分发器 */
    extern void can_frame_dispatcher_register_ui_callback(can_frame_callback_t callback, void *user_data);
    can_frame_dispatcher_register_ui_callback(can_frame_received_callback, NULL);
    
    log_info("CAN monitor UI created successfully");
    return can_mon;
}

/**
 * @brief 销毁CAN监控界面
 */
void ui_can_monitor_destroy(ui_can_monitor_t *can_mon)
{
    if (!can_mon) {
        return;
    }
    
    log_info("Destroying CAN monitor UI...");
    
    /* 注销UI回调 */
    extern void can_frame_dispatcher_register_ui_callback(can_frame_callback_t callback, void *user_data);
    can_frame_dispatcher_register_ui_callback(NULL, NULL);
    
    // 1. 清除全局指针（防止回调函数使用）
    if (g_can_monitor == can_mon) {
        g_can_monitor = NULL;
    }
    
    // 2. 删除定时器
    if (can_mon->update_timer) {
        lv_timer_del(can_mon->update_timer);
        can_mon->update_timer = NULL;
    }
    
    // 3. 停止CAN
    if (can_handler_is_running()) {
        log_info("Stopping CAN...");
        can_handler_stop();
        can_handler_deinit();
    }
    
    // 注意：不要手动删除screen，LVGL会在加载新屏幕时自动清理
    
    // 5. 释放接收队列
    if (s_rx_q) {
        frame_queue_destroy(s_rx_q);
        s_rx_q = NULL;
    }
    
    // 6. 释放内存
    free(can_mon);
    
    log_info("CAN monitor UI destroyed");
}

/**
 * @brief 清除CAN消息列表（异步调用）
 */
static void clear_messages_cb(void *user_data)
{
    (void)user_data;
    
    if (g_can_monitor && g_can_monitor->list) {
        lv_obj_clean(g_can_monitor->list);
        log_info("CAN消息列表已清除");
    }
}

void ui_can_monitor_clear_messages_async(void)
{
    lv_async_call(clear_messages_cb, NULL);
}
