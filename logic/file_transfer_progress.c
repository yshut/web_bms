/**
 * @file file_transfer_progress.c
 * @brief 文件传输进度跟踪模块实现
 */

#include "file_transfer_progress.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>

static transfer_progress_t g_progress = {0};
static pthread_mutex_t g_progress_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief 获取当前时间（毫秒）
 */
static uint64_t get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/**
 * @brief 初始化文件传输进度跟踪
 */
int file_transfer_progress_init(void)
{
    pthread_mutex_lock(&g_progress_mutex);
    memset(&g_progress, 0, sizeof(g_progress));
    g_progress.status = TRANSFER_STATUS_IDLE;
    pthread_mutex_unlock(&g_progress_mutex);
    
    log_info("文件传输进度跟踪模块初始化");
    return 0;
}

/**
 * @brief 清理文件传输进度跟踪
 */
void file_transfer_progress_deinit(void)
{
    pthread_mutex_lock(&g_progress_mutex);
    memset(&g_progress, 0, sizeof(g_progress));
    pthread_mutex_unlock(&g_progress_mutex);
    
    log_info("文件传输进度跟踪模块清理");
}

/**
 * @brief 开始新的传输任务
 */
int file_transfer_progress_start(const char *filename, transfer_type_t type, size_t total_bytes)
{
    if (!filename) {
        return -1;
    }
    
    pthread_mutex_lock(&g_progress_mutex);
    
    memset(&g_progress, 0, sizeof(g_progress));
    strncpy(g_progress.filename, filename, sizeof(g_progress.filename) - 1);
    g_progress.filename[sizeof(g_progress.filename) - 1] = '\0';
    
    g_progress.type = type;
    g_progress.status = TRANSFER_STATUS_IN_PROGRESS;
    g_progress.total_bytes = total_bytes;
    g_progress.bytes_transferred = 0;
    g_progress.percent = 0;
    g_progress.speed_bps = 0;
    g_progress.start_time_ms = get_time_ms();
    g_progress.elapsed_time_ms = 0;
    g_progress.error_msg[0] = '\0';
    
    pthread_mutex_unlock(&g_progress_mutex);
    
    log_info("开始传输: %s (%s, %zu 字节)", 
             filename, 
             type == TRANSFER_TYPE_UPLOAD ? "上传" : "下载",
             total_bytes);
    
    return 0;
}

/**
 * @brief 更新传输进度
 */
int file_transfer_progress_update(size_t bytes_transferred)
{
    pthread_mutex_lock(&g_progress_mutex);
    
    if (g_progress.status != TRANSFER_STATUS_IN_PROGRESS) {
        pthread_mutex_unlock(&g_progress_mutex);
        return -1;
    }
    
    g_progress.bytes_transferred = bytes_transferred;
    
    // 计算百分比
    if (g_progress.total_bytes > 0) {
        g_progress.percent = (uint32_t)((bytes_transferred * 100) / g_progress.total_bytes);
        if (g_progress.percent > 100) {
            g_progress.percent = 100;
        }
    }
    
    // 计算速度和已用时间
    uint64_t now = get_time_ms();
    g_progress.elapsed_time_ms = now - g_progress.start_time_ms;
    
    if (g_progress.elapsed_time_ms > 0) {
        // 速度：字节/秒
        g_progress.speed_bps = (uint32_t)((bytes_transferred * 1000) / g_progress.elapsed_time_ms);
    }
    
    pthread_mutex_unlock(&g_progress_mutex);
    
    return 0;
}

/**
 * @brief 完成传输
 */
int file_transfer_progress_complete(void)
{
    pthread_mutex_lock(&g_progress_mutex);
    
    g_progress.status = TRANSFER_STATUS_COMPLETED;
    g_progress.percent = 100;
    g_progress.bytes_transferred = g_progress.total_bytes;
    
    uint64_t now = get_time_ms();
    g_progress.elapsed_time_ms = now - g_progress.start_time_ms;
    
    pthread_mutex_unlock(&g_progress_mutex);
    
    log_info("传输完成: %s (%zu 字节, 耗时 %lu ms)",
             g_progress.filename,
             g_progress.total_bytes,
             (unsigned long)g_progress.elapsed_time_ms);
    
    return 0;
}

/**
 * @brief 传输失败
 */
int file_transfer_progress_fail(const char *error_msg)
{
    pthread_mutex_lock(&g_progress_mutex);
    
    g_progress.status = TRANSFER_STATUS_FAILED;
    
    if (error_msg) {
        strncpy(g_progress.error_msg, error_msg, sizeof(g_progress.error_msg) - 1);
        g_progress.error_msg[sizeof(g_progress.error_msg) - 1] = '\0';
    }
    
    uint64_t now = get_time_ms();
    g_progress.elapsed_time_ms = now - g_progress.start_time_ms;
    
    pthread_mutex_unlock(&g_progress_mutex);
    
    log_error("传输失败: %s (%s)",
              g_progress.filename,
              error_msg ? error_msg : "Unknown error");
    
    return 0;
}

/**
 * @brief 取消传输
 */
int file_transfer_progress_cancel(void)
{
    pthread_mutex_lock(&g_progress_mutex);
    
    g_progress.status = TRANSFER_STATUS_CANCELLED;
    
    uint64_t now = get_time_ms();
    g_progress.elapsed_time_ms = now - g_progress.start_time_ms;
    
    pthread_mutex_unlock(&g_progress_mutex);
    
    log_info("传输已取消: %s", g_progress.filename);
    
    return 0;
}

/**
 * @brief 获取当前传输进度
 */
int file_transfer_progress_get(transfer_progress_t *progress)
{
    if (!progress) {
        return -1;
    }
    
    pthread_mutex_lock(&g_progress_mutex);
    memcpy(progress, &g_progress, sizeof(transfer_progress_t));
    pthread_mutex_unlock(&g_progress_mutex);
    
    return 0;
}

/**
 * @brief 获取当前传输进度（JSON格式）
 */
char* file_transfer_progress_get_json(void)
{
    char *json = (char*)malloc(1024);
    if (!json) {
        return NULL;
    }
    
    pthread_mutex_lock(&g_progress_mutex);
    
    const char *status_str = "unknown";
    switch (g_progress.status) {
        case TRANSFER_STATUS_IDLE:
            status_str = "idle";
            break;
        case TRANSFER_STATUS_IN_PROGRESS:
            status_str = "in_progress";
            break;
        case TRANSFER_STATUS_COMPLETED:
            status_str = "completed";
            break;
        case TRANSFER_STATUS_FAILED:
            status_str = "failed";
            break;
        case TRANSFER_STATUS_CANCELLED:
            status_str = "cancelled";
            break;
    }
    
    const char *type_str = g_progress.type == TRANSFER_TYPE_UPLOAD ? "upload" : "download";
    
    snprintf(json, 1024,
             "{\"filename\":\"%s\","
             "\"type\":\"%s\","
             "\"status\":\"%s\","
             "\"bytes_transferred\":%zu,"
             "\"total_bytes\":%zu,"
             "\"percent\":%u,"
             "\"speed_bps\":%u,"
             "\"elapsed_ms\":%lu,"
             "\"error\":\"%s\"}",
             g_progress.filename,
             type_str,
             status_str,
             g_progress.bytes_transferred,
             g_progress.total_bytes,
             g_progress.percent,
             g_progress.speed_bps,
             (unsigned long)g_progress.elapsed_time_ms,
             g_progress.error_msg);
    
    pthread_mutex_unlock(&g_progress_mutex);
    
    return json;
}

