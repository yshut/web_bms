/**
 * @file uds_handler.h
 * @brief UDS flashing handler (threaded) - LVGL-friendly callbacks
 */

#ifndef UDS_HANDLER_H
#define UDS_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *interface;     /* can0 / can1 */
    uint32_t tx_id;            /* request ID */
    uint32_t rx_id;            /* response ID */
    uint32_t block_size;       /* transfer data block size (bytes) */
    const char *s19_path;      /* path to S19 file */
} uds_config_t;

typedef void (*uds_progress_cb)(int total_percent, int seg_index, int seg_total, void *user_data);
typedef void (*uds_log_cb)(const char *line, void *user_data);

int uds_init(const uds_config_t *cfg);
int uds_start(void);
void uds_stop(void);
void uds_deinit(void);
bool uds_is_running(void);

void uds_register_progress_cb(uds_progress_cb cb, void *user_data);
void uds_register_log_cb(uds_log_cb cb, void *user_data);

/* 远程控制接口 */
int uds_set_file_path(const char *path);
int uds_start_flash(void);
void uds_stop_flash(void);

/* 网页端/远程：设置 UDS 参数（不要求已选择文件） */
int uds_set_params(const char *iface, uint32_t tx_id, uint32_t rx_id, uint32_t block_size);

#endif /* UDS_HANDLER_H */
