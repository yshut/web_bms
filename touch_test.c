/**
 * @file touch_test.c
 * @brief 触摸功能测试程序 - 显示触摸坐标和状态
 * @details 这是一个简化的测试程序，用于验证触摸驱动是否正常工作
 */

#include "lvgl.h"
#include "drivers/display_drv.h"
#include "drivers/touch_drv.h"
#include "utils/logger.h"
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>

/* 全局变量 */
static volatile bool g_running = true;
static lv_disp_t *g_disp = NULL;
static lv_indev_t *g_touch_indev = NULL;

/* UI 对象 */
static lv_obj_t *g_label_title = NULL;
static lv_obj_t *g_label_coord = NULL;
static lv_obj_t *g_label_state = NULL;
static lv_obj_t *g_circle = NULL;
static lv_obj_t *g_counter_label = NULL;
static int g_touch_count = 0;

/**
 * @brief 信号处理
 */
static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n收到退出信号...\n");
        g_running = false;
    }
}

/**
 * @brief Tick 线程
 */
static void* tick_thread(void *arg)
{
    while (g_running) {
        usleep(1000);
        lv_tick_inc(1);
    }
    return NULL;
}

/**
 * @brief 输入设备回调 - 用于显示触摸信息
 */
static void indev_read_wrapper(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    /* 调用原始的触摸驱动回调 */
    extern void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
    
    /* 获取触摸设备的原始读取函数 */
    if (g_touch_indev && g_touch_indev->driver && g_touch_indev->driver->read_cb) {
        g_touch_indev->driver->read_cb(drv, data);
    }
    
    /* 更新显示 */
    static char coord_buf[64];
    static char state_buf[64];
    static lv_indev_state_t last_state = LV_INDEV_STATE_RELEASED;
    
    /* 更新坐标显示 */
    snprintf(coord_buf, sizeof(coord_buf), "坐标: X=%d, Y=%d", 
             data->point.x, data->point.y);
    if (g_label_coord) {
        lv_label_set_text(g_label_coord, coord_buf);
    }
    
    /* 更新状态显示 */
    if (data->state == LV_INDEV_STATE_PRESSED) {
        snprintf(state_buf, sizeof(state_buf), "状态: 按下 (PRESSED)");
        if (g_label_state) {
            lv_obj_set_style_text_color(g_label_state, lv_color_hex(0x22C55E), 0);
        }
        
        /* 移动圆圈到触摸位置 */
        if (g_circle) {
            lv_obj_set_pos(g_circle, data->point.x - 15, data->point.y - 15);
            lv_obj_clear_flag(g_circle, LV_OBJ_FLAG_HIDDEN);
        }
        
        /* 统计触摸次数（按下时计数） */
        if (last_state == LV_INDEV_STATE_RELEASED) {
            g_touch_count++;
            static char count_buf[64];
            snprintf(count_buf, sizeof(count_buf), "触摸次数: %d", g_touch_count);
            if (g_counter_label) {
                lv_label_set_text(g_counter_label, count_buf);
            }
        }
    } else {
        snprintf(state_buf, sizeof(state_buf), "状态: 释放 (RELEASED)");
        if (g_label_state) {
            lv_obj_set_style_text_color(g_label_state, lv_color_hex(0xEF4444), 0);
        }
        
        /* 隐藏圆圈 */
        if (g_circle) {
            lv_obj_add_flag(g_circle, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    if (g_label_state) {
        lv_label_set_text(g_label_state, state_buf);
    }
    
    last_state = data->state;
}

/**
 * @brief 创建测试UI
 */
static void create_test_ui(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    
    /* 标题 */
    g_label_title = lv_label_create(screen);
    lv_label_set_text(g_label_title, "触摸测试程序");
    lv_obj_set_style_text_font(g_label_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_label_title, lv_color_hex(0x2196F3), 0);
    lv_obj_align(g_label_title, LV_ALIGN_TOP_MID, 0, 20);
    
    /* 说明文字 */
    lv_obj_t *label_hint = lv_label_create(screen);
    lv_label_set_text(label_hint, "请触摸屏幕任意位置");
    lv_obj_set_style_text_font(label_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(label_hint, LV_ALIGN_TOP_MID, 0, 60);
    
    /* 坐标显示 */
    g_label_coord = lv_label_create(screen);
    lv_label_set_text(g_label_coord, "坐标: X=0, Y=0");
    lv_obj_set_style_text_font(g_label_coord, &lv_font_montserrat_20, 0);
    lv_obj_align(g_label_coord, LV_ALIGN_CENTER, 0, -40);
    
    /* 状态显示 */
    g_label_state = lv_label_create(screen);
    lv_label_set_text(g_label_state, "状态: 释放 (RELEASED)");
    lv_obj_set_style_text_font(g_label_state, &lv_font_montserrat_20, 0);
    lv_obj_align(g_label_state, LV_ALIGN_CENTER, 0, 0);
    
    /* 触摸次数 */
    g_counter_label = lv_label_create(screen);
    lv_label_set_text(g_counter_label, "触摸次数: 0");
    lv_obj_set_style_text_font(g_counter_label, &lv_font_montserrat_16, 0);
    lv_obj_align(g_counter_label, LV_ALIGN_CENTER, 0, 40);
    
    /* 跟随触摸的圆圈 */
    g_circle = lv_obj_create(screen);
    lv_obj_set_size(g_circle, 30, 30);
    lv_obj_set_style_radius(g_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_circle, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(g_circle, LV_OPA_70, 0);
    lv_obj_set_style_border_width(g_circle, 2, 0);
    lv_obj_set_style_border_color(g_circle, lv_color_hex(0xFF0000), 0);
    lv_obj_add_flag(g_circle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_circle, LV_OBJ_FLAG_CLICKABLE);
    
    /* 屏幕分辨率显示 */
    lv_obj_t *label_res = lv_label_create(screen);
    char res_buf[64];
    snprintf(res_buf, sizeof(res_buf), "屏幕分辨率: %dx%d", 
             lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_label_set_text(label_res, res_buf);
    lv_obj_set_style_text_font(label_res, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label_res, lv_color_hex(0x666666), 0);
    lv_obj_align(label_res, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    /* 退出提示 */
    lv_obj_t *label_exit = lv_label_create(screen);
    lv_label_set_text(label_exit, "按 Ctrl+C 退出");
    lv_obj_set_style_text_font(label_exit, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label_exit, lv_color_hex(0x999999), 0);
    lv_obj_align(label_exit, LV_ALIGN_BOTTOM_MID, 0, -5);
}

/**
 * @brief 主函数
 */
int main(int argc, char *argv[])
{
    pthread_t tick_tid;
    
    printf("========================================\n");
    printf("LVGL 触摸测试程序\n");
    printf("========================================\n\n");
    
    /* 初始化日志 */
    log_init("/tmp/touch_test.log", LOG_LEVEL_DEBUG);
    
    /* 信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* 初始化 LVGL */
    lv_init();
    printf("[OK] LVGL 初始化完成\n");
    
    /* 初始化显示驱动 */
    g_disp = display_drv_init();
    if (g_disp == NULL) {
        printf("[ERROR] 显示驱动初始化失败\n");
        return -1;
    }
    printf("[OK] 显示驱动初始化完成 (%dx%d)\n", 
           lv_disp_get_hor_res(g_disp), lv_disp_get_ver_res(g_disp));
    
    /* 初始化触摸驱动 */
    g_touch_indev = touch_drv_init();
    if (g_touch_indev == NULL) {
        printf("[ERROR] 触摸驱动初始化失败\n");
        printf("[HINT] 请检查:\n");
        printf("  1. 触摸设备是否存在 (ls -l /dev/input/event*)\n");
        printf("  2. 设备路径是否正确 (drivers/touch_drv.c)\n");
        printf("  3. 设备权限是否正确 (sudo chmod 666 /dev/input/event*)\n");
        return -1;
    }
    printf("[OK] 触摸驱动初始化完成\n");
    
    /* 启动 tick 线程 */
    pthread_create(&tick_tid, NULL, tick_thread, NULL);
    printf("[OK] Tick 线程启动\n");
    
    /* 创建测试UI */
    create_test_ui();
    printf("[OK] 测试UI创建完成\n\n");
    
    printf("请触摸屏幕进行测试...\n");
    printf("如果看到坐标和状态更新，说明触摸驱动工作正常。\n");
    printf("如果没有响应，请查看 /tmp/touch_test.log 日志文件。\n\n");
    
    /* 主循环 */
    while (g_running) {
        uint32_t sleep_ms = lv_timer_handler();
        usleep(sleep_ms * 1000);
    }
    
    printf("\n正在退出...\n");
    
    /* 清理 */
    pthread_join(tick_tid, NULL);
    lv_deinit();
    log_deinit();
    
    printf("触摸测试程序已退出。\n");
    printf("触摸总次数: %d\n", g_touch_count);
    
    return 0;
}

