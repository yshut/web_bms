/**
 * @file ui_remote_control.h
 * @brief UI远程控制接口 - 允许Web端控制所有页面按钮
 */

#ifndef UI_REMOTE_CONTROL_H
#define UI_REMOTE_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化UI远程控制
 */
int ui_remote_init(void);

/**
 * @brief 清理UI远程控制
 */
void ui_remote_deinit(void);

/* ========== CAN监控页面远程控制 ========== */

/**
 * @brief 远程点击CAN监控页面的"开始"按钮
 */
void ui_remote_can_click_start(void);

/**
 * @brief 远程点击CAN监控页面的"停止"按钮
 */
void ui_remote_can_click_stop(void);

/**
 * @brief 远程点击CAN监控页面的"发送"按钮
 * @param id CAN ID (十六进制字符串，如"123")
 * @param data 数据 (十六进制字符串，如"0102030405060708")
 * @param channel 通道 (0或1)
 * @param extended 是否扩展帧
 */
void ui_remote_can_send_frame(const char *id, const char *data, int channel, bool extended);

/**
 * @brief 远程点击CAN监控页面的"清除"按钮
 */
void ui_remote_can_click_clear(void);

/**
 * @brief 远程点击CAN监控页面的"录制"按钮
 */
void ui_remote_can_click_record(void);

/**
 * @brief 远程设置CAN监控页面的波特率
 * @param channel 通道 (0或1)
 * @param bitrate 波特率
 */
void ui_remote_can_set_bitrate(int channel, uint32_t bitrate);

/* ========== UDS诊断页面远程控制 ========== */

/**
 * @brief 远程点击UDS页面的"选择文件"按钮
 * @param file_path S19文件路径
 */
void ui_remote_uds_select_file(const char *file_path);

/**
 * @brief 远程点击UDS页面的"开始刷写"按钮
 */
void ui_remote_uds_click_start(void);

/**
 * @brief 远程点击UDS页面的"停止"按钮
 */
void ui_remote_uds_click_stop(void);

/**
 * @brief 远程设置UDS页面的波特率
 * @param bitrate 波特率
 */
void ui_remote_uds_set_bitrate(uint32_t bitrate);

/**
 * @brief 远程同步UDS页面参数
 */
void ui_remote_uds_set_params(const char *iface, uint32_t bitrate, uint32_t tx_id, uint32_t rx_id, uint32_t block_size);

/**
 * @brief 远程清除UDS日志
 */
void ui_remote_uds_clear_log(void);

/**
 * @brief 获取UDS页面状态JSON
 */
int ui_remote_uds_get_state_json(char *buf, size_t size);

/* ========== WiFi页面远程控制 ========== */

/**
 * @brief 远程点击WiFi页面的"扫描"按钮
 */
void ui_remote_wifi_click_scan(void);

/**
 * @brief 远程连接到指定WiFi
 * @param ssid WiFi名称
 * @param password WiFi密码
 */
void ui_remote_wifi_connect(const char *ssid, const char *password);

/**
 * @brief 远程断开当前WiFi
 */
void ui_remote_wifi_disconnect(void);

/**
 * @brief 远程删除保存的WiFi
 * @param ssid WiFi名称
 */
void ui_remote_wifi_forget(const char *ssid);

/* ========== 文件管理页面远程控制 ========== */

/**
 * @brief 远程刷新文件列表
 */
void ui_remote_file_refresh(void);

/**
 * @brief 远程进入指定目录
 * @param path 目录路径
 */
void ui_remote_file_enter_dir(const char *path);

/**
 * @brief 远程返回上级目录
 */
void ui_remote_file_go_back(void);

/**
 * @brief 远程删除文件/目录
 * @param path 路径
 */
void ui_remote_file_delete(const char *path);

/**
 * @brief 远程重命名文件/目录
 * @param old_path 旧路径
 * @param new_name 新名称
 */
void ui_remote_file_rename(const char *old_path, const char *new_name);

/* ========== 主页远程控制 ========== */

/**
 * @brief 远程导航到指定页面
 * @param page_name 页面名称: "home", "can", "uds", "wifi", "file"
 */
void ui_remote_navigate(const char *page_name);

#ifdef __cplusplus
}
#endif

#endif /* UI_REMOTE_CONTROL_H */

