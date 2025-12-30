/**
 * @file can_recorder.c
 * @brief CAN报文录制模块实现
 */

#include "can_recorder.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>

/* 录制器上下文 */
typedef struct {
    bool recording;                    /* 录制状态 */
    bool record_can0;                  /* 录制CAN0 */
    bool record_can1;                  /* 录制CAN1 */
    
    FILE *record_file;                 /* 当前录制文件 */
    char record_dir[256];              /* 录制目录 */
    char record_basename[128];         /* 文件基础名 */
    int file_seq;                      /* 文件序号 */
    uint64_t max_file_size;            /* 最大文件大小 */
    uint64_t current_file_size;        /* 当前文件大小 */
    
    char *write_buffer;                /* 写入缓冲区 */
    size_t buffer_size;                /* 缓冲区大小 */
    size_t buffer_used;                /* 已使用缓冲区 */
    
    pthread_t flush_thread;            /* 刷新线程 */
    pthread_mutex_t mutex;             /* 互斥锁 */
    int flush_interval_ms;             /* 刷新间隔 */
    
    struct timeval start_time;         /* 录制开始时间 */
    can_recorder_stats_t stats;        /* 统计信息 */
} can_recorder_ctx_t;

static can_recorder_ctx_t g_recorder = {
    .recording = false,
    .record_can0 = true,
    .record_can1 = true,
    .record_file = NULL,
    .max_file_size = 40 * 1024 * 1024,  // 40MB
    .buffer_size = 256 * 1024,          // 256KB缓冲
    .flush_interval_ms = 200,           // 200ms刷新
};

/**
 * @brief 确保录制目录存在
 */
static int ensure_record_directory(void)
{
    struct stat st = {0};
    
    if (stat(g_recorder.record_dir, &st) == -1) {
        if (mkdir(g_recorder.record_dir, 0755) < 0) {
            log_error("创建录制目录失败: %s", strerror(errno));
            return -1;
        }
        log_info("创建录制目录: %s", g_recorder.record_dir);
    }
    
    return 0;
}

/**
 * @brief 生成ASC文件头
 */
static void generate_asc_header(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    snprintf(buffer, size,
             "date %s %02d:%02d:%02d.000\n"
             "base hex  timestamps absolute\n"
             "internal events logged\n"
             "Begin Triggerblock %s %02d:%02d:%02d.000\n"
             "   0.000000 Start of measurement\n",
             "Mon Jan 1 00:00:00 2024",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             "Mon Jan 1 00:00:00 2024",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
}

/**
 * @brief 打开新的录制文件
 */
static int open_record_file(void)
{
    char filepath[512];
    char header[1024];
    
    // 关闭旧文件
    if (g_recorder.record_file) {
        fclose(g_recorder.record_file);
        g_recorder.record_file = NULL;
    }
    
    // 生成文件名
    snprintf(filepath, sizeof(filepath), "%s/%s_%03d.asc",
             g_recorder.record_dir, g_recorder.record_basename, g_recorder.file_seq);
    
    // 打开文件
    g_recorder.record_file = fopen(filepath, "w");
    if (!g_recorder.record_file) {
        log_error("打开录制文件失败: %s", strerror(errno));
        return -1;
    }
    
    // 写入ASC文件头
    generate_asc_header(header, sizeof(header));
    fprintf(g_recorder.record_file, "%s", header);
    
    g_recorder.current_file_size = strlen(header);
    strncpy(g_recorder.stats.current_file, filepath, sizeof(g_recorder.stats.current_file) - 1);
    g_recorder.stats.file_count++;
    g_recorder.file_seq++;
    
    log_info("创建录制文件: %s", filepath);
    return 0;
}

/**
 * @brief 检查是否需要分段
 */
static int check_file_rotation(void)
{
    if (g_recorder.current_file_size >= g_recorder.max_file_size) {
        log_info("文件达到最大大小，开始分段");
        return open_record_file();
    }
    return 0;
}

/**
 * @brief 将CAN帧转换为ASC格式并添加到缓冲区
 */
static void append_frame_to_buffer(int channel, const can_frame_t *frame)
{
    char line[256];
    struct timeval now, diff;
    
    gettimeofday(&now, NULL);
    timersub(&now, &g_recorder.start_time, &diff);
    
    double timestamp = diff.tv_sec + diff.tv_usec / 1000000.0;
    
    // 格式化CAN帧为ASC格式
    // 标准帧: "   1.234567 1  123             Rx   d 8  11 22 33 44 55 66 77 88"
    // 扩展帧: "   1.234567 1  18FF45F4x       Rx   d 8  11 22 33 44 55 66 77 88"
    int len;
    if (frame->is_extended) {
        len = snprintf(line, sizeof(line),
                      "  %10.6f %d  %08lXx       Rx   d %d ",
                      timestamp, channel + 1,
                      (unsigned long)frame->can_id, frame->can_dlc);
    } else {
        len = snprintf(line, sizeof(line),
                      "  %10.6f %d  %-16X Rx   d %d ",
                      timestamp, channel + 1,
                      frame->can_id, frame->can_dlc);
    }
    
    // 添加数据字节
    for (int i = 0; i < frame->can_dlc && i < 8; i++) {
        len += snprintf(line + len, sizeof(line) - len, " %02X", frame->data[i]);
    }
    len += snprintf(line + len, sizeof(line) - len, "\n");
    
    // 添加到缓冲区
    pthread_mutex_lock(&g_recorder.mutex);
    
    if (g_recorder.buffer_used + len < g_recorder.buffer_size) {
        memcpy(g_recorder.write_buffer + g_recorder.buffer_used, line, len);
        g_recorder.buffer_used += len;
    } else {
        log_warn("录制缓冲区已满，丢弃帧");
    }
    
    pthread_mutex_unlock(&g_recorder.mutex);
}

/**
 * @brief 刷新缓冲区到文件
 */
static void flush_buffer_to_file(void)
{
    pthread_mutex_lock(&g_recorder.mutex);
    
    // 调试：定期打印刷新状态
    static int flush_count = 0;
    flush_count++;
    
    if (flush_count % 50 == 1) {
        log_info("刷新状态: buffer_used=%zu, file=%p, recording=%d",
                 g_recorder.buffer_used, (void*)g_recorder.record_file, g_recorder.recording);
    }
    
    if (g_recorder.buffer_used > 0 && g_recorder.record_file) {
        log_info("刷新缓冲区: %zu字节 -> 文件", g_recorder.buffer_used);
        
        size_t written = fwrite(g_recorder.write_buffer, 1, g_recorder.buffer_used, g_recorder.record_file);
        fflush(g_recorder.record_file); // 强制刷新到磁盘
        
        if (written == g_recorder.buffer_used) {
            log_info("写入成功: %zu字节", written);
            g_recorder.current_file_size += written;
            g_recorder.stats.bytes_written += written;
            g_recorder.buffer_used = 0;
            
            // 检查是否需要分段
            check_file_rotation();
        } else {
            log_error("写入录制文件失败: 期望%zu字节，实际%zu字节", g_recorder.buffer_used, written);
        }
    } else if (g_recorder.buffer_used > 0 && !g_recorder.record_file) {
        log_error("缓冲区有数据(%zu字节)但文件未打开", g_recorder.buffer_used);
    }
    
    pthread_mutex_unlock(&g_recorder.mutex);
}

/**
 * @brief 刷新线程
 */
static void* flush_thread_func(void *arg)
{
    (void)arg;
    
    log_info("录制刷新线程启动");
    
    while (g_recorder.recording) {
        usleep(g_recorder.flush_interval_ms * 1000);
        flush_buffer_to_file();
    }
    
    // 最后刷新一次
    flush_buffer_to_file();
    
    log_info("录制刷新线程退出");
    return NULL;
}

/**
 * @brief 初始化录制器
 */
int can_recorder_init(const can_recorder_config_t *config)
{
    if (g_recorder.recording) {
        log_warn("录制器已经在运行");
        return 0;
    }
    
    // 应用配置
    if (config) {
        g_recorder.record_can0 = config->record_can0;
        g_recorder.record_can1 = config->record_can1;
        
        if (config->record_dir[0]) {
            strncpy(g_recorder.record_dir, config->record_dir, sizeof(g_recorder.record_dir) - 1);
        }
        
        if (config->max_file_size > 0) {
            g_recorder.max_file_size = config->max_file_size;
        }
        
        if (config->flush_interval_ms > 0) {
            g_recorder.flush_interval_ms = config->flush_interval_ms;
        }
    }
    
    // 默认配置
    if (g_recorder.record_dir[0] == '\0') {
        strncpy(g_recorder.record_dir, "/mnt/SDCARD/can_records", sizeof(g_recorder.record_dir) - 1);
    }
    
    // 分配缓冲区
    g_recorder.write_buffer = (char *)malloc(g_recorder.buffer_size);
    if (!g_recorder.write_buffer) {
        log_error("分配录制缓冲区失败");
        return -1;
    }
    
    // 初始化互斥锁
    pthread_mutex_init(&g_recorder.mutex, NULL);
    
    // 清空统计
    memset(&g_recorder.stats, 0, sizeof(g_recorder.stats));
    
    log_info("CAN录制器初始化完成");
    log_info("录制目录: %s", g_recorder.record_dir);
    log_info("CAN0: %s, CAN1: %s", 
             g_recorder.record_can0 ? "启用" : "禁用",
             g_recorder.record_can1 ? "启用" : "禁用");
    
    return 0;
}

/**
 * @brief 清理录制器
 */
void can_recorder_deinit(void)
{
    if (g_recorder.recording) {
        can_recorder_stop();
    }
    
    if (g_recorder.write_buffer) {
        free(g_recorder.write_buffer);
        g_recorder.write_buffer = NULL;
    }
    
    pthread_mutex_destroy(&g_recorder.mutex);
    
    log_info("CAN录制器已清理");
}

/**
 * @brief 开始录制
 */
int can_recorder_start(void)
{
    if (g_recorder.recording) {
        log_warn("录制已经在进行");
        return 0;
    }
    
    log_info("========== 开始CAN录制 ==========");
    log_info("录制通道: CAN0=%d, CAN1=%d", g_recorder.record_can0, g_recorder.record_can1);
    
    // 确保目录存在
    if (ensure_record_directory() < 0) {
        return -1;
    }
    
    // 生成文件基础名（时间戳）
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    snprintf(g_recorder.record_basename, sizeof(g_recorder.record_basename),
             "can_%04d%02d%02d_%02d%02d%02d",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    
    log_info("录制文件基础名: %s", g_recorder.record_basename);
    
    g_recorder.file_seq = 1;
    g_recorder.buffer_used = 0;
    gettimeofday(&g_recorder.start_time, NULL);
    
    // 打开第一个文件
    if (open_record_file() < 0) {
        log_error("打开录制文件失败");
        return -1;
    }
    
    log_info("录制文件已打开: %s", g_recorder.stats.current_file);
    
    // 启动刷新线程
    g_recorder.recording = true;
    log_info("设置recording=true");
    if (pthread_create(&g_recorder.flush_thread, NULL, flush_thread_func, NULL) != 0) {
        log_error("创建刷新线程失败");
        g_recorder.recording = false;
        fclose(g_recorder.record_file);
        g_recorder.record_file = NULL;
        return -1;
    }
    
    log_info("CAN录制已启动");
    return 0;
}

/**
 * @brief 停止录制
 */
void can_recorder_stop(void)
{
    if (!g_recorder.recording) {
        return;
    }
    
    g_recorder.recording = false;
    
    // 等待刷新线程退出
    pthread_join(g_recorder.flush_thread, NULL);
    
    // 关闭文件
    if (g_recorder.record_file) {
        // 写入结束标记
        fprintf(g_recorder.record_file, "End TriggerBlock\n");
        fclose(g_recorder.record_file);
        g_recorder.record_file = NULL;
    }
    
    log_info("CAN录制已停止");
    log_info("统计: 总帧数=%lu, CAN0=%lu, CAN1=%lu, 文件数=%u",
             g_recorder.stats.total_frames,
             g_recorder.stats.can0_frames,
             g_recorder.stats.can1_frames,
             g_recorder.stats.file_count);
}

/**
 * @brief 检查是否正在录制
 */
bool can_recorder_is_recording(void)
{
    return g_recorder.recording;
}

/**
 * @brief 获取当前录制文件名
 */
const char* can_recorder_get_filename(void)
{
    static char filename[384];
    
    if (!g_recorder.recording || !g_recorder.record_file) {
        return NULL;
    }
    
    // 构建当前文件名
    snprintf(filename, sizeof(filename), "%s/%s_%03d.asc",
             g_recorder.record_dir,
             g_recorder.record_basename,
             g_recorder.file_seq);
    
    return filename;
}

/**
 * @brief 获取统计信息
 */
void can_recorder_get_stats(can_recorder_stats_t *stats)
{
    if (!stats) return;
    
    pthread_mutex_lock(&g_recorder.mutex);
    memcpy(stats, &g_recorder.stats, sizeof(can_recorder_stats_t));
    pthread_mutex_unlock(&g_recorder.mutex);
}

/**
 * @brief CAN帧回调
 */
void can_recorder_frame_callback(int channel, const can_frame_t *frame, void *user_data)
{
    (void)user_data;
    
    // 调试：每100帧打印一次
    static uint32_t debug_count = 0;
    debug_count++;
    
    if (!frame) {
        log_error("录制回调：frame为NULL");
        return;
    }
    
    if (!g_recorder.recording) {
        // 未录制时静默返回，不输出日志
        return;
    }
    
    // 调试：打印前几个帧
    if (g_recorder.stats.total_frames < 5) {
        log_info("录制回调：channel=%d, id=0x%03X, dlc=%d, record_can0=%d, record_can1=%d",
                 channel, frame->can_id, frame->can_dlc, 
                 g_recorder.record_can0, g_recorder.record_can1);
    }
    
    // 检查是否录制此通道
    if (channel == 0 && !g_recorder.record_can0) {
        return;  // 静默跳过
    }
    if (channel == 1 && !g_recorder.record_can1) {
        return;  // 静默跳过
    }
    
    // 录制帧
    append_frame_to_buffer(channel, frame);
    
    // 更新统计
    pthread_mutex_lock(&g_recorder.mutex);
    g_recorder.stats.total_frames++;
    if (channel == 0) {
        g_recorder.stats.can0_frames++;
    } else {
        g_recorder.stats.can1_frames++;
    }
    
    // 每100帧打印一次统计
    if (g_recorder.stats.total_frames % 100 == 0) {
        log_info("录制统计: 总帧数=%llu, CAN0=%llu, CAN1=%llu, 缓冲=%zu字节",
                 g_recorder.stats.total_frames, g_recorder.stats.can0_frames,
                 g_recorder.stats.can1_frames, g_recorder.buffer_used);
    }
    
    pthread_mutex_unlock(&g_recorder.mutex);
}

/**
 * @brief 设置录制通道
 */
void can_recorder_set_channels(bool record_can0, bool record_can1)
{
    pthread_mutex_lock(&g_recorder.mutex);
    g_recorder.record_can0 = record_can0;
    g_recorder.record_can1 = record_can1;
    pthread_mutex_unlock(&g_recorder.mutex);
    
    log_info("录制通道设置: CAN0=%s, CAN1=%s",
             record_can0 ? "启用" : "禁用",
             record_can1 ? "启用" : "禁用");
}

