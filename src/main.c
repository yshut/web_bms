#include "lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include "ui/ui_main.h"
#include "common/app_config.h"
#include "can/can_worker.h"
#include "uds/uds_handler.h"
#include "wifi/wifi_manager.h"
#include "file/file_manager.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <signal.h>

#define DISP_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10)

static volatile bool g_running = true;

// 信号处理
void signal_handler(int sig) {
    g_running = false;
}

// 时间更新线程
void* time_update_thread(void* arg) {
    while (g_running) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
        
        // 更新时间显示
        ui_main_update_time(time_str);
        
        sleep(1);
    }
    return NULL;
}

// LVGL定时器处理
uint32_t custom_tick_get(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

int main(int argc, char *argv[]) {
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("===========================================\n");
    printf("  %s v%s\n", APP_NAME, APP_VERSION);
    printf("  基于 LVGL 8.3.2\n");
    printf("===========================================\n");

    // 初始化配置
    app_config_init();
    printf("配置初始化完成\n");
    printf("服务器: %s:%d\n", g_app_config.server_host, g_app_config.server_port);

    // 初始化 LVGL
    lv_init();
    printf("LVGL 初始化完成\n");

    // 初始化帧缓冲设备
    fbdev_init();
    printf("帧缓冲设备初始化完成\n");

    // 初始化触摸屏设备
    evdev_init();
    printf("触摸屏设备初始化完成\n");

    // 创建显示缓冲区
    static lv_color_t buf1[DISP_BUF_SIZE];
    static lv_color_t buf2[DISP_BUF_SIZE];
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, DISP_BUF_SIZE);

    // 注册显示驱动
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    lv_disp_drv_register(&disp_drv);
    printf("显示驱动注册完成 (%dx%d)\n", SCREEN_WIDTH, SCREEN_HEIGHT);

    // 注册输入设备驱动
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);
    printf("输入设备驱动注册完成\n");

    // 设置自定义tick函数
    lv_tick_set_cb(custom_tick_get);

    // 初始化各个模块
    can_worker_init();
    uds_handler_init();
    wifi_manager_init();
    file_manager_init();
    printf("业务模块初始化完成\n");

    // 创建UI
    ui_main_init();
    printf("UI 创建完成\n");

    // 启动时间更新线程
    pthread_t time_thread;
    pthread_create(&time_thread, NULL, time_update_thread, NULL);
    printf("时间更新线程已启动\n");

    printf("\n应用程序已启动，进入主循环...\n\n");

    // 主循环
    while (g_running) {
        lv_timer_handler();
        usleep(5000); // 5ms
    }

    // 清理资源
    printf("\n正在清理资源...\n");
    
    // 等待线程结束
    pthread_join(time_thread, NULL);
    
    // 停止各个模块
    can_worker_stop();
    uds_handler_stop();
    wifi_manager_disconnect();
    
    printf("应用程序已退出\n");
    return 0;
}

