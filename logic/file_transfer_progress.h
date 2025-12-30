/**
 * @file file_transfer_progress.h
 * @brief 文件传输进度跟踪模块
 */

#ifndef FILE_TRANSFER_PROGRESS_H
#define FILE_TRANSFER_PROGRESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 传输类型
 */
typedef enum {
    TRANSFER_TYPE_UPLOAD = 0,
    TRANSFER_TYPE_DOWNLOAD
} transfer_type_t;

/**
 * @brief 传输状态
 */
typedef enum {
    TRANSFER_STATUS_IDLE = 0,
    TRANSFER_STATUS_IN_PROGRESS,
    TRANSFER_STATUS_COMPLETED,
    TRANSFER_STATUS_FAILED,
    TRANSFER_STATUS_CANCELLED
} transfer_status_t;

/**
 * @brief 传输进度信息
 */
typedef struct {
    char filename[256];
    transfer_type_t type;
    transfer_status_t status;
    size_t bytes_transferred;
    size_t total_bytes;
    uint32_t percent;
    uint32_t speed_bps;  // bytes per second
    char error_msg[256];
    uint64_t start_time_ms;
    uint64_t elapsed_time_ms;
} transfer_progress_t;

/**
 * @brief 初始化文件传输进度跟踪
 */
int file_transfer_progress_init(void);

/**
 * @brief 清理文件传输进度跟踪
 */
void file_transfer_progress_deinit(void);

/**
 * @brief 开始新的传输任务
 * @param filename 文件名
 * @param type 传输类型
 * @param total_bytes 总字节数
 * @return 0成功，-1失败
 */
int file_transfer_progress_start(const char *filename, transfer_type_t type, size_t total_bytes);

/**
 * @brief 更新传输进度
 * @param bytes_transferred 已传输字节数
 * @return 0成功，-1失败
 */
int file_transfer_progress_update(size_t bytes_transferred);

/**
 * @brief 完成传输
 * @return 0成功，-1失败
 */
int file_transfer_progress_complete(void);

/**
 * @brief 传输失败
 * @param error_msg 错误消息
 * @return 0成功，-1失败
 */
int file_transfer_progress_fail(const char *error_msg);

/**
 * @brief 取消传输
 * @return 0成功，-1失败
 */
int file_transfer_progress_cancel(void);

/**
 * @brief 获取当前传输进度
 * @param progress 输出进度信息
 * @return 0成功，-1失败
 */
int file_transfer_progress_get(transfer_progress_t *progress);

/**
 * @brief 获取当前传输进度（JSON格式）
 * @return JSON字符串（需要调用者free），失败返回NULL
 */
char* file_transfer_progress_get_json(void);

#ifdef __cplusplus
}
#endif

#endif // FILE_TRANSFER_PROGRESS_H

