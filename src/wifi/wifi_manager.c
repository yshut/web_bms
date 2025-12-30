#include "wifi_manager.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

// WiFi管理器状态
typedef struct {
    bool connected;
    char current_ssid[64];
    wifi_scan_callback_t scan_callback;
    wifi_status_callback_t status_callback;
} wifi_manager_state_t;

static wifi_manager_state_t g_wifi_manager = {
    .connected = false,
    .current_ssid = "",
    .scan_callback = NULL,
    .status_callback = NULL
};

int wifi_manager_init(void) {
    // 初始化WiFi管理器
    g_wifi_manager.connected = false;
    memset(g_wifi_manager.current_ssid, 0, sizeof(g_wifi_manager.current_ssid));
    return 0;
}

int wifi_manager_scan(void) {
    // 使用 wpa_cli 或 iw 命令扫描WiFi
    // 这里简化实现，使用系统命令
    
    FILE *fp = popen("iw dev wlan0 scan | grep 'SSID:' | cut -d: -f2", "r");
    if (!fp) {
        printf("WiFi scan failed\n");
        return -1;
    }
    
    char **ssids = NULL;
    int *strengths = NULL;
    int count = 0;
    int capacity = 20;
    
    ssids = (char**)malloc(capacity * sizeof(char*));
    strengths = (int*)malloc(capacity * sizeof(int));
    
    if (!ssids || !strengths) {
        pclose(fp);
        return -1;
    }
    
    char line[128];
    while (fgets(line, sizeof(line), fp) != NULL && count < capacity) {
        // 去除换行符
        line[strcspn(line, "\n")] = 0;
        
        // 去除前导空格
        char *ssid = line;
        while (*ssid == ' ' || *ssid == '\t') ssid++;
        
        if (strlen(ssid) > 0) {
            ssids[count] = strdup(ssid);
            strengths[count] = -60; // 模拟信号强度
            count++;
        }
    }
    
    pclose(fp);
    
    // 调用回调
    if (g_wifi_manager.scan_callback) {
        g_wifi_manager.scan_callback((const char**)ssids, strengths, count);
    }
    
    // 释放内存
    for (int i = 0; i < count; i++) {
        free(ssids[i]);
    }
    free(ssids);
    free(strengths);
    
    printf("WiFi scan complete, found %d networks\n", count);
    return count;
}

int wifi_manager_connect(const char *ssid, const char *password) {
    if (!ssid) {
        return -1;
    }
    
    // 使用 wpa_supplicant 连接WiFi
    // 这里简化实现
    char cmd[512];
    
    if (password && strlen(password) > 0) {
        snprintf(cmd, sizeof(cmd), 
                "wpa_cli -i wlan0 add_network && "
                "wpa_cli -i wlan0 set_network 0 ssid '\"%s\"' && "
                "wpa_cli -i wlan0 set_network 0 psk '\"%s\"' && "
                "wpa_cli -i wlan0 enable_network 0 && "
                "wpa_cli -i wlan0 save_config",
                ssid, password);
    } else {
        snprintf(cmd, sizeof(cmd),
                "wpa_cli -i wlan0 add_network && "
                "wpa_cli -i wlan0 set_network 0 ssid '\"%s\"' && "
                "wpa_cli -i wlan0 set_network 0 key_mgmt NONE && "
                "wpa_cli -i wlan0 enable_network 0 && "
                "wpa_cli -i wlan0 save_config",
                ssid);
    }
    
    int ret = system(cmd);
    
    if (ret == 0) {
        g_wifi_manager.connected = true;
        strncpy(g_wifi_manager.current_ssid, ssid, sizeof(g_wifi_manager.current_ssid) - 1);
        
        // 调用回调
        if (g_wifi_manager.status_callback) {
            g_wifi_manager.status_callback(ssid, true);
        }
        
        printf("Connected to WiFi: %s\n", ssid);
    } else {
        printf("Failed to connect to WiFi: %s\n", ssid);
    }
    
    return ret;
}

int wifi_manager_disconnect(void) {
    system("wpa_cli -i wlan0 disconnect");
    
    g_wifi_manager.connected = false;
    memset(g_wifi_manager.current_ssid, 0, sizeof(g_wifi_manager.current_ssid));
    
    // 调用回调
    if (g_wifi_manager.status_callback) {
        g_wifi_manager.status_callback(NULL, false);
    }
    
    printf("WiFi disconnected\n");
    return 0;
}

bool wifi_manager_is_connected(void) {
    return g_wifi_manager.connected;
}

const char* wifi_manager_get_current_ssid(void) {
    return g_wifi_manager.current_ssid;
}

void wifi_manager_set_scan_callback(wifi_scan_callback_t callback) {
    g_wifi_manager.scan_callback = callback;
}

void wifi_manager_set_status_callback(wifi_status_callback_t callback) {
    g_wifi_manager.status_callback = callback;
}

