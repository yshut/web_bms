#include "app_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 全局配置实例
app_config_t g_app_config;

void app_config_init(void) {
    // 初始化默认配置
    strncpy(g_app_config.server_host, REMOTE_SERVER_HOST, sizeof(g_app_config.server_host) - 1);
    g_app_config.server_port = REMOTE_SERVER_PORT;
    g_app_config.tcp_port = REMOTE_TCP_PORT;
    g_app_config.can1_bitrate = CAN_DEFAULT_BITRATE;
    g_app_config.can2_bitrate = CAN_DEFAULT_BITRATE;
    g_app_config.fullscreen = true;
    g_app_config.debug_mode = false;
}

void app_config_load(const char *config_file) {
    // TODO: 从JSON配置文件加载
    // 这里简化处理，使用默认配置
    app_config_init();
    
    FILE *fp = fopen(config_file, "r");
    if (fp) {
        // 读取配置文件
        // 使用 json-c 或简单的文本解析
        fclose(fp);
    }
}

void app_config_save(const char *config_file) {
    // TODO: 保存配置到JSON文件
    FILE *fp = fopen(config_file, "w");
    if (fp) {
        fprintf(fp, "{\n");
        fprintf(fp, "  \"server_host\": \"%s\",\n", g_app_config.server_host);
        fprintf(fp, "  \"server_port\": %u,\n", g_app_config.server_port);
        fprintf(fp, "  \"tcp_port\": %u,\n", g_app_config.tcp_port);
        fprintf(fp, "  \"can1_bitrate\": %u,\n", g_app_config.can1_bitrate);
        fprintf(fp, "  \"can2_bitrate\": %u,\n", g_app_config.can2_bitrate);
        fprintf(fp, "  \"fullscreen\": %s,\n", g_app_config.fullscreen ? "true" : "false");
        fprintf(fp, "  \"debug_mode\": %s\n", g_app_config.debug_mode ? "true" : "false");
        fprintf(fp, "}\n");
        fclose(fp);
    }
}

void app_config_set_server(const char *host, uint16_t port) {
    strncpy(g_app_config.server_host, host, sizeof(g_app_config.server_host) - 1);
    g_app_config.server_port = port;
}

void app_config_set_can_bitrate(uint32_t can1, uint32_t can2) {
    g_app_config.can1_bitrate = can1;
    g_app_config.can2_bitrate = can2;
}

