/**
 * @file hardware_monitor.c
 * @brief 硬件状态监控模块实现
 */

#include "hardware_monitor.h"
#include "can_handler.h"
#include "remote_transport.h"
#include "../utils/logger.h"
#include "../utils/app_config.h"
#include "../utils/net_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/can.h>
#include <linux/can/error.h>

/* 监控器上下文 */
typedef struct {
    hw_monitor_config_t config;
    bool running;
    pthread_t monitor_thread;
    hw_status_callback_t callback;
    void *callback_user_data;
    
    /* 状态缓存 */
    hw_can_status_t can0_status;
    hw_can_status_t can1_status;
    hw_storage_status_t sd_status;
    hw_system_status_t sys_status;
    hw_network_status_t eth_status;
    
    /* 上次上报时间 */
    uint64_t last_report_ms;
    
    pthread_mutex_t mutex;
} hw_monitor_ctx_t;

static hw_monitor_ctx_t g_hw_ctx = {
    .running = false,
};

/* 前向声明 */
static void* monitor_thread_func(void *arg);
static int update_can_status(const char *interface, hw_can_status_t *status);
static int update_storage_status(const char *mount_point, hw_storage_status_t *status);
static int update_system_status(hw_system_status_t *status);
static int update_network_status(const char *interface, hw_network_status_t *status);
static uint64_t get_current_time_ms(void);

/**
 * @brief 获取当前时间（毫秒）
 */
static uint64_t get_current_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/**
 * @brief 初始化硬件监控
 */
int hw_monitor_init(const hw_monitor_config_t *config)
{
    if (!config) {
        log_error("hw_monitor_init: config is NULL");
        return -1;
    }
    
    pthread_mutex_init(&g_hw_ctx.mutex, NULL);
    
    memcpy(&g_hw_ctx.config, config, sizeof(hw_monitor_config_t));
    
    /* 初始化状态 */
    memset(&g_hw_ctx.can0_status, 0, sizeof(hw_can_status_t));
    memset(&g_hw_ctx.can1_status, 0, sizeof(hw_can_status_t));
    memset(&g_hw_ctx.sd_status, 0, sizeof(hw_storage_status_t));
    memset(&g_hw_ctx.sys_status, 0, sizeof(hw_system_status_t));
    memset(&g_hw_ctx.eth_status, 0, sizeof(hw_network_status_t));
    
    strncpy(g_hw_ctx.can0_status.interface, "can0", sizeof(g_hw_ctx.can0_status.interface) - 1);
    strncpy(g_hw_ctx.can1_status.interface, "can1", sizeof(g_hw_ctx.can1_status.interface) - 1);
    strncpy(g_hw_ctx.sd_status.mount_point,
            (g_app_config.storage_mount[0] ? g_app_config.storage_mount : "/mnt/SDCARD"),
            sizeof(g_hw_ctx.sd_status.mount_point) - 1);
    strncpy(g_hw_ctx.eth_status.interface,
            (g_app_config.net_iface[0] ? g_app_config.net_iface : "eth0"),
            sizeof(g_hw_ctx.eth_status.interface) - 1);
    
    g_hw_ctx.last_report_ms = 0;
    
    log_info("硬件监控初始化完成");
    return 0;
}

/**
 * @brief 反初始化硬件监控
 */
void hw_monitor_deinit(void)
{
    hw_monitor_stop();
    pthread_mutex_destroy(&g_hw_ctx.mutex);
    log_info("硬件监控已清理");
}

/**
 * @brief 启动硬件监控线程
 */
int hw_monitor_start(void)
{
    if (g_hw_ctx.running) {
        log_warn("硬件监控已在运行");
        return 0;
    }
    
    g_hw_ctx.running = true;
    
    if (pthread_create(&g_hw_ctx.monitor_thread, NULL, monitor_thread_func, NULL) != 0) {
        log_error("创建硬件监控线程失败");
        g_hw_ctx.running = false;
        return -1;
    }
    
    log_info("硬件监控线程已启动");
    return 0;
}

/**
 * @brief 停止硬件监控线程
 */
void hw_monitor_stop(void)
{
    if (!g_hw_ctx.running) {
        return;
    }
    
    g_hw_ctx.running = false;
    pthread_join(g_hw_ctx.monitor_thread, NULL);
    
    log_info("硬件监控线程已停止");
}

/**
 * @brief 注册状态变化回调
 */
void hw_monitor_register_callback(hw_status_callback_t callback, void *user_data)
{
    pthread_mutex_lock(&g_hw_ctx.mutex);
    g_hw_ctx.callback = callback;
    g_hw_ctx.callback_user_data = user_data;
    pthread_mutex_unlock(&g_hw_ctx.mutex);
}

/**
 * @brief 更新CAN状态
 */
static int update_can_status(const char *interface, hw_can_status_t *status)
{
    if (!interface || !status) return -1;
    
    /* 读取CAN接口状态 */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ip -s -d link show %s 2>/dev/null", interface);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        status->status = HW_STATUS_OFFLINE;
        snprintf(status->last_error, sizeof(status->last_error), "Failed to query interface");
        return -1;
    }
    
    char line[512];
    bool interface_found = false;
    status->error_warning = false;
    status->error_passive = false;
    status->bus_off = false;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, interface)) {
            interface_found = true;
        }
        
        /* 检查状态 */
        if (strstr(line, "state ERROR-PASSIVE")) {
            status->error_passive = true;
            status->status = HW_STATUS_ERROR;
            snprintf(status->last_error, sizeof(status->last_error), "ERROR-PASSIVE state");
        }
        if (strstr(line, "state ERROR-WARNING")) {
            status->error_warning = true;
            status->status = HW_STATUS_WARNING;
            snprintf(status->last_error, sizeof(status->last_error), "ERROR-WARNING state");
        }
        if (strstr(line, "state BUS-OFF")) {
            status->bus_off = true;
            status->status = HW_STATUS_ERROR;
            snprintf(status->last_error, sizeof(status->last_error), "BUS-OFF state");
        }
        if (strstr(line, "state ERROR-ACTIVE") || strstr(line, "state RUNNING")) {
            if (!status->error_warning && !status->error_passive && !status->bus_off) {
                status->status = HW_STATUS_NORMAL;
                status->last_error[0] = '\0';
            }
        }
        
        /* 解析统计信息：
         * ip -s 输出格式:
         *   RX: bytes  packets  errors  dropped overrun mcast
         *   <数值行>
         *   TX: bytes  packets  errors  dropped carrier collsns
         *   <数值行>
         */
        if (strstr(line, "RX:") && strstr(line, "packets")) {
            /* 下一行是 RX 数值 */
            if (fgets(line, sizeof(line), fp)) {
                unsigned long rx_bytes=0, rx_pkts=0, rx_err=0;
                sscanf(line, " %lu %lu %lu", &rx_bytes, &rx_pkts, &rx_err);
                status->rx_count    = (uint32_t)rx_pkts;
                status->error_count = (uint32_t)rx_err;
            }
        }
        if (strstr(line, "TX:") && strstr(line, "packets")) {
            /* 下一行是 TX 数值 */
            if (fgets(line, sizeof(line), fp)) {
                unsigned long tx_bytes=0, tx_pkts=0;
                sscanf(line, " %lu %lu", &tx_bytes, &tx_pkts);
                status->tx_count = (uint32_t)tx_pkts;
            }
        }
    }
    
    pclose(fp);
    
    if (!interface_found) {
        status->status = HW_STATUS_OFFLINE;
        snprintf(status->last_error, sizeof(status->last_error), "Interface not found");
        return -1;
    }
    
    return 0;
}

/**
 * @brief 更新存储设备状态
 */
static int update_storage_status(const char *mount_point, hw_storage_status_t *status)
{
    if (!mount_point || !status) return -1;
    
    struct statvfs stat;
    
    if (statvfs(mount_point, &stat) != 0) {
        status->status = HW_STATUS_ERROR;
        status->is_mounted = false;
        snprintf(status->last_error, sizeof(status->last_error), "Mount point not accessible");
        return -1;
    }
    
    status->is_mounted = true;
    status->total_bytes = stat.f_blocks * stat.f_frsize;
    status->free_bytes = stat.f_bfree * stat.f_frsize;
    status->used_bytes = status->total_bytes - status->free_bytes;
    
    /* 判断状态 */
    if (status->total_bytes == 0) {
        status->status = HW_STATUS_ERROR;
        snprintf(status->last_error, sizeof(status->last_error), "Storage size is zero");
    } else {
        float usage_percent = (float)status->used_bytes / status->total_bytes * 100.0f;
        if (usage_percent > 90.0f) {
            status->status = HW_STATUS_WARNING;
            snprintf(status->last_error, sizeof(status->last_error), "Storage almost full (%.1f%%)", usage_percent);
        } else {
            status->status = HW_STATUS_NORMAL;
            status->last_error[0] = '\0';
        }
    }
    
    return 0;
}

/**
 * @brief 更新系统状态
 */
static int update_system_status(hw_system_status_t *status)
{
    if (!status) return -1;
    
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        return -1;
    }

    status->uptime_seconds = info.uptime;

    /* 从 /proc/meminfo 读取 MemTotal / MemAvailable（与 LuCI 一致） */
    {
        uint64_t mem_total_kb = 0, mem_available_kb = 0, mem_free_kb = 0;
        FILE *mfp = fopen("/proc/meminfo", "r");
        if (mfp) {
            char mline[128];
            while (fgets(mline, sizeof(mline), mfp)) {
                uint64_t v = 0;
                if (sscanf(mline, "MemTotal: %llu kB", (unsigned long long *)&v) == 1)
                    mem_total_kb = v;
                else if (sscanf(mline, "MemAvailable: %llu kB", (unsigned long long *)&v) == 1)
                    mem_available_kb = v;
                else if (sscanf(mline, "MemFree: %llu kB", (unsigned long long *)&v) == 1)
                    mem_free_kb = v;
            }
            fclose(mfp);
        }
        if (mem_total_kb == 0) {
            /* /proc/meminfo 不可用时回退到 sysinfo */
            mem_total_kb     = info.totalram / 1024;
            mem_free_kb      = info.freeram  / 1024;
            mem_available_kb = mem_free_kb;
        }
        if (mem_available_kb == 0) mem_available_kb = mem_free_kb;
        status->memory_total = mem_total_kb;
        /* memory_free 存储 MemAvailable，与 LuCI 保持一致 */
        status->memory_free  = mem_available_kb;
        status->memory_used  = mem_total_kb > mem_available_kb
                               ? (mem_total_kb - mem_available_kb) : 0;
        status->memory_usage = mem_total_kb > 0
                               ? (float)status->memory_used / mem_total_kb * 100.0f : 0.0f;
    }
    
    /* 读取CPU使用率（简化版：从/proc/stat） */
    FILE *fp = fopen("/proc/stat", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            unsigned long user, nice, system, idle;
            if (sscanf(line, "cpu %lu %lu %lu %lu", &user, &nice, &system, &idle) == 4) {
                unsigned long total = user + nice + system + idle;
                if (total > 0) {
                    status->cpu_usage = (float)(user + nice + system) / total * 100.0f;
                }
            }
        }
        fclose(fp);
    }
    
    /* 读取温度（T113芯片温度） */
    fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (fp) {
        int temp_millicelsius = 0;
        if (fscanf(fp, "%d", &temp_millicelsius) == 1) {
            status->temperature = temp_millicelsius / 1000.0f;
        }
        fclose(fp);
    }
    
    return 0;
}

/**
 * @brief 更新网络状态
 */
static int update_network_status(const char *interface, hw_network_status_t *status)
{
    if (!interface || !status) return -1;
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        status->status = HW_STATUS_ERROR;
        return -1;
    }
    
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface, sizeof(ifr.ifr_name) - 1);
    
    /* 检查接口是否存在和UP */
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        status->status = HW_STATUS_OFFLINE;
        status->is_connected = false;
        close(sock);
        return -1;
    }
    
    status->is_connected = (ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING);
    
    /* 获取IP地址 */
    if (ioctl(sock, SIOCGIFADDR, &ifr) == 0) {
        struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
        strncpy(status->ip_address, inet_ntoa(addr->sin_addr), sizeof(status->ip_address) - 1);
    }
    
    /* 获取MAC地址 */
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
        unsigned char *mac = (unsigned char *)ifr.ifr_hwaddr.sa_data;
        snprintf(status->mac_address, sizeof(status->mac_address),
                "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    
    close(sock);
    
    /* 读取网络统计 */
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_bytes", interface);
    FILE *fp = fopen(path, "r");
    if (fp) {
        fscanf(fp, "%" SCNu64, &status->rx_bytes);
        fclose(fp);
    }
    
    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_bytes", interface);
    fp = fopen(path, "r");
    if (fp) {
        fscanf(fp, "%" SCNu64, &status->tx_bytes);
        fclose(fp);
    }
    
    status->status = status->is_connected ? HW_STATUS_NORMAL : HW_STATUS_WARNING;
    
    return 0;
}

/**
 * @brief 生成硬件状态JSON
 */
int hw_monitor_get_status_json(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return -1;
    
    pthread_mutex_lock(&g_hw_ctx.mutex);
    
    char *p = buffer;
    size_t remaining = buffer_size;
    int written;
    
    written = snprintf(p, remaining, "{");
    p += written; remaining -= written;
    
    /* CAN0 状态 */
    written = snprintf(p, remaining,
        "\"can0\":{\"status\":%d,\"bitrate\":%u,\"rx\":%u,\"tx\":%u,\"errors\":%u,"
        "\"error_warning\":%s,\"error_passive\":%s,\"bus_off\":%s,\"last_error\":\"%s\"},",
        g_hw_ctx.can0_status.status,
        g_hw_ctx.can0_status.bitrate,
        g_hw_ctx.can0_status.rx_count,
        g_hw_ctx.can0_status.tx_count,
        g_hw_ctx.can0_status.error_count,
        g_hw_ctx.can0_status.error_warning ? "true" : "false",
        g_hw_ctx.can0_status.error_passive ? "true" : "false",
        g_hw_ctx.can0_status.bus_off ? "true" : "false",
        g_hw_ctx.can0_status.last_error);
    p += written; remaining -= written;
    
    /* CAN1 状态 */
    written = snprintf(p, remaining,
        "\"can1\":{\"status\":%d,\"bitrate\":%u,\"rx\":%u,\"tx\":%u,\"errors\":%u,"
        "\"error_warning\":%s,\"error_passive\":%s,\"bus_off\":%s,\"last_error\":\"%s\"},",
        g_hw_ctx.can1_status.status,
        g_hw_ctx.can1_status.bitrate,
        g_hw_ctx.can1_status.rx_count,
        g_hw_ctx.can1_status.tx_count,
        g_hw_ctx.can1_status.error_count,
        g_hw_ctx.can1_status.error_warning ? "true" : "false",
        g_hw_ctx.can1_status.error_passive ? "true" : "false",
        g_hw_ctx.can1_status.bus_off ? "true" : "false",
        g_hw_ctx.can1_status.last_error);
    p += written; remaining -= written;
    
    /* 存储状态 */
    written = snprintf(p, remaining,
        "\"storage\":{\"status\":%d,\"mounted\":%s,\"total\":%" PRIu64 ",\"used\":%" PRIu64 ",\"free\":%" PRIu64 ","
        "\"mount_point\":\"%s\",\"last_error\":\"%s\"},",
        g_hw_ctx.sd_status.status,
        g_hw_ctx.sd_status.is_mounted ? "true" : "false",
        g_hw_ctx.sd_status.total_bytes,
        g_hw_ctx.sd_status.used_bytes,
        g_hw_ctx.sd_status.free_bytes,
        g_hw_ctx.sd_status.mount_point,
        g_hw_ctx.sd_status.last_error);
    p += written; remaining -= written;
    
    /* 系统状态 */
    written = snprintf(p, remaining,
        "\"system\":{\"cpu_usage\":%.1f,\"memory_total\":%" PRIu64 ",\"memory_used\":%" PRIu64 ","
        "\"memory_free\":%" PRIu64 ",\"memory_usage\":%.1f,\"temperature\":%.1f,\"uptime\":%" PRIu64 "},",
        g_hw_ctx.sys_status.cpu_usage,
        g_hw_ctx.sys_status.memory_total,
        g_hw_ctx.sys_status.memory_used,
        g_hw_ctx.sys_status.memory_free,
        g_hw_ctx.sys_status.memory_usage,
        g_hw_ctx.sys_status.temperature,
        g_hw_ctx.sys_status.uptime_seconds);
    p += written; remaining -= written;
    
    /* 网络状态 */
    written = snprintf(p, remaining,
        "\"network\":{\"status\":%d,\"connected\":%s,\"interface\":\"%s\","
        "\"ip\":\"%s\",\"mac\":\"%s\",\"rx_bytes\":%" PRIu64 ",\"tx_bytes\":%" PRIu64 "}",
        g_hw_ctx.eth_status.status,
        g_hw_ctx.eth_status.is_connected ? "true" : "false",
        g_hw_ctx.eth_status.interface,
        g_hw_ctx.eth_status.ip_address,
        g_hw_ctx.eth_status.mac_address,
        g_hw_ctx.eth_status.rx_bytes,
        g_hw_ctx.eth_status.tx_bytes);
    p += written; remaining -= written;
    
    written = snprintf(p, remaining, "}");
    
    pthread_mutex_unlock(&g_hw_ctx.mutex);
    
    return 0;
}

/**
 * @brief 触发立即上报
 */
void hw_monitor_report_now(void)
{
    if (!remote_transport_is_connected()) {
        return;
    }
    
    char status_json[4096];
    if (hw_monitor_get_status_json(status_json, sizeof(status_json)) == 0) {
        remote_transport_publish_event("hardware_status", status_json);
        log_debug("硬件状态已上报");
    }
}

/**
 * @brief 监控线程主函数
 */
static void* monitor_thread_func(void *arg)
{
    (void)arg;
    
    log_info("硬件监控线程开始运行");
    
    while (g_hw_ctx.running) {
        uint64_t now_ms = get_current_time_ms();
        
        pthread_mutex_lock(&g_hw_ctx.mutex);
        
        /* 更新各项状态 */
        if (g_hw_ctx.config.enable_can_monitor) {
            update_can_status("can0", &g_hw_ctx.can0_status);
            update_can_status("can1", &g_hw_ctx.can1_status);
        }
        
        if (g_hw_ctx.config.enable_storage_monitor) {
            update_storage_status(g_hw_ctx.sd_status.mount_point, &g_hw_ctx.sd_status);
        }
        
        if (g_hw_ctx.config.enable_system_monitor) {
            update_system_status(&g_hw_ctx.sys_status);
        }
        
        if (g_hw_ctx.config.enable_network_monitor) {
            char active_iface[sizeof(g_hw_ctx.eth_status.interface)] = {0};
            if (net_manager_get_active_interface(active_iface, sizeof(active_iface)) == 0 && active_iface[0]) {
                strncpy(g_hw_ctx.eth_status.interface, active_iface, sizeof(g_hw_ctx.eth_status.interface) - 1);
                g_hw_ctx.eth_status.interface[sizeof(g_hw_ctx.eth_status.interface) - 1] = '\0';
            }
            update_network_status(g_hw_ctx.eth_status.interface, &g_hw_ctx.eth_status);
        }
        
        pthread_mutex_unlock(&g_hw_ctx.mutex);
        
        /* 定期上报 */
        if (g_hw_ctx.config.enable_auto_report && remote_transport_is_connected()) {
            if (now_ms - g_hw_ctx.last_report_ms >= g_hw_ctx.config.report_interval_ms) {
                hw_monitor_report_now();
                g_hw_ctx.last_report_ms = now_ms;
            }
        }
        
        /* 休眠 */
        usleep(g_hw_ctx.config.interval_ms * 1000);
    }
    
    log_info("硬件监控线程退出");
    return NULL;
}

/**
 * @brief 获取CAN状态
 */
int hw_monitor_get_can_status(const char *interface, hw_can_status_t *status)
{
    if (!interface || !status) return -1;
    
    pthread_mutex_lock(&g_hw_ctx.mutex);
    
    if (strcmp(interface, "can0") == 0) {
        memcpy(status, &g_hw_ctx.can0_status, sizeof(hw_can_status_t));
    } else if (strcmp(interface, "can1") == 0) {
        memcpy(status, &g_hw_ctx.can1_status, sizeof(hw_can_status_t));
    } else {
        pthread_mutex_unlock(&g_hw_ctx.mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&g_hw_ctx.mutex);
    return 0;
}

/**
 * @brief 获取存储状态
 */
int hw_monitor_get_storage_status(const char *mount_point, hw_storage_status_t *status)
{
    if (!mount_point || !status) return -1;
    
    pthread_mutex_lock(&g_hw_ctx.mutex);
    memcpy(status, &g_hw_ctx.sd_status, sizeof(hw_storage_status_t));
    pthread_mutex_unlock(&g_hw_ctx.mutex);
    
    return 0;
}

/**
 * @brief 获取系统状态
 */
int hw_monitor_get_system_status(hw_system_status_t *status)
{
    if (!status) return -1;
    
    pthread_mutex_lock(&g_hw_ctx.mutex);
    memcpy(status, &g_hw_ctx.sys_status, sizeof(hw_system_status_t));
    pthread_mutex_unlock(&g_hw_ctx.mutex);
    
    return 0;
}

/**
 * @brief 获取网络状态
 */
int hw_monitor_get_network_status(const char *interface, hw_network_status_t *status)
{
    if (!interface || !status) return -1;
    
    pthread_mutex_lock(&g_hw_ctx.mutex);
    memcpy(status, &g_hw_ctx.eth_status, sizeof(hw_network_status_t));
    pthread_mutex_unlock(&g_hw_ctx.mutex);
    
    return 0;
}

