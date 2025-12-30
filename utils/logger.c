/**
 * @file logger.c
 * @brief 日志系统实现
 */

#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

/* 全局变量 */
static FILE *g_log_file = NULL;
static log_level_t g_log_level = LOG_LEVEL_INFO;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 日志级别名称 */
static const char *log_level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
};

/* 日志级别颜色（ANSI转义码） */
static const char *log_level_colors[] = {
    "\033[36m",  // DEBUG - 青色
    "\033[32m",  // INFO - 绿色
    "\033[33m",  // WARN - 黄色
    "\033[31m",  // ERROR - 红色
};

#define COLOR_RESET "\033[0m"

/**
 * @brief 初始化日志系统
 */
int log_init(const char *log_file, log_level_t level)
{
    g_log_level = level;
    
    if (log_file != NULL) {
        g_log_file = fopen(log_file, "a");
        if (g_log_file == NULL) {
            fprintf(stderr, "警告: 无法打开日志文件: %s\n", log_file);
            /* 继续运行，只输出到控制台 */
        }
    }
    
    return 0;
}

/**
 * @brief 反初始化日志系统
 */
void log_deinit(void)
{
    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

/**
 * @brief 写日志
 */
void log_write(log_level_t level, const char *file, int line, 
               const char *func, const char *fmt, ...)
{
    if (level < g_log_level) {
        return;
    }
    
    pthread_mutex_lock(&g_log_mutex);
    
    /* 获取当前时间 */
    time_t now;
    struct tm *tm_info;
    char time_str[64];
    
    time(&now);
    tm_info = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    /* 提取文件名（去掉路径） */
    const char *filename = strrchr(file, '/');
    if (filename == NULL) {
        filename = file;
    } else {
        filename++;
    }
    
    /* 格式化日志消息 */
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    
    /* 输出到控制台（带颜色） */
    fprintf(stdout, "%s[%s]%s %s [%s:%d %s()] %s\n",
            log_level_colors[level],
            log_level_names[level],
            COLOR_RESET,
            time_str,
            filename,
            line,
            func,
            message);
    fflush(stdout);
    
    /* 输出到文件（无颜色） */
    if (g_log_file != NULL) {
        fprintf(g_log_file, "[%s] %s [%s:%d %s()] %s\n",
                log_level_names[level],
                time_str,
                filename,
                line,
                func,
                message);
        fflush(g_log_file);
    }
    
    pthread_mutex_unlock(&g_log_mutex);
}

/**
 * @brief 设置日志级别
 */
void log_set_level(log_level_t level)
{
    g_log_level = level;
}


