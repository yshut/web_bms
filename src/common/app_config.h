#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// 服务器配置
#define REMOTE_SERVER_HOST "cloud.yshut.cn"
#define REMOTE_SERVER_PORT 5052
#define REMOTE_TCP_PORT 5051

// 屏幕配置
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 600
#define SCREEN_DPI 160

// CAN 配置
#define CAN_INTERFACE_1 "can0"
#define CAN_INTERFACE_2 "can1"
#define CAN_DEFAULT_BITRATE 500000

// 应用配置
#define APP_NAME "T113-S3工业控制工具"
#define APP_VERSION "1.0.0"

// 线程优先级
#define THREAD_PRIORITY_HIGH 90
#define THREAD_PRIORITY_NORMAL 50
#define THREAD_PRIORITY_LOW 10

// CPU 核心绑定
#define CPU_CORE_UI 0
#define CPU_CORE_CAN 1

// 缓冲区大小
#define CAN_BUFFER_SIZE 10000
#define WS_BUFFER_SIZE 8192
#define FILE_BUFFER_SIZE 4096

// 颜色定义 (RGB888)
#define COLOR_PRIMARY 0x22D3EE      // 青色
#define COLOR_SECONDARY 0x8AB4F8    // 蓝色
#define COLOR_SUCCESS 0x10B981      // 绿色
#define COLOR_DANGER 0xEF4444       // 红色
#define COLOR_WARNING 0xF59E0B      // 橙色
#define COLOR_BG_MAIN 0xFFFFFF      // 白色背景
#define COLOR_TEXT_PRIMARY 0x000000 // 黑色文字
#define COLOR_TEXT_SECONDARY 0x6B7280 // 灰色文字

// 应用配置结构
typedef struct {
    char server_host[64];
    uint16_t server_port;
    uint16_t tcp_port;
    uint32_t can1_bitrate;
    uint32_t can2_bitrate;
    bool fullscreen;
    bool debug_mode;
} app_config_t;

// 全局配置实例
extern app_config_t g_app_config;

// 配置函数
void app_config_init(void);
void app_config_load(const char *config_file);
void app_config_save(const char *config_file);
void app_config_set_server(const char *host, uint16_t port);
void app_config_set_can_bitrate(uint32_t can1, uint32_t can2);

#endif // APP_CONFIG_H

