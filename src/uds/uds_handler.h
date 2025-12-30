#ifndef UDS_HANDLER_H
#define UDS_HANDLER_H

#include <stdbool.h>

// UDS处理器回调
typedef void (*uds_progress_callback_t)(int percent);
typedef void (*uds_log_callback_t)(const char *log);

// 初始化UDS处理器
int uds_handler_init(void);

// 启动UDS下载
int uds_handler_start(const char *file_path);

// 停止UDS下载
void uds_handler_stop(void);

// 设置回调
void uds_handler_set_progress_callback(uds_progress_callback_t callback);
void uds_handler_set_log_callback(uds_log_callback_t callback);

// 获取状态
bool uds_handler_is_running(void);

#endif // UDS_HANDLER_H

