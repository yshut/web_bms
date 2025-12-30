/**
 * @file logger.h
 * @brief 日志系统
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

/**
 * @brief 日志级别
 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
} log_level_t;

/**
 * @brief 初始化日志系统
 * @param log_file 日志文件路径，NULL表示只输出到控制台
 * @param level 日志级别
 * @return int 成功返回0，失败返回负数
 */
int log_init(const char *log_file, log_level_t level);

/**
 * @brief 反初始化日志系统
 */
void log_deinit(void);

/**
 * @brief 写日志
 * @param level 日志级别
 * @param file 源文件名
 * @param line 行号
 * @param func 函数名
 * @param fmt 格式化字符串
 * @param ... 可变参数
 */
void log_write(log_level_t level, const char *file, int line, 
               const char *func, const char *fmt, ...);

/**
 * @brief 设置日志级别
 * @param level 日志级别
 */
void log_set_level(log_level_t level);

/* 便捷宏定义 */
#define log_debug(fmt, ...) \
    log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define log_info(fmt, ...) \
    log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define log_warn(fmt, ...) \
    log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define log_error(fmt, ...) \
    log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#endif /* LOGGER_H */

