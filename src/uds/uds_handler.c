#include "uds_handler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

// UDS处理器状态
typedef struct {
    bool running;
    char file_path[256];
    pthread_t thread;
    uds_progress_callback_t progress_callback;
    uds_log_callback_t log_callback;
} uds_handler_state_t;

static uds_handler_state_t g_uds_handler = {
    .running = false,
    .progress_callback = NULL,
    .log_callback = NULL
};

// UDS下载线程
static void* uds_download_thread(void *arg) {
    // 这是一个简化的UDS下载实现
    // 实际应该使用ISO 14229标准的UDS协议
    
    if (g_uds_handler.log_callback) {
        g_uds_handler.log_callback("开始UDS下载...");
    }
    
    FILE *fp = fopen(g_uds_handler.file_path, "rb");
    if (!fp) {
        if (g_uds_handler.log_callback) {
            char log[256];
            snprintf(log, sizeof(log), "无法打开文件: %s", g_uds_handler.file_path);
            g_uds_handler.log_callback(log);
        }
        g_uds_handler.running = false;
        return NULL;
    }
    
    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (g_uds_handler.log_callback) {
        char log[256];
        snprintf(log, sizeof(log), "文件大小: %ld 字节", file_size);
        g_uds_handler.log_callback(log);
    }
    
    // 模拟下载过程
    char buffer[1024];
    long bytes_sent = 0;
    
    while (g_uds_handler.running && !feof(fp)) {
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), fp);
        if (bytes_read > 0) {
            // 这里应该通过CAN发送UDS请求
            // 简化为延时模拟
            usleep(10000); // 10ms
            
            bytes_sent += bytes_read;
            int percent = (int)((bytes_sent * 100) / file_size);
            
            if (g_uds_handler.progress_callback) {
                g_uds_handler.progress_callback(percent);
            }
            
            if (bytes_sent % 10240 == 0) { // 每10KB记录一次
                if (g_uds_handler.log_callback) {
                    char log[256];
                    snprintf(log, sizeof(log), "已发送: %ld / %ld 字节 (%d%%)", 
                            bytes_sent, file_size, percent);
                    g_uds_handler.log_callback(log);
                }
            }
        }
    }
    
    fclose(fp);
    
    if (g_uds_handler.running) {
        if (g_uds_handler.log_callback) {
            g_uds_handler.log_callback("UDS下载完成!");
        }
        if (g_uds_handler.progress_callback) {
            g_uds_handler.progress_callback(100);
        }
    } else {
        if (g_uds_handler.log_callback) {
            g_uds_handler.log_callback("UDS下载已取消");
        }
    }
    
    g_uds_handler.running = false;
    return NULL;
}

int uds_handler_init(void) {
    // 初始化UDS处理器
    return 0;
}

int uds_handler_start(const char *file_path) {
    if (g_uds_handler.running) {
        printf("UDS download already running\n");
        return -1;
    }
    
    strncpy(g_uds_handler.file_path, file_path, sizeof(g_uds_handler.file_path) - 1);
    g_uds_handler.running = true;
    
    pthread_create(&g_uds_handler.thread, NULL, uds_download_thread, NULL);
    
    return 0;
}

void uds_handler_stop(void) {
    if (!g_uds_handler.running) {
        return;
    }
    
    g_uds_handler.running = false;
    pthread_join(g_uds_handler.thread, NULL);
}

void uds_handler_set_progress_callback(uds_progress_callback_t callback) {
    g_uds_handler.progress_callback = callback;
}

void uds_handler_set_log_callback(uds_log_callback_t callback) {
    g_uds_handler.log_callback = callback;
}

bool uds_handler_is_running(void) {
    return g_uds_handler.running;
}

