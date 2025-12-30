/**
 * @file hardware_monitor.h
 * @brief 硬件状态监控模块 - 监控CAN、SD卡、系统状态
 */

#ifndef HARDWARE_MONITOR_H
#define HARDWARE_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  /* for size_t */

/* 硬件状态枚举 */
typedef enum {
    HW_STATUS_NORMAL = 0,
    HW_STATUS_WARNING = 1,
    HW_STATUS_ERROR = 2,
    HW_STATUS_OFFLINE = 3
} hw_status_t;

/* CAN总线状态 */
typedef struct {
    hw_status_t status;
    char interface[16];        // can0/can1
    uint32_t bitrate;          // 波特率
    uint32_t rx_count;         // 接收帧数
    uint32_t tx_count;         // 发送帧数
    uint32_t error_count;      // 错误计数
    bool error_warning;        // 错误警告状态
    bool error_passive;        // 错误被动状态
    bool bus_off;              // 总线关闭状态
    char last_error[64];       // 最后的错误信息
} hw_can_status_t;

/* 存储设备状态 */
typedef struct {
    hw_status_t status;
    char device[64];           // 设备路径
    char mount_point[128];     // 挂载点
    uint64_t total_bytes;      // 总容量（字节）
    uint64_t free_bytes;       // 可用容量（字节）
    uint64_t used_bytes;       // 已用容量（字节）
    bool is_mounted;           // 是否已挂载
    char last_error[128];      // 最后的错误信息
} hw_storage_status_t;

/* 系统状态 */
typedef struct {
    float cpu_usage;           // CPU使用率 (0-100)
    uint64_t memory_total;     // 总内存（KB）
    uint64_t memory_used;      // 已用内存（KB）
    uint64_t memory_free;      // 可用内存（KB）
    float memory_usage;        // 内存使用率 (0-100)
    float temperature;         // 温度（摄氏度）
    uint64_t uptime_seconds;   // 运行时间（秒）
} hw_system_status_t;

/* 网络状态 */
typedef struct {
    hw_status_t status;
    char interface[16];        // eth0/wlan0
    bool is_connected;         // 是否已连接
    char ip_address[64];       // IP地址
    char mac_address[32];      // MAC地址
    uint64_t rx_bytes;         // 接收字节数
    uint64_t tx_bytes;         // 发送字节数
} hw_network_status_t;

/* 硬件监控配置 */
typedef struct {
    uint32_t interval_ms;      // 监控间隔（毫秒）
    bool enable_can_monitor;   // 启用CAN监控
    bool enable_storage_monitor; // 启用存储监控
    bool enable_system_monitor;  // 启用系统监控
    bool enable_network_monitor; // 启用网络监控
    bool enable_auto_report;   // 启用自动上报（通过WebSocket）
    uint32_t report_interval_ms; // 上报间隔（毫秒）
} hw_monitor_config_t;

/* 状态变化回调函数 */
typedef void (*hw_status_callback_t)(const char *component, hw_status_t old_status, hw_status_t new_status, void *user_data);

/**
 * @brief 初始化硬件监控
 * @param config 监控配置
 * @return 0成功，-1失败
 */
int hw_monitor_init(const hw_monitor_config_t *config);

/**
 * @brief 反初始化硬件监控
 */
void hw_monitor_deinit(void);

/**
 * @brief 启动硬件监控线程
 * @return 0成功，-1失败
 */
int hw_monitor_start(void);

/**
 * @brief 停止硬件监控线程
 */
void hw_monitor_stop(void);

/**
 * @brief 注册状态变化回调
 * @param callback 回调函数
 * @param user_data 用户数据
 */
void hw_monitor_register_callback(hw_status_callback_t callback, void *user_data);

/**
 * @brief 获取CAN总线状态
 * @param interface CAN接口名称（can0/can1）
 * @param status 输出状态
 * @return 0成功，-1失败
 */
int hw_monitor_get_can_status(const char *interface, hw_can_status_t *status);

/**
 * @brief 获取存储设备状态
 * @param mount_point 挂载点（如"/mnt/SDCARD"）
 * @param status 输出状态
 * @return 0成功，-1失败
 */
int hw_monitor_get_storage_status(const char *mount_point, hw_storage_status_t *status);

/**
 * @brief 获取系统状态
 * @param status 输出状态
 * @return 0成功，-1失败
 */
int hw_monitor_get_system_status(hw_system_status_t *status);

/**
 * @brief 获取网络状态
 * @param interface 网络接口名称（eth0/wlan0）
 * @param status 输出状态
 * @return 0成功，-1失败
 */
int hw_monitor_get_network_status(const char *interface, hw_network_status_t *status);

/**
 * @brief 触发立即上报硬件状态（通过WebSocket）
 */
void hw_monitor_report_now(void);

/**
 * @brief 获取硬件状态JSON字符串（用于WebSocket上报）
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 0成功，-1失败
 */
int hw_monitor_get_status_json(char *buffer, size_t buffer_size);

#endif /* HARDWARE_MONITOR_H */

