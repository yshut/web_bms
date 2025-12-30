/**
 * @file main.c
 * @brief LVGL应用主程序 - T113-S3工业控制
 * @details 替代QT实现的主控制程序
 */

#include "lvgl.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "drivers/display_drv.h"
#include "drivers/touch_drv.h"
#include "ui/ui_common.h"
#include "ui/ui_home.h"
#include "logic/app_manager.h"
#include "logic/can_handler.h"
#include "logic/can_recorder.h"
#include "logic/can_frame_dispatcher.h"
#include "logic/ws_client.h"
#include "logic/ws_command_handler.h"
#include "logic/ui_remote_control.h"
#include "logic/file_transfer_progress.h"
#include "logic/hardware_monitor.h"
#include "utils/logger.h"

/* 全局变量 */
// 注意：信号处理函数里只能做 async-signal-safe 的事情
// 用 sig_atomic_t 作为跨信号/线程的退出标志，避免未定义行为
static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_exit_requested = 0;
static lv_disp_t *g_disp = NULL;
static lv_indev_t *g_touch_indev = NULL;

/* 定时器tick计数（每1ms增加） */
static uint32_t g_tick_ms = 0;

/**
 * @brief 信号处理函数
 */
static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        g_exit_requested = 1;
    }
}

/**
 * @brief tick更新线程
 */
static void* tick_thread(void *arg)
{
    (void)arg;
    
    while (g_running) {
        usleep(1000);  // 1ms
        g_tick_ms++;
        lv_tick_inc(1);
    }
    
    return NULL;
}

/**
 * @brief 初始化LVGL
 */
static int init_lvgl(void)
{
    /* 初始化LVGL */
    lv_init();
    
    // LVGL 8.x 不需要 lv_tick_set_cb
    // tick 在 tick_thread 中通过 lv_tick_inc() 更新
    
    log_info("LVGL初始化完成");
    return 0;
}

/**
 * @brief 初始化显示驱动
 */
static int init_display(void)
{
    /* 初始化framebuffer显示驱动 */
    g_disp = display_drv_init();
    if (g_disp == NULL) {
        log_error("显示驱动初始化失败");
        return -1;
    }
    
    log_info("显示驱动初始化成功 (分辨率: %dx%d)", 
             lv_disp_get_hor_res(g_disp), 
             lv_disp_get_ver_res(g_disp));
    
    return 0;
}

/**
 * @brief 初始化触摸驱动
 */
static int init_touch(void)
{
    /* 初始化evdev触摸驱动 */
    g_touch_indev = touch_drv_init();
    if (g_touch_indev == NULL) {
        log_error("触摸驱动初始化失败");
        return -1;
    }
    
    log_info("触摸驱动初始化成功");
    return 0;
}

/**
 * @brief 初始化UI界面
 */
static int init_ui(void)
{
    /* 初始化通用UI样式 */
    ui_common_init();
    
    /* 创建并显示主界面 */
    ui_home_t *home = ui_home_create();
    if (home == NULL) {
        log_error("主界面创建失败");
        return -1;
    }
    
    /* 注册主界面实例（用于WebSocket状态更新） */
    extern void ui_home_register_instance(ui_home_t *ui);
    ui_home_register_instance(home);
    
    /* 加载主界面屏幕 */
    lv_scr_load(home->screen);
    
    log_info("UI界面初始化成功");
    return 0;
}

/**
 * @brief 初始化CAN总线（双通道后台接收）
 */
static int init_can_system(void)
{
    /* 初始化双通道CAN（can0和can1） */
    log_info("初始化CAN双通道...");
    
    // 默认波特率：500Kbps
    if (can_handler_init_dual(500000, 500000) < 0) {
        log_warn("CAN双通道初始化失败（可能是硬件不支持）");
        // 不返回错误，允许应用继续运行
        return 0;
    }
    
    /* 初始化CAN帧缓冲区（用于WebSocket查询）*/
    extern int can_frame_buffer_init(int max_frames);
    if (can_frame_buffer_init(500) < 0) {
        log_error("CAN帧缓冲区初始化失败");
        return -1;
    }
    
    /* 初始化录制器 */
    can_recorder_config_t recorder_config = {
        .record_can0 = true,
        .record_can1 = true,
        .max_file_size = 40 * 1024 * 1024,  // 40MB
        .flush_interval_ms = 200,           // 200ms
    };
    strncpy(recorder_config.record_dir, "/mnt/SDCARD/can_records", 
            sizeof(recorder_config.record_dir) - 1);
    
    if (can_recorder_init(&recorder_config) < 0) {
        log_error("CAN录制器初始化失败");
        return -1;
    }
    
    /* 注册CAN帧分发器（会自动分发到录制器、WebSocket、UI、缓冲区） */
    can_handler_register_callback(can_frame_dispatcher_callback, NULL);
    
    /* 启动CAN接收（后台持续接收）*/
    if (can_handler_start() < 0) {
        log_error("CAN接收启动失败");
        return -1;
    }
    
    log_info("CAN系统初始化成功（双通道后台接收已启动）");
    return 0;
}

/**
 * @brief 清理CAN系统
 */
static void cleanup_can_system(void)
{
    log_info("清理CAN系统...");
    
    // 停止录制
    if (can_recorder_is_recording()) {
        can_recorder_stop();
    }
    
    // 停止CAN接收
    can_handler_stop();
    
    // 清理资源
    can_recorder_deinit();
    can_handler_deinit();
    
    // 清理帧缓冲区
    extern void can_frame_buffer_deinit(void);
    can_frame_buffer_deinit();
    
    log_info("CAN系统已清理");
}

/**
 * @brief WebSocket连接状态回调
 */
static void on_websocket_state_changed(bool connected, const char *host, uint16_t port, void *user_data)
{
    (void)user_data;
    
    if (connected) {
        log_info("WebSocket已连接: %s:%d", host, port);
        
        // 发送设备上线事件
        char event_data[256];
        snprintf(event_data, sizeof(event_data),
                "{\"device_id\":\"%s\",\"timestamp\":%ld}",
                ws_client_get_device_id(),
                time(NULL));
        ws_client_publish_event("device_online", event_data);
        
        // 更新主界面状态显示
        extern void ui_home_update_server_status_async(bool connected, const char *host, uint16_t port);
        ui_home_update_server_status_async(connected, host, port);
        
    } else {
        log_warn("WebSocket已断开: %s:%d", host, port);
        
        // 更新主界面状态显示
        extern void ui_home_update_server_status_async(bool connected, const char *host, uint16_t port);
        ui_home_update_server_status_async(connected, host, port);
    }
}

/**
 * @brief 初始化WebSocket客户端
 */
static int init_websocket(void)
{
    log_info("初始化WebSocket客户端...");
    
    // 从配置文件读取服务器地址
    // 注意：0.0.0.0 仅用于服务端“监听所有网卡”，客户端不能用 0.0.0.0 去连接。
    // 如果未提供配置文件，建议你把主机IP写入 /mnt/UDISK/ws_config.txt（推荐）或 /mnt/SDCARD/ws_config.txt：
    // 第一行: SERVER_IP
    // 第二行: SERVER_PORT（可选，默认5052）
    char host[128] = "192.168.100.1";   // 默认值（可按你的PC实际IP修改）
    uint16_t port = 5052;
    
    const char *cfg_paths[] = {
        "/mnt/UDISK/ws_config.txt",   // 优先UDISK：更常用、易维护
        "/mnt/SDCARD/ws_config.txt",
        NULL
    };
    bool loaded = false;
    for (int i = 0; cfg_paths[i]; i++) {
        FILE *fp = fopen(cfg_paths[i], "r");
        if (!fp) continue;
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0] != '\0') {
                strncpy(host, line, sizeof(host) - 1);
                host[sizeof(host) - 1] = '\0';
            }
        }
        if (fgets(line, sizeof(line), fp)) {
            int p = atoi(line);
            if (p > 0 && p <= 65535) port = (uint16_t)p;
        }
        fclose(fp);
        loaded = true;
        log_info("加载WebSocket配置(%s): %s:%d", cfg_paths[i], host, port);
        break;
    }
    if (!loaded) {
        log_warn("未找到WebSocket配置文件，使用默认配置: %s:%d", host, port);
        log_warn("请在 /mnt/UDISK/ws_config.txt 写入两行: <SERVER_IP> 和 <PORT(可选)>，然后重启应用");
    }
    
    // 配置WebSocket客户端
    ws_config_t config = {
        .port = port,
        .use_ssl = false,
        .reconnect_interval_ms = 4000,
        .keepalive_interval_s = 20,
    };
    strncpy(config.host, host, sizeof(config.host) - 1);
    config.host[sizeof(config.host) - 1] = '\0';
    strncpy(config.path, "/ws", sizeof(config.path) - 1);
    config.path[sizeof(config.path) - 1] = '\0';
    
    // 初始化UI远程控制模块
    if (ui_remote_init() < 0) {
        log_error("UI远程控制模块初始化失败");
        return -1;
    }
    
    // 初始化文件传输进度跟踪
    if (file_transfer_progress_init() < 0) {
        log_error("文件传输进度跟踪初始化失败");
        return -1;
    }
    
    // 初始化命令处理器
    if (ws_command_handler_init() < 0) {
        log_error("WebSocket命令处理器初始化失败");
        return -1;
    }
    
    // 初始化客户端
    if (ws_client_init(&config) < 0) {
        log_error("WebSocket客户端初始化失败");
        return -1;
    }
    
    // 注册状态回调
    ws_client_register_state_callback(on_websocket_state_changed, NULL);
    
    // 启动连接
    if (ws_client_start() < 0) {
        log_error("WebSocket客户端启动失败");
        return -1;
    }
    
    log_info("WebSocket客户端初始化完成，设备ID: %s", ws_client_get_device_id());
    
    return 0;
}

/**
 * @brief 清理WebSocket系统
 */
static void cleanup_websocket(void)
{
    log_info("清理WebSocket系统...");
    
    ws_client_stop();
    ws_client_deinit();
    ws_command_handler_deinit();
    file_transfer_progress_deinit();
    ui_remote_deinit();
    
    log_info("WebSocket系统已清理");
}

/**
 * @brief 初始化硬件监控系统
 */
static int init_hardware_monitor(void)
{
    log_info("初始化硬件监控系统...");
    
    hw_monitor_config_t config = {
        .interval_ms = 2000,              // 每2秒更新一次
        .enable_can_monitor = true,
        .enable_storage_monitor = true,
        .enable_system_monitor = true,
        .enable_network_monitor = true,
        .enable_auto_report = true,
        .report_interval_ms = 10000,      // 每10秒上报一次
    };
    
    if (hw_monitor_init(&config) < 0) {
        log_error("硬件监控初始化失败");
        return -1;
    }
    
    if (hw_monitor_start() < 0) {
        log_error("硬件监控启动失败");
        return -1;
    }
    
    log_info("硬件监控系统初始化完成");
    return 0;
}

/**
 * @brief 清理硬件监控系统
 */
static void cleanup_hardware_monitor(void)
{
    log_info("清理硬件监控系统...");
    hw_monitor_stop();
    hw_monitor_deinit();
    log_info("硬件监控系统已清理");
}

/**
 * @brief 主函数
 */
int main(int argc, char *argv[])
{
    int ret;
    pthread_t tick_tid;
    
    (void)argc;  // 未使用的参数
    (void)argv;  // 未使用的参数
    
    /* 初始化日志系统 */
    log_init("/tmp/lvgl_app.log", LOG_LEVEL_DEBUG);
    log_info("========================================");
    log_info("T113-S3 LVGL工业控制应用启动");
    log_info("版本: v1.0.0");
    log_info("编译时间: %s %s", __DATE__, __TIME__);
    log_info("========================================");
    
    /* 注册信号处理（不使用 SA_RESTART，尽量让 usleep/select 尽快被中断返回） */
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
    }
    
    /* 初始化LVGL */
    ret = init_lvgl();
    if (ret < 0) {
        log_error("LVGL初始化失败");
        return -1;
    }
    
    /* 初始化显示驱动 */
    ret = init_display();
    if (ret < 0) {
        log_error("显示驱动初始化失败");
        return -1;
    }
    
    /* 初始化触摸驱动 */
    ret = init_touch();
    if (ret < 0) {
        log_warn("触摸驱动初始化失败，将无法使用触摸功能");
        // 继续运行，可以用USB鼠标代替
    }
    
    /* 启动tick线程 */
    ret = pthread_create(&tick_tid, NULL, tick_thread, NULL);
    if (ret != 0) {
        log_error("创建tick线程失败");
        return -1;
    }
    
    /* 初始化应用管理器（业务逻辑层） */
    ret = app_manager_init();
    if (ret < 0) {
        log_error("应用管理器初始化失败");
        return -1;
    }
    log_info("应用管理器初始化完成");
    
    /* 初始化CAN系统（双通道后台接收） */
    ret = init_can_system();
    if (ret < 0) {
        log_warn("CAN系统初始化失败，CAN功能将不可用");
        // 不中止程序，允许其他功能继续使用
    }
    
    /* 初始化WebSocket客户端（远程控制） */
    ret = init_websocket();
    if (ret < 0) {
        log_warn("WebSocket客户端初始化失败，远程控制功能将不可用");
        // 不中止程序，允许其他功能继续使用
    }
    
    /* 初始化硬件监控系统（在WebSocket之后启动，以便能够上报状态） */
    ret = init_hardware_monitor();
    if (ret < 0) {
        log_warn("硬件监控系统初始化失败，硬件状态监控功能将不可用");
        // 不中止程序，允许其他功能继续使用
    }
    
    /* 初始化UI */
    ret = init_ui();
    if (ret < 0) {
        log_error("UI初始化失败");
        return -1;
    }
    
    log_info("应用初始化完成，进入主循环");
    
    /* 主循环 */
    while (g_running) {
        // 收到退出请求后，在主线程里打印日志并触发退出（避免在信号处理函数里调用 log_* 导致死锁/阻塞）
        if (g_exit_requested) {
            log_info("收到退出信号，准备退出...");
            g_running = 0;
            break;
        }
        /* 让LVGL处理任务 */
        uint32_t sleep_ms = lv_timer_handler();
        
        /* 休眠以降低CPU占用率 */
        // 上限：避免 sleep_ms 太大导致退出不及时
        if (sleep_ms > 50) sleep_ms = 50;
        if (sleep_ms > 0) {
            // 若被信号中断（EINTR），下一轮会立刻检查 g_exit_requested
            (void)usleep(sleep_ms * 1000);
        }
    }
    
    log_info("应用正在退出...");
    
    /* 清理硬件监控系统 */
    cleanup_hardware_monitor();
    
    /* 清理WebSocket系统 */
    cleanup_websocket();
    
    /* 清理CAN系统 */
    cleanup_can_system();
    
    /* 清理资源 */
    app_manager_deinit();
    
    /* 等待tick线程退出 */
    pthread_join(tick_tid, NULL);
    
    /* 清理LVGL */
    lv_deinit();
    
    log_info("应用已退出");
    log_deinit();
    
    return 0;
}

