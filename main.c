/**
 * @file main.c
 * @brief T113-S3 无屏幕守护进程
 *
 * 功能：CAN 数据采集 → MQTT 上报 + 本地 HTTP 配置界面
 * 访问：http://<设备IP>:8080/
 */

#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "logic/app_manager.h"
#include "logic/can_handler.h"
#include "logic/can_recorder.h"
#include "logic/can_frame_dispatcher.h"
#include "logic/remote_transport.h"
#include "logic/ws_command_handler.h"
#include "logic/ui_remote_control.h"
#include "logic/file_transfer_progress.h"
#include "logic/hardware_monitor.h"
#include "logic/can_mqtt_engine.h"
#include "logic/device_http_server.h"
#include "utils/logger.h"
#include "utils/app_config.h"
#include "utils/net_manager.h"

static volatile sig_atomic_t g_running        = 1;
static volatile sig_atomic_t g_exit_requested = 0;

static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        g_exit_requested = 1;
    }
}

/* ------------------------------------------------------------------ */
/*  CAN 系统                                                             */
/* ------------------------------------------------------------------ */

static int init_can_system(void)
{
    log_info("初始化 CAN 双通道 (can0=%u bps, can1=%u bps)...",
             g_app_config.can0_bitrate, g_app_config.can1_bitrate);

    if (can_handler_init_dual(g_app_config.can0_bitrate,
                              g_app_config.can1_bitrate) < 0) {
        log_warn("CAN 双通道初始化失败（可能硬件不支持），CAN 功能不可用");
        return 0;
    }

    extern int can_frame_buffer_init(int max_frames);
    if (can_frame_buffer_init(500) < 0) {
        log_error("CAN 帧缓冲区初始化失败");
        return -1;
    }

    can_recorder_config_t rec = {
        .record_can0        = true,
        .record_can1        = true,
        .max_file_size      = (uint64_t)g_app_config.can_record_max_mb * 1024 * 1024,
        .max_total_bytes    = 8ULL * 1024 * 1024 * 1024,  /* 8 GB 总上限 */
        .flush_interval_ms  = (int)g_app_config.can_record_flush_ms,
    };
    strncpy(rec.record_dir, g_app_config.can_record_dir,
            sizeof(rec.record_dir) - 1);

    if (can_recorder_init(&rec) < 0) {
        log_error("CAN 录制器初始化失败");
        return -1;
    }

    /* 自动启动持续录制 */
    if (can_recorder_start() < 0) {
        log_warn("CAN 录制器自动启动失败，继续运行");
    } else {
        log_info("CAN 录制已自动启动 (单文件 %u MB，总上限 8 GB)",
                 g_app_config.can_record_max_mb);
    }

    can_handler_register_callback(can_frame_dispatcher_callback, NULL);

    if (can_handler_start() < 0) {
        log_error("CAN 接收启动失败");
        return -1;
    }

    log_info("CAN 系统初始化完成");
    return 0;
}

static void cleanup_can_system(void)
{
    if (can_recorder_is_recording()) can_recorder_stop();
    can_handler_stop();
    can_recorder_deinit();
    can_handler_deinit();
    extern void can_frame_buffer_deinit(void);
    can_frame_buffer_deinit();
    log_info("CAN 系统已清理");
}

/* ------------------------------------------------------------------ */
/*  远程传输（MQTT / WebSocket）                                          */
/* ------------------------------------------------------------------ */

static void on_transport_state(bool connected, const char *host,
                               uint16_t port, void *user_data)
{
    (void)user_data;
    if (connected) {
        log_info("远程传输已连接(%s): %s:%u",
                 remote_transport_mode_to_string(remote_transport_get_mode()),
                 host, (unsigned)port);
        char ev[256];
        snprintf(ev, sizeof(ev),
                 "{\"device_id\":\"%s\",\"timestamp\":%ld}",
                 remote_transport_get_device_id(), time(NULL));
        remote_transport_publish_event("device_online", ev);
    } else {
        log_warn("远程传输已断开(%s): %s:%u",
                 remote_transport_mode_to_string(remote_transport_get_mode()),
                 host, (unsigned)port);
    }
}

static int init_remote_transport(void)
{
    log_info("初始化远程传输层，模式=%s",
             app_config_transport_mode_to_string(g_app_config.transport_mode));

    if (ui_remote_init()              < 0) { log_error("ui_remote_init 失败");               return -1; }
    if (file_transfer_progress_init() < 0) { log_error("file_transfer_progress_init 失败");  return -1; }
    if (ws_command_handler_init()     < 0) { log_error("ws_command_handler_init 失败");      return -1; }

    if (remote_transport_init_from_app_config() < 0) {
        log_error("远程传输层初始化失败");
        return -1;
    }
    remote_transport_register_state_callback(on_transport_state, NULL);

    if (remote_transport_start() < 0) {
        log_error("远程传输层启动失败");
        return -1;
    }

    log_info("远程传输层已启动，设备ID: %s", remote_transport_get_device_id());
    return 0;
}

static void cleanup_remote_transport(void)
{
    remote_transport_stop();
    remote_transport_deinit();
    ws_command_handler_deinit();
    file_transfer_progress_deinit();
    ui_remote_deinit();
    log_info("远程传输系统已清理");
}

/* ------------------------------------------------------------------ */
/*  硬件监控                                                              */
/* ------------------------------------------------------------------ */

static int init_hardware_monitor(void)
{
    hw_monitor_config_t cfg = {
        .interval_ms            = g_app_config.hw_interval_ms,
        .enable_can_monitor     = true,
        .enable_storage_monitor = true,
        .enable_system_monitor  = true,
        .enable_network_monitor = true,
        .enable_auto_report     = g_app_config.hw_auto_report,
        .report_interval_ms     = g_app_config.hw_report_interval_ms,
    };
    if (hw_monitor_init(&cfg) < 0) { log_error("硬件监控初始化失败"); return -1; }
    if (hw_monitor_start()    < 0) { log_error("硬件监控启动失败");   return -1; }
    log_info("硬件监控已启动");
    return 0;
}

static void cleanup_hardware_monitor(void)
{
    hw_monitor_stop();
    hw_monitor_deinit();
    log_info("硬件监控已清理");
}

/* ------------------------------------------------------------------ */
/*  主函数                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* 加载配置 */
    char cfg_path[256]     = {0};
    char net_cfg_path[256] = {0};
    int cfg_loaded     = app_config_load_best(cfg_path, sizeof(cfg_path));
    int net_cfg_loaded = app_config_load_network_best(net_cfg_path, sizeof(net_cfg_path));

    /* 初始化日志 */
    log_level_t lvl = LOG_LEVEL_INFO;
    if      (g_app_config.log_level == APP_LOG_DEBUG) lvl = LOG_LEVEL_DEBUG;
    else if (g_app_config.log_level == APP_LOG_WARN)  lvl = LOG_LEVEL_WARN;
    else if (g_app_config.log_level == APP_LOG_ERROR) lvl = LOG_LEVEL_ERROR;
    log_init(g_app_config.log_file[0] ? g_app_config.log_file : NULL, lvl);

    log_info("========================================");
    log_info("T113-S3 守护进程启动（无屏幕模式）");
    log_info("编译时间: %s %s", __DATE__, __TIME__);
    log_info("========================================");

    if (cfg_loaded == 0) {
        log_info("已加载配置: %s", cfg_path);
    } else {
        log_warn("未找到配置文件，使用默认值");
        char saved[256] = {0};
        if (app_config_save_best(saved, sizeof(saved)) == 0)
            log_info("已生成默认配置: %s", saved);
    }
    if (net_cfg_loaded == 0) {
        log_info("已加载网络配置: %s", net_cfg_path);
    } else {
        log_warn("未找到网络配置，不修改 IP");
        char saved[256] = {0};
        app_config_save_network_best(saved, sizeof(saved));
    }

    log_info("transport=%s  ws=%s:%u  mqtt=%s:%u",
             app_config_transport_mode_to_string(g_app_config.transport_mode),
             g_app_config.ws_host,   g_app_config.ws_port,
             g_app_config.mqtt_host, g_app_config.mqtt_port);
    log_info("can0=%u bps  can1=%u bps", g_app_config.can0_bitrate, g_app_config.can1_bitrate);
    log_info("net: dhcp=%s  ip=%s",
             g_app_config.net_use_dhcp ? "true" : "false",
             g_app_config.net_ip);

    /* 信号处理 */
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = signal_handler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT,  &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
    }

    /* 应用网络配置 */
    if (net_cfg_loaded == 0) {
        if (net_manager_apply_current_config() < 0)
            log_warn("应用网络配置失败，继续使用系统当前网络");
    }

    /* 初始化各子系统 */
    app_manager_init();

    if (init_can_system() < 0)
        log_warn("CAN 系统初始化失败，CAN 功能不可用");

    if (init_remote_transport() < 0)
        log_warn("远程传输初始化失败，远程控制功能不可用");

    if (init_hardware_monitor() < 0)
        log_warn("硬件监控初始化失败");

    /* CAN-MQTT 规则引擎 */
    if (can_mqtt_engine_init() == 0) {
        can_frame_dispatcher_register_engine_callback(
            can_mqtt_engine_frame_callback, NULL);
        log_info("CAN-MQTT 引擎已启动");
    } else {
        log_warn("CAN-MQTT 引擎初始化失败");
    }

    /* 板端 HTTP 配置服务器 */
    const char *dev_ip = g_app_config.net_ip[0]
                         ? g_app_config.net_ip : "192.168.100.100";
    if (device_http_server_start(8080) == 0) {
        log_info("配置网页已启动: http://%s:8080/", dev_ip);
    } else {
        log_warn("HTTP 服务器启动失败");
    }

    log_info("所有子系统已启动，进入主循环 (Ctrl+C 退出)");

    /* 主循环：每秒检查一次退出信号 */
    while (g_running) {
        if (g_exit_requested) {
            log_info("收到退出信号，准备退出...");
            g_running = 0;
            break;
        }
        sleep(1);
    }

    /* 清理 */
    log_info("正在退出...");
    device_http_server_stop();
    can_frame_dispatcher_register_engine_callback(NULL, NULL);
    can_mqtt_engine_deinit();
    cleanup_hardware_monitor();
    cleanup_remote_transport();
    cleanup_can_system();
    app_manager_deinit();

    log_info("应用已退出");
    log_deinit();
    return 0;
}
