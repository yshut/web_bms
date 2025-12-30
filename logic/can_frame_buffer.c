/**
 * @file can_frame_buffer.c
 * @brief CAN帧环形缓冲区实现
 */

#include "can_frame_buffer.h"
#include "../utils/logger.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>

/* 带时间戳的CAN帧 */
typedef struct {
    can_frame_t frame;
    int channel;
    double timestamp;  // 秒级时间戳，带小数
    bool valid;
} buffered_can_frame_t;

/* 环形缓冲区 */
static struct {
    buffered_can_frame_t *frames;
    int max_frames;
    int write_index;
    int count;
    pthread_mutex_t mutex;
    bool initialized;
} g_buffer = {
    .frames = NULL,
    .max_frames = 0,
    .write_index = 0,
    .count = 0,
    .initialized = false
};

/**
 * @brief 获取当前时间戳（秒，带小数）
 */
static double get_timestamp(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

/**
 * @brief 初始化CAN帧缓冲区
 */
int can_frame_buffer_init(int max_frames)
{
    if (g_buffer.initialized) {
        log_warn("CAN帧缓冲区已经初始化");
        return 0;
    }
    
    if (max_frames <= 0 || max_frames > 10000) {
        log_error("无效的max_frames: %d", max_frames);
        return -1;
    }
    
    g_buffer.frames = (buffered_can_frame_t*)calloc(max_frames, sizeof(buffered_can_frame_t));
    if (!g_buffer.frames) {
        log_error("分配CAN帧缓冲区失败");
        return -1;
    }
    
    g_buffer.max_frames = max_frames;
    g_buffer.write_index = 0;
    g_buffer.count = 0;
    
    pthread_mutex_init(&g_buffer.mutex, NULL);
    g_buffer.initialized = true;
    
    log_info("CAN帧缓冲区已初始化: %d帧", max_frames);
    return 0;
}

/**
 * @brief 清理CAN帧缓冲区
 */
void can_frame_buffer_deinit(void)
{
    if (!g_buffer.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_buffer.mutex);
    
    if (g_buffer.frames) {
        free(g_buffer.frames);
        g_buffer.frames = NULL;
    }
    
    g_buffer.max_frames = 0;
    g_buffer.write_index = 0;
    g_buffer.count = 0;
    g_buffer.initialized = false;
    
    pthread_mutex_unlock(&g_buffer.mutex);
    pthread_mutex_destroy(&g_buffer.mutex);
    
    log_info("CAN帧缓冲区已清理");
}

/**
 * @brief 添加一帧到缓冲区
 */
void can_frame_buffer_add(int channel, const can_frame_t *frame)
{
    if (!g_buffer.initialized || !frame) {
        return;
    }
    
    pthread_mutex_lock(&g_buffer.mutex);
    
    // 写入新帧
    buffered_can_frame_t *buf_frame = &g_buffer.frames[g_buffer.write_index];
    memcpy(&buf_frame->frame, frame, sizeof(can_frame_t));
    buf_frame->channel = channel;
    buf_frame->timestamp = get_timestamp();
    buf_frame->valid = true;
    
    // 移动写指针（环形）
    g_buffer.write_index = (g_buffer.write_index + 1) % g_buffer.max_frames;
    
    // 更新计数
    if (g_buffer.count < g_buffer.max_frames) {
        g_buffer.count++;
    }
    
    pthread_mutex_unlock(&g_buffer.mutex);
}

/**
 * @brief 清空缓冲区
 */
void can_frame_buffer_clear(void)
{
    if (!g_buffer.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_buffer.mutex);
    
    // 清空所有帧
    if (g_buffer.frames) {
        memset(g_buffer.frames, 0, g_buffer.max_frames * sizeof(buffered_can_frame_t));
    }
    
    g_buffer.write_index = 0;
    g_buffer.count = 0;
    
    pthread_mutex_unlock(&g_buffer.mutex);
    
    log_info("CAN帧缓冲区已清空");
}

/**
 * @brief 获取最近的N帧，转换为JSON数组
 */
int can_frame_buffer_get_json(char *buffer, int buffer_size, int limit)
{
    if (!g_buffer.initialized || !buffer || buffer_size <= 0) {
        return -1;
    }
    
    if (limit <= 0) {
        limit = 50;
    }
    
    pthread_mutex_lock(&g_buffer.mutex);
    
    // 如果没有帧，返回空数组
    if (g_buffer.count == 0) {
        pthread_mutex_unlock(&g_buffer.mutex);
        snprintf(buffer, buffer_size, "[]");
        return 0;
    }
    
    // 计算要读取的帧数
    int frames_to_read = (limit < g_buffer.count) ? limit : g_buffer.count;
    
    // 计算起始索引（从最新的帧开始往回读）
    int start_index = (g_buffer.write_index - frames_to_read + g_buffer.max_frames) % g_buffer.max_frames;
    
    // 构建JSON数组
    char *p = buffer;
    int remaining = buffer_size;
    int written = 0;
    
    written = snprintf(p, remaining, "[");
    p += written;
    remaining -= written;
    
    int actual_count = 0;
    for (int i = 0; i < frames_to_read && remaining > 100; i++) {
        int index = (start_index + i) % g_buffer.max_frames;
        buffered_can_frame_t *buf_frame = &g_buffer.frames[index];
        
        if (!buf_frame->valid) {
            continue;
        }
        
        const can_frame_t *frame = &buf_frame->frame;
        
        // 添加逗号（除了第一帧）
        if (actual_count > 0) {
            written = snprintf(p, remaining, ",");
            p += written;
            remaining -= written;
        }
        
        // 写入帧数据
        // 格式: {"id":123,"data":[1,2,3,4,5,6,7,8],"timestamp":1234567890.123,"iface":"can0"}
        written = snprintf(p, remaining, 
                          "{\"id\":%u,\"data\":[",
                          frame->can_id & 0x1FFFFFFF);
        p += written;
        remaining -= written;
        
        // 写入数据字节
        for (int j = 0; j < frame->can_dlc && j < 8; j++) {
            if (j > 0) {
                written = snprintf(p, remaining, ",");
                p += written;
                remaining -= written;
            }
            written = snprintf(p, remaining, "%u", frame->data[j]);
            p += written;
            remaining -= written;
        }
        
        // 写入时间戳和接口
        const char *iface_name = (buf_frame->channel == 1) ? "can1" : "can0";
        written = snprintf(p, remaining, 
                          "],\"timestamp\":%.3f,\"iface\":\"%s\"}",
                          buf_frame->timestamp, iface_name);
        p += written;
        remaining -= written;
        
        actual_count++;
    }
    
    // 关闭数组
    if (remaining > 1) {
        snprintf(p, remaining, "]");
    }
    
    pthread_mutex_unlock(&g_buffer.mutex);
    
    return actual_count;
}

/**
 * @brief 获取缓冲区中的帧数量
 */
int can_frame_buffer_get_count(void)
{
    if (!g_buffer.initialized) {
        return 0;
    }
    
    pthread_mutex_lock(&g_buffer.mutex);
    int count = g_buffer.count;
    pthread_mutex_unlock(&g_buffer.mutex);
    
    return count;
}

