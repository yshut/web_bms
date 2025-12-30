/**
 * @file ws_command_handler.c
 * @brief WebSocket命令处理器实现
 */

#include "ws_command_handler.h"
#include "ws_client.h"
#include "can_handler.h"
#include "can_recorder.h"
#include "file_transfer.h"
#include "uds_handler.h"
#include "ui_remote_control.h"
#include "../logic/app_manager.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* 前向声明 */
static int handle_navigation_cmd(const char *cmd, const char *request_id);
static int handle_can_cmd(const char *json, const char *cmd, const char *request_id);
static int handle_can_advanced_cmd(const char *json, const char *cmd, const char *request_id);
static int handle_uds_cmd(const char *json, const char *cmd, const char *request_id);
static int handle_uds_advanced_cmd(const char *json, const char *cmd, const char *request_id);
static int handle_file_cmd(const char *json, const char *cmd, const char *request_id);
static int handle_file_progress_cmd(const char *json, const char *cmd, const char *request_id);
static int handle_wifi_cmd(const char *json, const char *cmd, const char *request_id);
static int handle_wifi_status_cmd(const char *json, const char *cmd, const char *request_id);
static int handle_ui_cmd(const char *json, const char *cmd, const char *request_id);
static int handle_system_cmd(const char *json, const char *cmd, const char *request_id);

/* 简单的JSON字段提取 */
static char* extract_json_string(const char *json, const char *key)
{
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    
    const char *start = strstr(json, search_key);
    if (!start) return NULL;
    
    start = strchr(start + strlen(search_key), ':');
    if (!start) return NULL;
    
    // 跳过空格和引号
    start++;
    while (*start == ' ' || *start == '\t' || *start == '"') start++;
    
    const char *end = start;
    bool in_string = (*(start - 1) == '"');
    
    if (in_string) {
        end = strchr(start, '"');
    } else {
        // 数字或布尔值
        while (*end && *end != ',' && *end != '}' && *end != ' ') end++;
    }
    
    if (!end || end == start) return NULL;
    
    size_t len = (size_t)(end - start);
    char *result = (char*)malloc(len + 1);
    if (result) {
        strncpy(result, start, len);
        result[len] = '\0';
    }
    
    return result;
}

static int extract_json_int(const char *json, const char *key, int default_value)
{
    char *str = extract_json_string(json, key);
    if (!str) return default_value;
    
    int value = atoi(str);
    free(str);
    return value;
}

static bool extract_json_bool(const char *json, const char *key, bool default_value)
{
    char *str = extract_json_string(json, key);
    if (!str) return default_value;
    
    bool value = (strcmp(str, "true") == 0 || strcmp(str, "1") == 0);
    free(str);
    return value;
}

/**
 * @brief 初始化命令处理器
 */
int ws_command_handler_init(void)
{
    log_info("WebSocket命令处理器初始化");
    return 0;
}

/**
 * @brief 反初始化命令处理器
 */
void ws_command_handler_deinit(void)
{
    log_info("WebSocket命令处理器已清理");
}

/**
 * @brief 发送OK响应
 */
void ws_command_send_ok(const char *request_id, const char *extra_data)
{
    char response[1024];
    
    if (extra_data && request_id) {
        snprintf(response, sizeof(response), 
                "{\"ok\":true,\"id\":\"%s\",\"data\":%s}", 
                request_id, extra_data);
    } else if (request_id) {
        snprintf(response, sizeof(response), 
                "{\"ok\":true,\"id\":\"%s\"}", 
                request_id);
    } else if (extra_data) {
        snprintf(response, sizeof(response), 
                "{\"ok\":true,\"data\":%s}", 
                extra_data);
    } else {
        snprintf(response, sizeof(response), "{\"ok\":true}");
    }
    
    ws_client_send_json(response);
}

/**
 * @brief 发送ERROR响应
 */
void ws_command_send_error(const char *request_id, const char *error_msg)
{
    char response[512];
    
    if (request_id) {
        snprintf(response, sizeof(response), 
                "{\"ok\":false,\"error\":\"%s\",\"id\":\"%s\"}", 
                error_msg, request_id);
    } else {
        snprintf(response, sizeof(response), 
                "{\"ok\":false,\"error\":\"%s\"}", 
                error_msg);
    }
    
    ws_client_send_json(response);
}

/**
 * @brief 处理页面导航命令
 */
static int handle_navigation_cmd(const char *cmd, const char *request_id)
{
    if (strcmp(cmd, "show_home") == 0) {
        app_manager_switch_to_page(APP_PAGE_HOME);
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "show_can") == 0) {
        app_manager_switch_to_page(APP_PAGE_CAN_MONITOR);
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "show_uds") == 0) {
        app_manager_switch_to_page(APP_PAGE_UDS);
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "show_wifi") == 0) {
        app_manager_switch_to_page(APP_PAGE_WIFI);
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    return -1;  // 未识别的命令
}

/**
 * @brief 处理CAN控制命令
 */
static int handle_can_cmd(const char *json, const char *cmd, const char *request_id)
{
    if (strcmp(cmd, "can_scan") == 0) {
        // 扫描CAN接口
        log_info("扫描CAN接口...");
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "can_configure") == 0) {
        // 配置CAN接口
        log_info("配置CAN接口...");
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "can_start") == 0) {
        if (can_handler_start() == 0) {
            ws_command_send_ok(request_id, NULL);
        } else {
            ws_command_send_error(request_id, "CAN start failed");
        }
        return 0;
    }
    
    if (strcmp(cmd, "can_stop") == 0) {
        can_handler_stop();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "can_clear") == 0) {
        // 清除CAN消息列表
        extern void ui_can_monitor_clear_messages_async(void);
        ui_can_monitor_clear_messages_async();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "can_get_config") == 0) {
        // 获取当前CAN配置（波特率等）
        uint32_t bitrate0 = 500000;
        uint32_t bitrate1 = 500000;
        
        // 尝试从can_handler获取实际配置
        can_handler_get_bitrate_dual(&bitrate0, &bitrate1);
        
        char data[256];
        snprintf(data, sizeof(data), 
                "{\"can1_bitrate\":%u,\"can2_bitrate\":%u,"
                "\"can0\":%u,\"can1\":%u}",
                bitrate0, bitrate1, bitrate0, bitrate1);
        ws_command_send_ok(request_id, data);
        return 0;
    }
    
    if (strcmp(cmd, "can_set_bitrates") == 0) {
        // 注意：API使用can1/can2，设备端使用can0/can1
        int bitrate0 = extract_json_int(json, "can1", 500000);  // API的can1对应设备的can0
        int bitrate1 = extract_json_int(json, "can2", 500000);  // API的can2对应设备的can1
        
        // 重新配置CAN接口
        if (can_handler_configure("can0", (uint32_t)bitrate0) == 0 &&
            can_handler_configure("can1", (uint32_t)bitrate1) == 0) {
            char data[128];
            snprintf(data, sizeof(data), "{\"can0\":%d,\"can1\":%d}", bitrate0, bitrate1);
            ws_command_send_ok(request_id, data);
        } else {
            ws_command_send_error(request_id, "Failed to set bitrates");
        }
        return 0;
    }
    
    if (strcmp(cmd, "can_get_status") == 0) {
        // 获取CAN监控和录制状态
        bool is_running = can_handler_is_running();
        bool is_recording = can_recorder_is_recording();
        
        char data[256];
        snprintf(data, sizeof(data), 
                "{\"is_running\":%s,\"running\":%s,\"is_recording\":%s,\"recording\":%s}",
                is_running ? "true" : "false",
                is_running ? "true" : "false",
                is_recording ? "true" : "false",
                is_recording ? "true" : "false");
        ws_command_send_ok(request_id, data);
        return 0;
    }
    
    if (strcmp(cmd, "can_record_start") == 0) {
        // 开始录制CAN报文
        if (can_recorder_is_recording()) {
            ws_command_send_error(request_id, "Already recording");
        } else if (can_recorder_start() == 0) {
            ws_command_send_ok(request_id, NULL);
        } else {
            ws_command_send_error(request_id, "Failed to start recording");
        }
        return 0;
    }
    
    if (strcmp(cmd, "can_record_stop") == 0) {
        // 停止录制CAN报文
        if (!can_recorder_is_recording()) {
            ws_command_send_error(request_id, "Not recording");
        } else {
            const char *filename = can_recorder_get_filename();
            char data[512];
            snprintf(data, sizeof(data), "{\"filename\":\"%s\"}", filename ? filename : "");
            
            can_recorder_stop();
            ws_command_send_ok(request_id, data);
        }
        return 0;
    }
    
    if (strcmp(cmd, "can_send_frame") == 0) {
        char *text = extract_json_string(json, "text");
        if (text) {
            // 解析并发送CAN帧
            // 格式: "can0 123#0102030405060708" 或 "18FF45F4#0102030405060708"
            log_info("发送CAN帧: %s", text);
            
            // 简单解析: ID#DATA
            char *sep = strchr(text, '#');
            if (sep) {
                *sep = '\0';
                char *id_str = text;
                char *data_str = sep + 1;
                
                // 跳过接口名（如果有）
                char *space = strchr(id_str, ' ');
                if (space) {
                    id_str = space + 1;
                }
                
                // 解析CAN ID
                uint32_t can_id = 0;
                sscanf(id_str, "%x", &can_id);
                
                // 解析数据
                can_frame_t frame;
                frame.can_id = can_id;
                frame.can_dlc = 0;
                
                char *p = data_str;
                while (*p && frame.can_dlc < 8) {
                    unsigned int byte_val;
                    if (sscanf(p, "%2x", &byte_val) == 1) {
                        frame.data[frame.can_dlc++] = (uint8_t)byte_val;
                        p += 2;
                    } else {
                        break;
                    }
                }
                
                // 发送帧
                if (can_handler_send(&frame) == 0) {
                    ws_command_send_ok(request_id, NULL);
                } else {
                    ws_command_send_error(request_id, "Failed to send frame");
                }
            } else {
                ws_command_send_error(request_id, "Invalid frame format");
            }
            
            free(text);
        } else {
            ws_command_send_error(request_id, "Missing frame text");
        }
        return 0;
    }
    
    if (strcmp(cmd, "can_recent_frames") == 0) {
        int limit = extract_json_int(json, "limit", 50);
        
        // 获取最近的CAN帧
        extern int can_frame_buffer_get_json(char *buffer, int buffer_size, int limit);
        
        // 分配较大的缓冲区用于JSON数据（每帧约100字节，50帧需要5KB）
        char *large_buffer = (char*)malloc(8192);
        if (!large_buffer) {
            ws_command_send_error(request_id, "Memory allocation failed");
            return 0;
        }
        
        int count = can_frame_buffer_get_json(large_buffer, 8192, limit);
        
        // 构建完整的响应（包含frames数组）
        char *response = (char*)malloc(8192 + 128);
        if (response) {
            snprintf(response, 8192 + 128, "{\"frames\":%s,\"count\":%d}", large_buffer, count);
            ws_command_send_ok(request_id, response);
            free(response);
        } else {
            ws_command_send_ok(request_id, "{\"frames\":[]}");
        }
        
        free(large_buffer);
        return 0;
    }
    
    // ========== 新增：按钮级控制命令 ==========
    
    if (strcmp(cmd, "can_click_start") == 0) {
        // 远程点击"开始"按钮
        ui_remote_can_click_start();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "can_click_stop") == 0) {
        // 远程点击"停止"按钮
        ui_remote_can_click_stop();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "can_click_clear") == 0) {
        // 远程点击"清除"按钮
        ui_remote_can_click_clear();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "can_click_record") == 0) {
        // 远程点击"录制"按钮
        ui_remote_can_click_record();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "can_click_send") == 0) {
        // 远程点击"发送"按钮（带参数）
        char *id = extract_json_string(json, "id");
        char *data = extract_json_string(json, "data");
        int channel = extract_json_int(json, "channel", 0);
        bool extended = extract_json_bool(json, "extended", false);
        
        if (id && data) {
            ui_remote_can_send_frame(id, data, channel, extended);
            ws_command_send_ok(request_id, NULL);
        } else {
            ws_command_send_error(request_id, "Missing id or data");
        }
        
        if (id) free(id);
        if (data) free(data);
        return 0;
    }
    
    if (strcmp(cmd, "can_set_channel_bitrate") == 0) {
        // 远程设置单个通道波特率
        int channel = extract_json_int(json, "channel", 0);
        int bitrate = extract_json_int(json, "bitrate", 500000);
        
        ui_remote_can_set_bitrate(channel, (uint32_t)bitrate);
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    return -1;
}

/**
 * @brief 处理UDS控制命令
 */
static int handle_uds_cmd(const char *json, const char *cmd, const char *request_id)
{
    if (strcmp(cmd, "uds_set_file") == 0) {
        char *path = extract_json_string(json, "path");
        if (path) {
            // 设置UDS文件路径
            extern int uds_set_file_path(const char *path);
            if (uds_set_file_path(path) == 0) {
                ws_command_send_ok(request_id, NULL);
            } else {
                ws_command_send_error(request_id, "Invalid file path");
            }
            free(path);
        } else {
            ws_command_send_error(request_id, "Missing file path");
        }
        return 0;
    }
    
    if (strcmp(cmd, "uds_progress") == 0) {
        // 获取UDS刷写进度
        char progress[512];
        snprintf(progress, sizeof(progress),
                "{\"total_percent\":0,\"status\":\"idle\"}");
        ws_command_send_ok(request_id, progress);
        return 0;
    }
    
    if (strcmp(cmd, "uds_logs") == 0) {
        int limit = extract_json_int(json, "limit", 100);
        
        // 获取UDS日志（简单实现）
        char logs[256];
        snprintf(logs, sizeof(logs), "{\"lines\":[]}");
        ws_command_send_ok(request_id, logs);
        return 0;
    }
    
    if (strcmp(cmd, "uds_scan") == 0) {
        // 扫描CAN接口
        log_info("UDS扫描CAN接口...");
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "uds_list") == 0) {
        char *dir = extract_json_string(json, "dir");
        if (!dir) dir = strdup("/mnt/SDCARD");
        
        char *files = list_s19_files_json(dir);
        if (files) {
            ws_command_send_ok(request_id, files);
            free(files);
        } else {
            ws_command_send_error(request_id, "Failed to list files");
        }
        free(dir);
        return 0;
    }
    
    if (strcmp(cmd, "uds_upload") == 0) {
        char *path = extract_json_string(json, "path");
        char *b64_data = extract_json_string(json, "data");
        
        if (path && b64_data) {
            size_t bin_len;
            uint8_t *bin_data = base64_decode(b64_data, &bin_len);
            
            if (bin_data) {
                if (file_write(path, bin_data, bin_len) == 0) {
                    char data[512];
                    snprintf(data, sizeof(data), "{\"path\":\"%s\",\"size\":%zu}", path, bin_len);
                    ws_command_send_ok(request_id, data);
                } else {
                    ws_command_send_error(request_id, "Failed to write file");
                }
                free(bin_data);
            } else {
                ws_command_send_error(request_id, "Failed to decode data");
            }
        } else {
            ws_command_send_error(request_id, "Missing path or data");
        }
        
        if (path) free(path);
        if (b64_data) free(b64_data);
        return 0;
    }
    
    if (strcmp(cmd, "uds_start") == 0) {
        // 开始UDS刷写
        extern int uds_start_flash(void);
        if (uds_start_flash() == 0) {
            ws_command_send_ok(request_id, NULL);
        } else {
            ws_command_send_error(request_id, "Failed to start flashing");
        }
        return 0;
    }
    
    if (strcmp(cmd, "uds_stop") == 0) {
        // 停止UDS刷写
        extern void uds_stop_flash(void);
        uds_stop_flash();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    // ========== 新增：UDS按钮级控制命令 ==========
    
    if (strcmp(cmd, "uds_click_select_file") == 0) {
        // 远程点击"选择文件"按钮
        char *path = extract_json_string(json, "path");
        if (path) {
            ui_remote_uds_select_file(path);
            ws_command_send_ok(request_id, NULL);
            free(path);
        } else {
            ws_command_send_error(request_id, "Missing file path");
        }
        return 0;
    }
    
    if (strcmp(cmd, "uds_click_start") == 0) {
        // 远程点击"开始刷写"按钮
        ui_remote_uds_click_start();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "uds_click_stop") == 0) {
        // 远程点击"停止"按钮
        ui_remote_uds_click_stop();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "uds_set_bitrate") == 0) {
        // 远程设置UDS波特率
        int bitrate = extract_json_int(json, "bitrate", 500000);
        ui_remote_uds_set_bitrate((uint32_t)bitrate);
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "uds_clear_log") == 0) {
        // 远程清除日志
        ui_remote_uds_clear_log();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    return -1;
}

/**
 * @brief 处理文件管理命令
 */
static int handle_file_cmd(const char *json, const char *cmd, const char *request_id)
{
    if (strcmp(cmd, "fs_list") == 0) {
        char *path = extract_json_string(json, "path");
        if (!path) path = strdup("/mnt/SDCARD");
        
        char *list_json = file_list_directory_json(path);
        if (list_json) {
            ws_command_send_ok(request_id, list_json);
            free(list_json);
        } else {
            ws_command_send_error(request_id, "Failed to list directory");
        }
        free(path);
        return 0;
    }
    
    if (strcmp(cmd, "fs_mkdir") == 0) {
        char *path = extract_json_string(json, "path");
        if (path) {
            if (file_mkdir_recursive(path) == 0) {
                char data[512];
                snprintf(data, sizeof(data), "{\"path\":\"%s\"}", path);
                ws_command_send_ok(request_id, data);
            } else {
                ws_command_send_error(request_id, "Failed to create directory");
            }
            free(path);
        } else {
            ws_command_send_error(request_id, "Missing path");
        }
        return 0;
    }
    
    if (strcmp(cmd, "fs_delete") == 0) {
        char *path = extract_json_string(json, "path");
        if (path) {
            if (file_delete_recursive(path) == 0) {
                char data[512];
                snprintf(data, sizeof(data), "{\"path\":\"%s\"}", path);
                ws_command_send_ok(request_id, data);
            } else {
                ws_command_send_error(request_id, "Failed to delete");
            }
            free(path);
        } else {
            ws_command_send_error(request_id, "Missing path");
        }
        return 0;
    }
    
    if (strcmp(cmd, "fs_rename") == 0) {
        char *path = extract_json_string(json, "path");
        char *new_name = extract_json_string(json, "new_name");
        
        if (path && new_name) {
            if (file_rename(path, new_name) == 0) {
                ws_command_send_ok(request_id, NULL);
            } else {
                ws_command_send_error(request_id, "Failed to rename");
            }
        } else {
            ws_command_send_error(request_id, "Missing path or new_name");
        }
        
        if (path) free(path);
        if (new_name) free(new_name);
        return 0;
    }
    
    if (strcmp(cmd, "fs_upload") == 0) {
        char *path = extract_json_string(json, "path");
        char *b64_data = extract_json_string(json, "data");
        
        if (path && b64_data) {
            size_t bin_len;
            uint8_t *bin_data = base64_decode(b64_data, &bin_len);
            
            if (bin_data) {
                if (file_write(path, bin_data, bin_len) == 0) {
                    char data[512];
                    snprintf(data, sizeof(data), "{\"path\":\"%s\",\"size\":%zu}", path, bin_len);
                    ws_command_send_ok(request_id, data);
                } else {
                    ws_command_send_error(request_id, "Failed to write file");
                }
                free(bin_data);
            } else {
                ws_command_send_error(request_id, "Failed to decode data");
            }
        } else {
            ws_command_send_error(request_id, "Missing path or data");
        }
        
        if (path) free(path);
        if (b64_data) free(b64_data);
        return 0;
    }
    
    if (strcmp(cmd, "fs_read") == 0) {
        char *path = extract_json_string(json, "path");
        if (path) {
            size_t size;
            uint8_t *data = file_read(path, &size);
            
            if (data && size < 1048576) {  // 限制1MB以内
                size_t b64_len;
                char *b64 = base64_encode(data, size, &b64_len);
                
                if (b64) {
                    // 动态分配响应缓冲区
                    size_t resp_size = 256 + b64_len;
                    char *response = (char*)malloc(resp_size);
                    if (response) {
                        snprintf(response, resp_size,
                                "{\"name\":\"%s\",\"size\":%zu,\"data\":\"%s\"}",
                                path, size, b64);
                        ws_command_send_ok(request_id, response);
                        free(response);
                    }
                    free(b64);
                } else {
                    ws_command_send_error(request_id, "Failed to encode data");
                }
                free(data);
            } else {
                if (data) free(data);
                ws_command_send_error(request_id, "File too large or read failed");
            }
            free(path);
        } else {
            ws_command_send_error(request_id, "Missing path");
        }
        return 0;
    }
    
    if (strcmp(cmd, "fs_read_range") == 0) {
        char *path = extract_json_string(json, "path");
        int offset = extract_json_int(json, "offset", 0);
        int length = extract_json_int(json, "length", 65536);
        
        if (length <= 0) length = 65536;
        if (length > 262144) length = 262144; // 最大256KB
        
        if (path) {
            size_t read_len;
            bool eof;
            uint8_t *data = file_read_range(path, offset, length, &read_len, &eof);
            
            if (data) {
                size_t b64_len;
                char *b64 = base64_encode(data, read_len, &b64_len);
                
                if (b64) {
                    // 动态分配响应缓冲区
                    size_t resp_size = 256 + b64_len;
                    char *response = (char*)malloc(resp_size);
                    if (response) {
                        snprintf(response, resp_size,
                                "{\"name\":\"%s\",\"read\":%zu,\"eof\":%s,\"data\":\"%s\"}",
                                path, read_len, eof ? "true" : "false", b64);
                        ws_command_send_ok(request_id, response);
                        free(response);
                    }
                    free(b64);
                } else {
                    ws_command_send_error(request_id, "Failed to encode data");
                }
                free(data);
            } else {
                ws_command_send_error(request_id, "Failed to read file");
            }
            free(path);
        } else {
            ws_command_send_error(request_id, "Missing path");
        }
        return 0;
    }
    
    if (strcmp(cmd, "fs_stat") == 0) {
        char *path = extract_json_string(json, "path");
        if (path) {
            file_info_t info;
            if (file_get_info(path, &info) == 0) {
                char data[512];
                snprintf(data, sizeof(data),
                        "{\"name\":\"%s\",\"size\":%zu,\"mtime\":%ld,\"is_dir\":%s}",
                        info.name, info.size, info.mtime, info.is_dir ? "true" : "false");
                ws_command_send_ok(request_id, data);
            } else {
                ws_command_send_error(request_id, "File not found");
            }
            free(path);
        } else {
            ws_command_send_error(request_id, "Missing path");
        }
        return 0;
    }
    
    // ========== 新增：文件管理按钮级控制命令 ==========
    
    if (strcmp(cmd, "file_click_refresh") == 0) {
        // 远程点击"刷新"按钮
        ui_remote_file_refresh();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "file_enter_dir") == 0) {
        // 远程进入目录
        char *path = extract_json_string(json, "path");
        if (path) {
            ui_remote_file_enter_dir(path);
            ws_command_send_ok(request_id, NULL);
            free(path);
        } else {
            ws_command_send_error(request_id, "Missing path");
        }
        return 0;
    }
    
    if (strcmp(cmd, "file_go_back") == 0) {
        // 远程返回上级目录
        ui_remote_file_go_back();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "file_click_delete") == 0) {
        // 远程删除文件/目录
        char *path = extract_json_string(json, "path");
        if (path) {
            ui_remote_file_delete(path);
            ws_command_send_ok(request_id, NULL);
            free(path);
        } else {
            ws_command_send_error(request_id, "Missing path");
        }
        return 0;
    }
    
    if (strcmp(cmd, "file_click_rename") == 0) {
        // 远程重命名文件/目录
        char *old_path = extract_json_string(json, "old_path");
        char *new_name = extract_json_string(json, "new_name");
        
        if (old_path && new_name) {
            ui_remote_file_rename(old_path, new_name);
            ws_command_send_ok(request_id, NULL);
        } else {
            ws_command_send_error(request_id, "Missing old_path or new_name");
        }
        
        if (old_path) free(old_path);
        if (new_name) free(new_name);
        return 0;
    }
    
    return -1;
}

/**
 * @brief 处理WiFi控制命令（新增）
 */
static int handle_wifi_cmd(const char *json, const char *cmd, const char *request_id)
{
    if (strcmp(cmd, "wifi_click_scan") == 0) {
        // 远程点击"扫描"按钮
        ui_remote_wifi_click_scan();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "wifi_connect") == 0) {
        // 远程连接WiFi
        char *ssid = extract_json_string(json, "ssid");
        char *password = extract_json_string(json, "password");
        
        if (ssid) {
            ui_remote_wifi_connect(ssid, password);
            ws_command_send_ok(request_id, NULL);
        } else {
            ws_command_send_error(request_id, "Missing ssid");
        }
        
        if (ssid) free(ssid);
        if (password) free(password);
        return 0;
    }
    
    if (strcmp(cmd, "wifi_disconnect") == 0) {
        // 远程断开WiFi
        ui_remote_wifi_disconnect();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "wifi_forget") == 0) {
        // 远程忘记WiFi
        char *ssid = extract_json_string(json, "ssid");
        if (ssid) {
            ui_remote_wifi_forget(ssid);
            ws_command_send_ok(request_id, NULL);
            free(ssid);
        } else {
            ws_command_send_error(request_id, "Missing ssid");
        }
        return 0;
    }
    
    return -1;
}

/**
 * @brief 处理系统命令
 */
static int handle_system_cmd(const char *json, const char *cmd, const char *request_id)
{
    if (strcmp(cmd, "ping") == 0) {
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "get_status") == 0) {
        // 返回系统状态
        char status[512];
        snprintf(status, sizeof(status),
                "{\"device_id\":\"%s\",\"can_running\":%s,\"uptime\":%ld}",
                ws_client_get_device_id(),
                can_handler_is_running() ? "true" : "false",
                time(NULL));
        ws_command_send_ok(request_id, status);
        return 0;
    }
    
    if (strcmp(cmd, "get_bitrates") == 0) {
        uint32_t bitrate0 = 0, bitrate1 = 0;
        can_handler_get_bitrate_dual(&bitrate0, &bitrate1);
        
        char data[128];
        snprintf(data, sizeof(data), "{\"can0\":%u,\"can1\":%u}", bitrate0, bitrate1);
        ws_command_send_ok(request_id, data);
        return 0;
    }
    
    if (strcmp(cmd, "system_info") == 0) {
        // 获取系统信息
        char info[1024];
        FILE *fp = fopen("/proc/cpuinfo", "r");
        char cpu_model[128] = "Unknown";
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "model name")) {
                    char *colon = strchr(line, ':');
                    if (colon) {
                        snprintf(cpu_model, sizeof(cpu_model), "%s", colon + 2);
                        char *newline = strchr(cpu_model, '\n');
                        if (newline) *newline = '\0';
                    }
                    break;
                }
            }
            fclose(fp);
        }
        
        // 获取内存信息
        long total_mem = 0, free_mem = 0;
        fp = fopen("/proc/meminfo", "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "MemTotal:")) {
                    sscanf(line, "MemTotal: %ld kB", &total_mem);
                } else if (strstr(line, "MemAvailable:")) {
                    sscanf(line, "MemAvailable: %ld kB", &free_mem);
                }
            }
            fclose(fp);
        }
        
        snprintf(info, sizeof(info),
                "{\"cpu\":\"%s\",\"mem_total\":%ld,\"mem_free\":%ld,\"uptime\":%ld}",
                cpu_model, total_mem, free_mem, time(NULL));
        ws_command_send_ok(request_id, info);
        return 0;
    }
    
    if (strcmp(cmd, "system_reboot") == 0) {
        log_info("系统重启请求");
        ws_command_send_ok(request_id, NULL);
        
        // 延迟重启，给响应时间
        sleep(1);
        system("reboot");
        return 0;
    }
    
    if (strcmp(cmd, "system_logs") == 0) {
        int limit = extract_json_int(json, "limit", 100);
        
        // 读取系统日志
        char cmd_buf[256];
        snprintf(cmd_buf, sizeof(cmd_buf), "dmesg | tail -n %d", limit);
        
        FILE *fp = popen(cmd_buf, "r");
        if (fp) {
            char *logs = (char*)malloc(16384);
            if (logs) {
                size_t pos = 0;
                pos += snprintf(logs + pos, 16384 - pos, "{\"lines\":[");
                
                char line[512];
                bool first = true;
                while (fgets(line, sizeof(line), fp) && pos < 15000) {
                    // 移除换行符
                    char *newline = strchr(line, '\n');
                    if (newline) *newline = '\0';
                    
                    if (!first) {
                        pos += snprintf(logs + pos, 16384 - pos, ",");
                    }
                    first = false;
                    
                    // 转义引号
                    char escaped[1024];
                    size_t j = 0;
                    for (size_t i = 0; i < strlen(line) && j < sizeof(escaped) - 2; i++) {
                        if (line[i] == '"' || line[i] == '\\') {
                            escaped[j++] = '\\';
                        }
                        escaped[j++] = line[i];
                    }
                    escaped[j] = '\0';
                    
                    pos += snprintf(logs + pos, 16384 - pos, "\"%s\"", escaped);
                }
                
                pos += snprintf(logs + pos, 16384 - pos, "]}");
                ws_command_send_ok(request_id, logs);
                free(logs);
            }
            pclose(fp);
        } else {
            ws_command_send_error(request_id, "Failed to read logs");
        }
        return 0;
    }
    
    return -1;
}

/**
 * @brief 处理UI控制命令
 */
static int handle_ui_cmd(const char *json, const char *cmd, const char *request_id)
{
    if (strcmp(cmd, "ui_screenshot") == 0) {
        log_info("截图请求");
        // TODO: 实现截图功能
        ws_command_send_error(request_id, "Screenshot not implemented yet");
        return 0;
    }
    
    if (strcmp(cmd, "ui_get_state") == 0) {
        // 获取当前UI状态
        app_page_t page = APP_PAGE_HOME;
        
        // 尝试获取当前页面，如果函数不存在则使用默认值
        #ifdef APP_MANAGER_H
        page = app_manager_get_current_page();
        #endif
        
        const char *page_name = "unknown";
        switch (page) {
            case APP_PAGE_HOME: page_name = "home"; break;
            case APP_PAGE_CAN_MONITOR: page_name = "can"; break;
            case APP_PAGE_UDS: page_name = "uds"; break;
            case APP_PAGE_WIFI: page_name = "wifi"; break;
            case APP_PAGE_FILE_MANAGER: page_name = "file"; break;
            case APP_PAGE_WEBSOCKET: page_name = "websocket"; break;
            default: page_name = "unknown"; break;
        }
        
        char state[256];
        snprintf(state, sizeof(state),
                "{\"current_page\":\"%s\",\"can_running\":%s}",
                page_name,
                can_handler_is_running() ? "true" : "false");
        ws_command_send_ok(request_id, state);
        return 0;
    }
    
    if (strcmp(cmd, "ui_get_current_page") == 0) {
        // 获取当前页面
        app_page_t page = APP_PAGE_HOME;
        
        #ifdef APP_MANAGER_H
        page = app_manager_get_current_page();
        #endif
        
        const char *page_name = "unknown";
        switch (page) {
            case APP_PAGE_HOME: page_name = "home"; break;
            case APP_PAGE_CAN_MONITOR: page_name = "can"; break;
            case APP_PAGE_UDS: page_name = "uds"; break;
            case APP_PAGE_WIFI: page_name = "wifi"; break;
            case APP_PAGE_FILE_MANAGER: page_name = "file"; break;
            case APP_PAGE_WEBSOCKET: page_name = "websocket"; break;
            default: page_name = "unknown"; break;
        }
        
        char data[128];
        snprintf(data, sizeof(data), "{\"page\":\"%s\"}", page_name);
        ws_command_send_ok(request_id, data);
        return 0;
    }
    
    if (strcmp(cmd, "ui_click") == 0) {
        int x = extract_json_int(json, "x", 0);
        int y = extract_json_int(json, "y", 0);
        
        log_info("模拟点击: (%d, %d)", x, y);
        // TODO: 实现模拟点击功能
        ws_command_send_error(request_id, "Click simulation not implemented yet");
        return 0;
    }
    
    if (strcmp(cmd, "ui_input_text") == 0) {
        char *text = extract_json_string(json, "text");
        if (text) {
            log_info("输入文本: %s", text);
            // TODO: 实现文本输入功能
            ws_command_send_error(request_id, "Text input not implemented yet");
            free(text);
        } else {
            ws_command_send_error(request_id, "Missing text");
        }
        return 0;
    }
    
    return -1;
}

/**
 * @brief 处理高级CAN命令
 */
static int handle_can_advanced_cmd(const char *json, const char *cmd, const char *request_id)
{
    if (strcmp(cmd, "can_set_filter") == 0) {
        log_info("设置CAN过滤器");
        // TODO: 实现CAN过滤器设置
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    if (strcmp(cmd, "can_record") == 0) {
        char *action = extract_json_string(json, "action");
        char *filename = extract_json_string(json, "filename");
        
        if (action && strcmp(action, "start") == 0) {
            log_info("开始CAN录制: %s", filename ? filename : "default.log");
            // TODO: 实现CAN录制功能
            ws_command_send_ok(request_id, NULL);
        } else if (action && strcmp(action, "stop") == 0) {
            log_info("停止CAN录制");
            // TODO: 停止CAN录制
            ws_command_send_ok(request_id, NULL);
        } else {
            ws_command_send_error(request_id, "Invalid action");
        }
        
        if (action) free(action);
        if (filename) free(filename);
        return 0;
    }
    
    if (strcmp(cmd, "can_replay") == 0) {
        char *filename = extract_json_string(json, "filename");
        if (filename) {
            log_info("回放CAN数据: %s", filename);
            // TODO: 实现CAN回放功能
            ws_command_send_ok(request_id, NULL);
            free(filename);
        } else {
            ws_command_send_error(request_id, "Missing filename");
        }
        return 0;
    }
    
    return -1;
}

/**
 * @brief 处理UDS高级命令
 */
static int handle_uds_advanced_cmd(const char *json, const char *cmd, const char *request_id)
{
    if (strcmp(cmd, "uds_read_dtc") == 0) {
        log_info("读取DTC");
        // TODO: 实现读取DTC功能
        char data[256];
        snprintf(data, sizeof(data), "{\"dtc_codes\":[]}");
        ws_command_send_ok(request_id, data);
        return 0;
    }
    
    if (strcmp(cmd, "uds_clear_dtc") == 0) {
        log_info("清除DTC");
        // TODO: 实现清除DTC功能
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    return -1;
}

/**
 * @brief 处理WiFi状态查询
 */
static int handle_wifi_status_cmd(const char *json, const char *cmd, const char *request_id)
{
    if (strcmp(cmd, "wifi_status") == 0) {
        // 获取WiFi状态
        FILE *fp = popen("iwconfig 2>/dev/null | grep ESSID", "r");
        char ssid[128] = "Not connected";
        bool connected = false;
        
        if (fp) {
            char line[256];
            if (fgets(line, sizeof(line), fp)) {
                char *essid = strstr(line, "ESSID:");
                if (essid) {
                    essid += 7;
                    char *quote_end = strchr(essid, '"');
                    if (quote_end) {
                        size_t len = quote_end - essid - 1;
                        if (len > 0 && len < sizeof(ssid)) {
                            strncpy(ssid, essid + 1, len);
                            ssid[len] = '\0';
                            connected = true;
                        }
                    }
                }
            }
            pclose(fp);
        }
        
        char status[256];
        snprintf(status, sizeof(status),
                "{\"connected\":%s,\"ssid\":\"%s\"}",
                connected ? "true" : "false",
                ssid);
        ws_command_send_ok(request_id, status);
        return 0;
    }
    
    if (strcmp(cmd, "wifi_scan") == 0) {
        log_info("扫描WiFi");
        ui_remote_wifi_click_scan();
        ws_command_send_ok(request_id, NULL);
        return 0;
    }
    
    return -1;
}

/**
 * @brief 处理文件上传进度查询
 */
static int handle_file_progress_cmd(const char *json, const char *cmd, const char *request_id)
{
    if (strcmp(cmd, "fs_upload_progress") == 0) {
        // TODO: 实现文件上传进度跟踪
        char progress[128];
        snprintf(progress, sizeof(progress), "{\"percent\":0,\"bytes\":0,\"total\":0}");
        ws_command_send_ok(request_id, progress);
        return 0;
    }
    
    return -1;
}

/**
 * @brief 处理接收到的JSON命令
 */
int ws_command_handler_process(const char *json_str)
{
    if (!json_str) {
        return -1;
    }
    
    log_debug("处理WebSocket命令: %s", json_str);
    
    // 提取命令和ID
    char *cmd = extract_json_string(json_str, "cmd");
    if (!cmd) {
        log_warn("无效的命令格式（缺少cmd字段）");
        return -1;
    }
    
    char *request_id = extract_json_string(json_str, "id");
    
    int ret = -1;
    
    // 尝试各种命令处理器
    ret = handle_navigation_cmd(cmd, request_id);
    if (ret == 0) goto cleanup;
    
    ret = handle_can_cmd(json_str, cmd, request_id);
    if (ret == 0) goto cleanup;
    
    ret = handle_can_advanced_cmd(json_str, cmd, request_id);
    if (ret == 0) goto cleanup;
    
    ret = handle_uds_cmd(json_str, cmd, request_id);
    if (ret == 0) goto cleanup;
    
    ret = handle_uds_advanced_cmd(json_str, cmd, request_id);
    if (ret == 0) goto cleanup;
    
    ret = handle_file_cmd(json_str, cmd, request_id);
    if (ret == 0) goto cleanup;
    
    ret = handle_file_progress_cmd(json_str, cmd, request_id);
    if (ret == 0) goto cleanup;
    
    ret = handle_wifi_cmd(json_str, cmd, request_id);
    if (ret == 0) goto cleanup;
    
    ret = handle_wifi_status_cmd(json_str, cmd, request_id);
    if (ret == 0) goto cleanup;
    
    ret = handle_ui_cmd(json_str, cmd, request_id);
    if (ret == 0) goto cleanup;
    
    ret = handle_system_cmd(json_str, cmd, request_id);
    if (ret == 0) goto cleanup;
    
    // 未识别的命令
    log_warn("未识别的命令: %s", cmd);
    ws_command_send_error(request_id, "unknown command");
    
cleanup:
    free(cmd);
    if (request_id) free(request_id);
    
    return ret;
}
