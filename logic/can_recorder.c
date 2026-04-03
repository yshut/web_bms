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
#include <dirent.h>
#include <errno.h>

/* 默认总大小上限：8 GB */
#define DEFAULT_MAX_TOTAL_BYTES  (8ULL * 1024 * 1024 * 1024)

/* 录制器上下文 */
typedef struct {
    bool recording;                    /* 录制状态 */
    bool record_can0;                  /* 录制CAN0 */
    bool record_can1;                  /* 录制CAN1 */
    
    FILE *record_file;                 /* 当前录制文件 */
    char record_dir[256];              /* 录制目录 */
    char record_basename[128];         /* 文件基础名 */
    int file_seq;                      /* 文件序号 */
    uint64_t max_file_size;            /* 单文件最大大小 */
    uint64_t max_total_bytes;          /* 录制目录总大小上限 */
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
    .max_file_size   = 40ULL * 1024 * 1024,   /* 40MB / 文件 */
    .max_total_bytes = DEFAULT_MAX_TOTAL_BYTES, /* 8GB 总上限 */
    .buffer_size = 256 * 1024,                 /* 256KB 写入缓冲 */
    .flush_interval_ms = 200,                  /* 200ms 刷新 */
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

/* ------------------------------------------------------------------ */
/*  循环录制：扫描目录，按文件名排序，删除最旧的文件直到总大小在上限以下   */
/* ------------------------------------------------------------------ */

#define MAX_RECORD_FILES 4096

typedef struct {
    char path[512];
    uint64_t size;
} rec_file_info_t;

static int rec_file_cmp(const void *a, const void *b)
{
    /* 文件名含时间戳，字典序 = 时间序 */
    return strcmp(((const rec_file_info_t *)a)->path,
                  ((const rec_file_info_t *)b)->path);
}

/**
 * @brief 扫描录制目录，统计总大小，按需删除最旧文件使总大小 <= max_total_bytes
 *        调用者无需持锁（只在 open_record_file 内调用，已在 flush_thread 外）
 */
static void cleanup_old_records(void)
{
    if (g_recorder.max_total_bytes == 0) return;

    DIR *dir = opendir(g_recorder.record_dir);
    if (!dir) return;

    rec_file_info_t *files = (rec_file_info_t *)malloc(
                                MAX_RECORD_FILES * sizeof(rec_file_info_t));
    if (!files) { closedir(dir); return; }

    int count = 0;
    uint64_t total = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < MAX_RECORD_FILES) {
        /* 只处理 .asc 文件 */
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".asc") != 0) continue;

        char fpath[512];
        snprintf(fpath, sizeof(fpath), "%s/%s",
                 g_recorder.record_dir, ent->d_name);

        struct stat st;
        if (stat(fpath, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;

        strncpy(files[count].path, fpath, sizeof(files[count].path) - 1);
        files[count].path[sizeof(files[count].path) - 1] = '\0';
        files[count].size = (uint64_t)st.st_size;
        total += files[count].size;
        count++;
    }
    closedir(dir);

    if (count == 0) { free(files); return; }

    /* 按文件名（时间序）排序 */
    qsort(files, count, sizeof(rec_file_info_t), rec_file_cmp);

    /* 保留当前正在写的文件，不删除 */
    const char *current = g_recorder.stats.current_file;

    /* 删除最旧的文件直到 total <= max_total_bytes */
    int i = 0;
    while (total > g_recorder.max_total_bytes && i < count) {
        /* 不删除当前正在写的文件 */
        if (current[0] && strcmp(files[i].path, current) == 0) {
            i++;
            continue;
        }
        log_info("循环录制：总大小 %.1f GB 超限，删除最旧文件: %s (%.1f MB)",
                 (double)total / (1024.0*1024.0*1024.0),
                 files[i].path,
                 (double)files[i].size / (1024.0*1024.0));
        if (remove(files[i].path) == 0) {
            total -= files[i].size;
        } else {
            log_warn("删除旧录制文件失败: %s - %s", files[i].path, strerror(errno));
        }
        i++;
    }

    log_info("录制目录总大小: %.2f GB / %.2f GB (文件数: %d)",
             (double)total / (1024.0*1024.0*1024.0),
             (double)g_recorder.max_total_bytes / (1024.0*1024.0*1024.0),
             count - i);

    free(files);
}

/**
 * @brief 生成ASC文件头（使用实际系统时间）
 */
static void generate_asc_header(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    /* 格式化日期部分，如 "Thu Apr  2 17:28:00 2026" */
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%a %b %e %H:%M:%S %Y", tm_info);

    snprintf(buffer, size,
             "date %s\n"
             "base hex  timestamps absolute\n"
             "internal events logged\n"
             "Begin Triggerblock %s\n"
             "   0.000000 Start of measurement\n",
             date_str,
             date_str);
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
        fprintf(g_recorder.record_file, "End TriggerBlock\n");
        fclose(g_recorder.record_file);
        g_recorder.record_file = NULL;
    }

    // 超总限时删除最旧文件（新文件打开前执行）
    cleanup_old_records();

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
    
    if (g_recorder.buffer_used > 0 && g_recorder.record_file) {
        size_t written = fwrite(g_recorder.write_buffer, 1, g_recorder.buffer_used, g_recorder.record_file);
        fflush(g_recorder.record_file);
        
        if (written == g_recorder.buffer_used) {
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

        if (config->max_total_bytes > 0) {
            g_recorder.max_total_bytes = config->max_total_bytes;
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
    
    /* 等待系统时钟有效（年份 >= 2020），最长等待 30 秒
     * 设备启动时 RTC 可能还未同步，time(NULL) 会返回 epoch 附近的值 */
    {
        time_t now;
        int waited = 0;
        do {
            now = time(NULL);
            struct tm *t = localtime(&now);
            if (t && (t->tm_year + 1900) >= 2020) break;
            if (waited == 0)
                log_warn("系统时钟无效(year=%d)，等待时钟同步...",
                         t ? t->tm_year + 1900 : 0);
            sleep(1);
            waited++;
        } while (waited < 30);
    }

    // 生成文件基础名（时间戳）
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    if (!tm_info || (tm_info->tm_year + 1900) < 2020) {
        /* 时钟仍然无效，使用单调计数器代替日期 */
        static unsigned int seq_counter = 0;
        snprintf(g_recorder.record_basename, sizeof(g_recorder.record_basename),
                 "can_seq%06u", ++seq_counter);
        log_warn("系统时钟仍然无效，使用序列号命名: %s", g_recorder.record_basename);
    } else {
        snprintf(g_recorder.record_basename, sizeof(g_recorder.record_basename),
                 "can_%04d%02d%02d_%02d%02d%02d",
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }
    
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
    
    if (!frame) {
        log_error("录制回调：frame为NULL");
        return;
    }
    
    if (!g_recorder.recording) {
        // 未录制时静默返回，不输出日志
        return;
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

