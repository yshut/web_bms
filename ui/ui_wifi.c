/**
 * @file ui_wifi.c
 * @brief WiFi设置界面实现（完整功能版）
 */

#include "ui_wifi.h"
#include "ui_common.h"
#include "../logic/app_manager.h"
#include "../utils/logger.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_WIFI_NETWORKS 50
#define MAX_SSID_LEN 64
#define MAX_CMD_LEN 512

// WiFi 网络信息
typedef struct {
    char ssid[MAX_SSID_LEN];
    int signal_strength;  // -100 to 0 dBm
    bool has_encryption;
    bool is_saved;        // 是否已保存
    bool is_connected;    // 是否已连接
} wifi_network_t;

static ui_wifi_t *g_wifi_ui = NULL;
static wifi_network_t g_networks[MAX_WIFI_NETWORKS];
static int g_network_count = 0;
static char g_selected_ssid[MAX_SSID_LEN] = {0};
static bool g_auto_connect_attempted = false;

// 前向声明
static void scan_wifi_networks(ui_wifi_t *wifi);
static void show_password_dialog(ui_wifi_t *wifi, const char *ssid);
static void connect_to_wifi(const char *ssid, const char *password);
static void load_saved_networks(void);
static void check_current_connection(void);
static void auto_connect_if_available(ui_wifi_t *wifi);
static bool is_valid_utf8(const char *str);
static void network_item_event_cb(lv_event_t *e);
static bool generate_psk(const char *ssid, const char *password, char *psk_out, size_t psk_size);

// ==================== 工具函数 ====================

// 检查字符串是否为有效的UTF-8
static bool is_valid_utf8(const char *str)
{
    if (!str) return false;
    
    const unsigned char *bytes = (const unsigned char *)str;
    while (*bytes) {
        if ((*bytes & 0x80) == 0) {
            // ASCII 字符
            bytes++;
        } else if ((*bytes & 0xE0) == 0xC0) {
            // 2字节UTF-8
            if ((bytes[1] & 0xC0) != 0x80) return false;
            bytes += 2;
        } else if ((*bytes & 0xF0) == 0xE0) {
            // 3字节UTF-8 (大部分中文)
            if ((bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80) return false;
            bytes += 3;
        } else if ((*bytes & 0xF8) == 0xF0) {
            // 4字节UTF-8
            if ((bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80 || (bytes[3] & 0xC0) != 0x80) return false;
            bytes += 4;
        } else {
            return false;
        }
    }
    return true;
}

// 清理SSID中的无效字符
static void sanitize_ssid(char *ssid)
{
    if (!ssid) return;
    
    // 如果不是有效的UTF-8，尝试过滤不可打印字符
    if (!is_valid_utf8(ssid)) {
        char *src = ssid;
        char *dst = ssid;
        
        while (*src) {
            // 保留ASCII可打印字符和高位字节（可能是中文）
            if ((*src >= 0x20 && *src <= 0x7E) || (*src & 0x80)) {
                *dst++ = *src;
            }
            src++;
        }
        *dst = '\0';
    }
    
    // 移除首尾空格
    char *start = ssid;
    while (*start == ' ' || *start == '\t') start++;
    
    if (start != ssid) {
        memmove(ssid, start, strlen(start) + 1);
    }
    
    int len = strlen(ssid);
    while (len > 0 && (ssid[len-1] == ' ' || ssid[len-1] == '\t')) {
        ssid[--len] = '\0';
    }
}

// 加载已保存的网络
static void load_saved_networks(void)
{
    log_info("加载已保存的网络...");
    
    // 读取wpa_supplicant配置
    FILE *fp = fopen("/etc/wpa_supplicant.conf", "r");
    if (!fp) {
        log_warn("无法打开wpa_supplicant配置文件");
        return;
    }
    
    char line[256];
    char saved_ssid[MAX_SSID_LEN];
    bool in_network_block = false;
    
    while (fgets(line, sizeof(line), fp)) {
        // 检测network块
        if (strstr(line, "network={")) {
            in_network_block = true;
            saved_ssid[0] = '\0';
        }
        
        if (in_network_block && strstr(line, "ssid=")) {
            // 解析SSID
            char *start = strchr(line, '"');
            if (start) {
                start++;
                char *end = strchr(start, '"');
                if (end) {
                    int len = end - start;
                    if (len > 0 && len < MAX_SSID_LEN) {
                        strncpy(saved_ssid, start, len);
                        saved_ssid[len] = '\0';
                        
                        // 标记为已保存
                        for (int i = 0; i < g_network_count; i++) {
                            if (strcmp(g_networks[i].ssid, saved_ssid) == 0) {
                                g_networks[i].is_saved = true;
                                log_info("发现已保存网络: %s", saved_ssid);
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        if (in_network_block && strstr(line, "}")) {
            in_network_block = false;
        }
    }
    
    fclose(fp);
}

// 检查当前连接状态
static void check_current_connection(void)
{
    log_info("检查当前WiFi连接状态...");
    
    // 执行wpa_cli status
    system("wpa_cli -i wlan0 status > /tmp/wifi_current_status.txt 2>&1");
    
    FILE *fp = fopen("/tmp/wifi_current_status.txt", "r");
    if (!fp) return;
    
    char line[256];
    char current_ssid[MAX_SSID_LEN] = {0};
    bool is_connected = false;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "wpa_state=COMPLETED")) {
            is_connected = true;
        }
        
        if (strstr(line, "ssid=")) {
            char *ssid_start = strchr(line, '=');
            if (ssid_start) {
                ssid_start++;
                // 移除换行符
                char *newline = strchr(ssid_start, '\n');
                if (newline) *newline = '\0';
                strncpy(current_ssid, ssid_start, MAX_SSID_LEN - 1);
            }
        }
    }
    
    fclose(fp);
    
    if (is_connected && strlen(current_ssid) > 0) {
        log_info("当前已连接到: %s", current_ssid);
        
        // 标记当前连接的网络
        for (int i = 0; i < g_network_count; i++) {
            if (strcmp(g_networks[i].ssid, current_ssid) == 0) {
                g_networks[i].is_connected = true;
                
                // 更新UI状态
                if (g_wifi_ui && g_wifi_ui->status_label) {
                    char status[128];
                    snprintf(status, sizeof(status), "状态: 已连接到 %s", current_ssid);
                    lv_label_set_text(g_wifi_ui->status_label, status);
                }
                break;
            }
        }
    }
}

// 自动连接已保存的网络
static void auto_connect_if_available(ui_wifi_t *wifi)
{
    if (!wifi || g_auto_connect_attempted) return;
    
    g_auto_connect_attempted = true;
    
    log_info("尝试自动连接已保存的网络...");
    
    // 先检查是否已经连接
    check_current_connection();
    
    // 如果已经连接，不需要自动连接
    for (int i = 0; i < g_network_count; i++) {
        if (g_networks[i].is_connected) {
            log_info("已经连接到网络: %s", g_networks[i].ssid);
            return;
        }
    }
    
    // 查找信号最强的已保存网络
    int best_saved = -1;
    int best_signal = -100;
    
    for (int i = 0; i < g_network_count; i++) {
        if (g_networks[i].is_saved && g_networks[i].signal_strength > best_signal) {
            best_saved = i;
            best_signal = g_networks[i].signal_strength;
        }
    }
    
    if (best_saved >= 0) {
        log_info("自动连接到已保存网络: %s (信号: %d dBm)", 
                 g_networks[best_saved].ssid, g_networks[best_saved].signal_strength);
        
        if (wifi->status_label) {
            char status[128];
            snprintf(status, sizeof(status), "状态: 自动连接到 %s...", g_networks[best_saved].ssid);
            lv_label_set_text(wifi->status_label, status);
        }
        
        // 完整的自动连接流程
        log_info("启动完整连接流程...");
        
        // 1. 确保wpa_supplicant正在运行
        system("killall wpa_supplicant 2>/dev/null");
        sleep(1);
        
        // 2. 重新启动wpa_supplicant
        system("wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf -Dnl80211,wext 2>/dev/null");
        sleep(2);
        
        // 3. 触发DHCP获取IP
        system("killall udhcpc 2>/dev/null");
        system("udhcpc -i wlan0 -n -q 2>/dev/null &");
        
        // 4. 等待连接
        sleep(3);
        
        // 5. 检查连接状态
        check_current_connection();
    } else {
        log_info("没有发现已保存的网络");
    }
}

// 网络排序比较函数：已保存且已连接 > 已保存 > 信号强
static int compare_networks(const void *a, const void *b)
{
    const wifi_network_t *na = (const wifi_network_t *)a;
    const wifi_network_t *nb = (const wifi_network_t *)b;
    
    // 已连接的网络优先
    if (na->is_connected && !nb->is_connected) return -1;
    if (!na->is_connected && nb->is_connected) return 1;
    
    // 已保存的网络其次
    if (na->is_saved && !nb->is_saved) return -1;
    if (!na->is_saved && nb->is_saved) return 1;
    
    // 最后按信号强度排序
    return (nb->signal_strength - na->signal_strength);
}

// ==================== 密码输入对话框 ====================

static void password_dialog_ok_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    
    ui_wifi_t *wifi = g_wifi_ui;
    if (!wifi || !wifi->password_dialog) return;
    
    // 获取输入的密码
    const char *password = lv_textarea_get_text(wifi->password_textarea);
    if (password && strlen(password) > 0) {
        log_info("开始连接 WiFi: %s", g_selected_ssid);
        
        if (wifi->status_label) {
            char status[128];
            snprintf(status, sizeof(status), "状态: 正在连接 %s...", g_selected_ssid);
            lv_label_set_text(wifi->status_label, status);
        }
        
        // 连接 WiFi
        connect_to_wifi(g_selected_ssid, password);
    }
    
    // 关闭对话框
    if (wifi->password_dialog) {
        lv_obj_del(wifi->password_dialog);
        wifi->password_dialog = NULL;
        wifi->password_textarea = NULL;
    }
}

static void password_dialog_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    
    ui_wifi_t *wifi = g_wifi_ui;
    if (wifi && wifi->password_dialog) {
        lv_obj_del(wifi->password_dialog);
        wifi->password_dialog = NULL;
        wifi->password_textarea = NULL;
    }
}

static void show_password_dialog(ui_wifi_t *wifi, const char *ssid)
{
    if (!wifi || !ssid) return;
    
    log_info("显示密码输入对话框: %s", ssid);
    
    // 保存选中的 SSID
    strncpy(g_selected_ssid, ssid, MAX_SSID_LEN - 1);
    
    // 如果已有对话框，先关闭
    if (wifi->password_dialog) {
        lv_obj_del(wifi->password_dialog);
    }
    
    // 创建全屏对话框
    wifi->password_dialog = lv_obj_create(wifi->screen);
    lv_obj_set_size(wifi->password_dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wifi->password_dialog, lv_color_hex(0xF0F0F0), 0);
    lv_obj_clear_flag(wifi->password_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(wifi->password_dialog);
    
    // 标题
    lv_obj_t *title_label = lv_label_create(wifi->password_dialog);
    char title_text[128];
    snprintf(title_text, sizeof(title_text), "连接到: %s", ssid);
    lv_label_set_text(title_label, title_text);
    lv_obj_set_style_text_font(title_label, ui_common_get_font(), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);
    
    // 输入框
    wifi->password_textarea = lv_textarea_create(wifi->password_dialog);
    lv_obj_set_size(wifi->password_textarea, LV_PCT(90), 50);
    lv_obj_align(wifi->password_textarea, LV_ALIGN_TOP_MID, 0, 60);
    lv_textarea_set_one_line(wifi->password_textarea, true);
    lv_textarea_set_password_mode(wifi->password_textarea, true);
    lv_textarea_set_placeholder_text(wifi->password_textarea, "请输入WiFi密码");
    lv_obj_set_style_text_font(wifi->password_textarea, ui_common_get_font(), 0);
    
    // 创建键盘
    lv_obj_t *kb = lv_keyboard_create(wifi->password_dialog);
    lv_obj_set_size(kb, LV_PCT(100), LV_PCT(50));
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, wifi->password_textarea);
    
    // 按钮容器
    lv_obj_t *btn_cont = lv_obj_create(wifi->password_dialog);
    lv_obj_set_size(btn_cont, LV_PCT(90), 60);
    lv_obj_align(btn_cont, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // 连接按钮
    lv_obj_t *ok_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(ok_btn, 200, 45);
    lv_obj_add_event_cb(ok_btn, password_dialog_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x4CAF50), 0);
    lv_obj_t *ok_label = lv_label_create(ok_btn);
    lv_label_set_text(ok_label, "连接");
    lv_obj_set_style_text_font(ok_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(ok_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(ok_label);
    
    // 取消按钮
    lv_obj_t *cancel_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(cancel_btn, 200, 45);
    lv_obj_add_event_cb(cancel_btn, password_dialog_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0xF44336), 0);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "取消");
    lv_obj_set_style_text_font(cancel_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(cancel_label);
}

// ==================== WiFi 扫描和连接 ====================

// 获取信号强度图标
static const char* get_signal_icon(int signal_strength)
{
    if (signal_strength >= -50) return "▂▄▆█"; // 强
    if (signal_strength >= -60) return "▂▄▆_"; // 良好
    if (signal_strength >= -70) return "▂▄__"; // 中等
    if (signal_strength >= -80) return "▂___"; // 弱
    return "____"; // 很弱
}

// 扫描 WiFi 网络
static void scan_wifi_networks(ui_wifi_t *wifi)
{
    if (!wifi) return;
    
    log_info("开始扫描 WiFi 网络...");
    
    if (wifi->status_label) {
        lv_label_set_text(wifi->status_label, "状态: 正在扫描...");
    }
    
    // 清空之前的扫描结果
    g_network_count = 0;
    memset(g_networks, 0, sizeof(g_networks));
    
    // 执行扫描命令（使用 iw 工具）
    // 注意：需要 root 权限
    system("iw dev wlan0 scan > /tmp/wifi_scan.txt 2>&1");
    sleep(1); // 等待扫描完成
    
    // 解析扫描结果
    FILE *fp = fopen("/tmp/wifi_scan.txt", "r");
    if (!fp) {
        log_error("无法打开扫描结果文件");
        
        // 添加示例网络供测试
        strcpy(g_networks[0].ssid, "TP-LINK_5G");
        g_networks[0].signal_strength = -45;
        g_networks[0].has_encryption = true;
        
        strcpy(g_networks[1].ssid, "ChinaNet-Home");
        g_networks[1].signal_strength = -60;
        g_networks[1].has_encryption = true;
        
        strcpy(g_networks[2].ssid, "Guest-WiFi");
        g_networks[2].signal_strength = -75;
        g_networks[2].has_encryption = false;
        
        g_network_count = 3;
        
        if (wifi->status_label) {
            lv_label_set_text(wifi->status_label, "状态: 扫描完成（示例数据）");
        }
        return;
    }
    
    // 解析文件内容
    char line[256];
    int current_network = -1;
    
    while (fgets(line, sizeof(line), fp) && g_network_count < MAX_WIFI_NETWORKS) {
        // 检测到新的 BSS (网络)
        if (strstr(line, "BSS ") == line) {
            current_network = g_network_count;
            g_network_count++;
            g_networks[current_network].signal_strength = -100;
            g_networks[current_network].has_encryption = false;
        }
        
        if (current_network < 0) continue;
        
        // 解析 SSID
        char *ssid_pos = strstr(line, "SSID: ");
        if (ssid_pos) {
            ssid_pos += 6; // 跳过 "SSID: "
            // 去除换行符
            char *newline = strchr(ssid_pos, '\n');
            if (newline) *newline = '\0';
            strncpy(g_networks[current_network].ssid, ssid_pos, MAX_SSID_LEN - 1);
            // 清理SSID中的无效字符
            sanitize_ssid(g_networks[current_network].ssid);
        }
        
        // 解析信号强度
        char *signal_pos = strstr(line, "signal: ");
        if (signal_pos) {
            int signal = 0;
            if (sscanf(signal_pos, "signal: %d", &signal) == 1) {
                g_networks[current_network].signal_strength = signal;
            }
        }
        
        // 检测加密
        if (strstr(line, "WPA") || strstr(line, "RSN") || strstr(line, "Privacy")) {
            g_networks[current_network].has_encryption = true;
        }
    }
    
    fclose(fp);
    
    log_info("扫描完成，发现 %d 个网络", g_network_count);
    
    // 加载已保存的网络并标记
    load_saved_networks();
    
    // 检查当前连接状态
    check_current_connection();
    
    // 对网络列表排序：已连接 > 已保存 > 信号强度
    if (g_network_count > 0) {
        qsort(g_networks, g_network_count, sizeof(wifi_network_t), compare_networks);
        log_info("网络列表已排序");
    }
    
    if (wifi->status_label) {
        char status[64];
        int saved_count = 0;
        for (int i = 0; i < g_network_count; i++) {
            if (g_networks[i].is_saved) saved_count++;
        }
        snprintf(status, sizeof(status), "状态: 发现 %d 个网络 (%d 个已保存)", g_network_count, saved_count);
        lv_label_set_text(wifi->status_label, status);
    }
    
    // 更新网络列表UI
    if (wifi->network_list) {
        lv_obj_clean(wifi->network_list); // 清空列表
        
        for (int i = 0; i < g_network_count; i++) {
            if (strlen(g_networks[i].ssid) == 0) continue;
            
            // 创建网络项文本
            char network_text[156];
            const char *lock_icon = g_networks[i].has_encryption ? "🔒" : "  ";
            const char *signal_icon = get_signal_icon(g_networks[i].signal_strength);
            const char *status_icon = "";
            
            // 添加连接状态和已保存标识
            if (g_networks[i].is_connected) {
                status_icon = "[已连接] ";
            } else if (g_networks[i].is_saved) {
                status_icon = "[已保存] ";
            }
            
            snprintf(network_text, sizeof(network_text), "%s%s %s %s (%d dBm)",
                     status_icon, lock_icon, g_networks[i].ssid, signal_icon, g_networks[i].signal_strength);
            
            lv_obj_t *item = lv_list_add_btn(wifi->network_list, LV_SYMBOL_WIFI, network_text);
            if (item) {
                lv_obj_set_style_text_font(item, ui_common_get_font(), 0);
                
                // 为已连接的网络设置特殊样式
                if (g_networks[i].is_connected) {
                    lv_obj_set_style_bg_color(item, lv_color_hex(0xC8E6C9), 0); // 绿色背景
                } else if (g_networks[i].is_saved) {
                    lv_obj_set_style_bg_color(item, lv_color_hex(0xE1F5FE), 0); // 蓝色背景
                }
                
                // 保存 SSID 到用户数据
                char *ssid_copy = strdup(g_networks[i].ssid);
                lv_obj_set_user_data(item, ssid_copy);
                
                // 为每个列表项添加点击事件
                lv_obj_add_event_cb(item, network_item_event_cb, LV_EVENT_CLICKED, wifi);
            }
        }
        
        if (g_network_count == 0) {
            lv_obj_t *empty = lv_list_add_text(wifi->network_list, "未发现网络");
            if (empty) {
                lv_obj_set_style_text_font(empty, ui_common_get_font(), 0);
                lv_obj_set_style_text_color(empty, lv_color_hex(0x888888), 0);
            }
        }
    }
    
    // 尝试自动连接（仅在首次创建时）
    if (!g_auto_connect_attempted) {
        auto_connect_if_available(wifi);
    }
}

// 使用 wpa_passphrase 生成加密的 PSK
static bool generate_psk(const char *ssid, const char *password, char *psk_out, size_t psk_size)
{
    if (!ssid || !password || !psk_out) return false;
    
    char cmd[MAX_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "wpa_passphrase \"%s\" \"%s\" 2>/dev/null | grep 'psk=' | grep -v '#psk' | cut -d'=' -f2 > /tmp/wifi_psk.txt", 
             ssid, password);
    
    system(cmd);
    
    FILE *fp = fopen("/tmp/wifi_psk.txt", "r");
    if (!fp) return false;
    
    if (fgets(psk_out, psk_size, fp) != NULL) {
        // 移除换行符
        char *newline = strchr(psk_out, '\n');
        if (newline) *newline = '\0';
        fclose(fp);
        return strlen(psk_out) > 0;
    }
    
    fclose(fp);
    return false;
}

// 连接到 WiFi
static void connect_to_wifi(const char *ssid, const char *password)
{
    if (!ssid) return;
    
    log_info("连接到 WiFi: %s", ssid);
    
    // 方法：直接写入配置文件
    FILE *fp = fopen("/etc/wpa_supplicant.conf", "a");
    if (!fp) {
        log_error("无法打开 wpa_supplicant.conf");
        
        ui_wifi_t *wifi = g_wifi_ui;
        if (wifi && wifi->status_label) {
            lv_label_set_text(wifi->status_label, "状态: 配置文件写入失败");
        }
        return;
    }
    
    // 写入网络配置
    fprintf(fp, "\nnetwork={\n");
    fprintf(fp, "    ssid=\"%s\"\n", ssid);
    
    if (password && strlen(password) > 0) {
        // 尝试生成加密的PSK
        char psk[128] = {0};
        if (generate_psk(ssid, password, psk, sizeof(psk))) {
            fprintf(fp, "    psk=%s\n", psk);
            log_info("使用加密PSK保存密码");
        } else {
            // 如果失败，使用明文密码（不推荐但可用）
            fprintf(fp, "    psk=\"%s\"\n", password);
            log_warn("使用明文密码（wpa_passphrase不可用）");
        }
        fprintf(fp, "    key_mgmt=WPA-PSK\n");
    } else {
        // 开放网络
        fprintf(fp, "    key_mgmt=NONE\n");
    }
    
    fprintf(fp, "    priority=1\n");
    fprintf(fp, "}\n");
    fclose(fp);
    
    log_info("网络配置已写入配置文件");
    
    // 重启网络接口以应用配置
    ui_wifi_t *wifi = g_wifi_ui;
    if (wifi && wifi->status_label) {
        lv_label_set_text(wifi->status_label, "状态: 正在连接...");
    }
    
    // 停止现有连接
    system("killall wpa_supplicant 2>/dev/null");
    sleep(1);
    
    // 重新启动 wpa_supplicant
    system("wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf -Dnl80211,wext 2>/dev/null");
    sleep(2);
    
    // 获取 IP 地址
    system("udhcpc -i wlan0 -n -q 2>/dev/null &");
    
    // 等待连接
    sleep(3);
    
    // 检查连接状态
    check_current_connection();
    
    log_info("WiFi 连接流程完成");
}

// 断开 WiFi
static void disconnect_wifi(void)
{
    log_info("断开 WiFi 连接");
    
    // 停止 wpa_supplicant
    system("killall wpa_supplicant 2>/dev/null");
    sleep(1);
    
    // 关闭网络接口
    system("ifconfig wlan0 down");
    sleep(1);
    
    // 重新启动接口（不连接）
    system("ifconfig wlan0 up");
    
    // 重新启动 wpa_supplicant（不会自动连接）
    system("wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf -Dnl80211,wext 2>/dev/null");
    
    log_info("WiFi 已断开");
    
    ui_wifi_t *wifi = g_wifi_ui;
    if (wifi && wifi->status_label) {
        lv_label_set_text(wifi->status_label, "状态: 已断开");
    }
}

// ==================== UI 事件回调 ====================

static void back_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        log_info("从WiFi设置返回主页");
        app_manager_switch_to_page(APP_PAGE_HOME);
    }
}

static void scan_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    
    ui_wifi_t *wifi = (ui_wifi_t *)lv_event_get_user_data(e);
    if (!wifi) return;
    
    log_info("用户点击扫描按钮");
    
    // 扫描网络（会自动更新UI）
    scan_wifi_networks(wifi);
}

static void network_item_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    
    lv_obj_t *item = lv_event_get_target(e);
    ui_wifi_t *wifi = (ui_wifi_t *)lv_event_get_user_data(e);
    
    if (!item || !wifi) return;
    
    // 获取 SSID
    char *ssid = (char *)lv_obj_get_user_data(item);
    if (!ssid) return;
    
    log_info("用户选择网络: %s", ssid);
    
    // 检查是否需要密码
    bool needs_password = false;
    bool is_saved = false;
    
    for (int i = 0; i < g_network_count; i++) {
        if (strcmp(g_networks[i].ssid, ssid) == 0) {
            needs_password = g_networks[i].has_encryption;
            is_saved = g_networks[i].is_saved;
            break;
        }
    }
    
    // 如果是已保存的网络，直接连接
    if (is_saved) {
        log_info("连接已保存的网络: %s", ssid);
        strncpy(g_selected_ssid, ssid, MAX_SSID_LEN - 1);
        
        if (wifi->status_label) {
            char status[128];
            snprintf(status, sizeof(status), "状态: 正在连接 %s...", ssid);
            lv_label_set_text(wifi->status_label, status);
        }
        
        // 使用wpa_cli reconnect连接已保存的网络
        system("wpa_cli -i wlan0 reconnect > /dev/null 2>&1");
        sleep(3);
        check_current_connection();
    } else if (needs_password) {
        // 显示密码输入对话框
        show_password_dialog(wifi, ssid);
    } else {
        // 直接连接开放网络
        strncpy(g_selected_ssid, ssid, MAX_SSID_LEN - 1);
        connect_to_wifi(ssid, NULL);
    }
}

static void disconnect_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        disconnect_wifi();
    }
}

// ==================== UI 创建 ====================

ui_wifi_t* ui_wifi_create(void)
{
    log_info("创建WiFi设置界面...");
    
    ui_wifi_t *wifi = malloc(sizeof(ui_wifi_t));
    if (!wifi) return NULL;
    
    memset(wifi, 0, sizeof(ui_wifi_t));
    g_wifi_ui = wifi;
    
    // 重置自动连接标志（允许在新打开的页面中自动连接）
    g_auto_connect_attempted = false;
    
    wifi->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(wifi->screen, lv_color_hex(0xF0F0F0), 0);
    
    /* 顶部标题栏 */
    lv_obj_t *header = lv_obj_create(wifi->screen);
    lv_obj_set_size(header, LV_PCT(100), 60);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 80, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " 返回");
    lv_obj_set_style_text_font(back_label, ui_common_get_font(), 0);
    lv_obj_center(back_label);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "WiFi 设置");
    lv_obj_set_style_text_font(title, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(title);
    
    /* 状态栏 */
    lv_obj_t *status_cont = lv_obj_create(wifi->screen);
    lv_obj_set_size(status_cont, LV_PCT(95), 50);
    lv_obj_align(status_cont, LV_ALIGN_TOP_MID, 0, 65);
    lv_obj_set_style_bg_color(status_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(status_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    wifi->status_label = lv_label_create(status_cont);
    lv_label_set_text(wifi->status_label, "状态: 离线");
    lv_obj_set_style_text_font(wifi->status_label, ui_common_get_font(), 0);
    lv_obj_center(wifi->status_label);
    
    /* Control buttons */
    lv_obj_t *control_cont = lv_obj_create(wifi->screen);
    lv_obj_set_size(control_cont, LV_PCT(95), 60);
    lv_obj_align(control_cont, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_flex_flow(control_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(control_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *scan_btn = lv_btn_create(control_cont);
    lv_obj_set_size(scan_btn, 150, 45);
    lv_obj_add_event_cb(scan_btn, scan_btn_event_cb, LV_EVENT_CLICKED, wifi);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x4CAF50), 0);
    lv_obj_t *scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, LV_SYMBOL_REFRESH " 扫描网络");
    lv_obj_set_style_text_font(scan_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(scan_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(scan_label);
    
    lv_obj_t *disconnect_btn = lv_btn_create(control_cont);
    lv_obj_set_size(disconnect_btn, 150, 45);
    lv_obj_add_event_cb(disconnect_btn, disconnect_btn_event_cb, LV_EVENT_CLICKED, wifi);
    lv_obj_set_style_bg_color(disconnect_btn, lv_color_hex(0xF44336), 0);
    lv_obj_t *disconnect_label = lv_label_create(disconnect_btn);
    lv_label_set_text(disconnect_label, "断开连接");
    lv_obj_set_style_text_font(disconnect_label, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(disconnect_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(disconnect_label);
    
    /* Network list */
    wifi->network_list = lv_list_create(wifi->screen);
    lv_obj_set_size(wifi->network_list, LV_PCT(95), LV_PCT(100) - 195);
    lv_obj_align(wifi->network_list, LV_ALIGN_TOP_MID, 0, 185);
    lv_obj_set_style_bg_color(wifi->network_list, lv_color_hex(0xFFFFFF), 0);
    
    // 注意：点击事件会在创建每个列表项时添加
    
    // 初始化时检查当前连接状态
    if (ui_wifi_is_connected()) {
        const char *ssid = ui_wifi_get_connected_ssid();
        const char *ip = ui_wifi_get_ip_address();
        
        char status_text[128];
        if (strlen(ssid) > 0 && strlen(ip) > 0) {
            snprintf(status_text, sizeof(status_text), "状态: 已连接到 %s (%s)", ssid, ip);
        } else if (strlen(ssid) > 0) {
            snprintf(status_text, sizeof(status_text), "状态: 已连接到 %s", ssid);
        } else {
            snprintf(status_text, sizeof(status_text), "状态: 已连接");
        }
        lv_label_set_text(wifi->status_label, status_text);
        
        log_info("WiFi页面打开时检测到已连接: %s", ssid);
        
        // 自动扫描网络以显示当前连接
        scan_wifi_networks(wifi);
    } else {
        // 未连接时显示提示
        lv_obj_t *hint = lv_list_add_text(wifi->network_list, "点击\"扫描网络\"搜索WiFi");
        if (hint) {
            lv_obj_set_style_text_font(hint, ui_common_get_font(), 0);
            lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
        }
        log_info("WiFi页面打开时未检测到连接");
    }
    
    log_info("WiFi settings UI created");
    return wifi;
}

void ui_wifi_destroy(ui_wifi_t *wifi)
{
    if (!wifi) return;
    
    log_info("开始销毁WiFi设置界面...");
    
    // 关闭密码对话框
    if (wifi->password_dialog) {
        lv_obj_del(wifi->password_dialog);
        wifi->password_dialog = NULL;
    }
    
    // 释放网络列表中的用户数据
    if (wifi->network_list) {
        uint32_t child_cnt = lv_obj_get_child_cnt(wifi->network_list);
        for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t *child = lv_obj_get_child(wifi->network_list, i);
            char *ssid = (char *)lv_obj_get_user_data(child);
            if (ssid) {
                free(ssid);
            }
        }
    }
    
    // 清除全局指针
    if (g_wifi_ui == wifi) {
        g_wifi_ui = NULL;
    }
    
    // 释放内存
    free(wifi);
    
    log_info("WiFi设置界面已销毁（WiFi连接保持）");
}

// ==================== WiFi 状态查询 ====================

/**
 * @brief 检查 WiFi 是否已连接
 */
bool ui_wifi_is_connected(void)
{
    // 检查 wlan0 接口是否有 IP 地址
    FILE *fp = popen("ip addr show wlan0 | grep 'inet ' | awk '{print $2}' | cut -d'/' -f1", "r");
    if (!fp) return false;
    
    char ip[64] = {0};
    bool connected = false;
    
    if (fgets(ip, sizeof(ip), fp) != NULL) {
        // 移除换行符
        char *newline = strchr(ip, '\n');
        if (newline) *newline = '\0';
        
        // 如果有 IP 地址且不是 0.0.0.0，说明已连接
        if (strlen(ip) > 0 && strcmp(ip, "0.0.0.0") != 0) {
            connected = true;
        }
    }
    
    pclose(fp);
    return connected;
}

/**
 * @brief 获取当前连接的 WiFi SSID
 */
const char* ui_wifi_get_connected_ssid(void)
{
    static char ssid[MAX_SSID_LEN] = {0};
    ssid[0] = '\0';
    
    // 使用 iw 命令获取当前连接的 SSID
    FILE *fp = popen("iw dev wlan0 link | grep 'SSID:' | awk '{print $2}'", "r");
    if (!fp) return ssid;
    
    if (fgets(ssid, sizeof(ssid), fp) != NULL) {
        // 移除换行符
        char *newline = strchr(ssid, '\n');
        if (newline) *newline = '\0';
    }
    
    pclose(fp);
    return ssid;
}

/**
 * @brief 获取 WiFi 接口的 IP 地址
 */
const char* ui_wifi_get_ip_address(void)
{
    static char ip[64] = {0};
    ip[0] = '\0';
    
    FILE *fp = popen("ip addr show wlan0 | grep 'inet ' | awk '{print $2}' | cut -d'/' -f1", "r");
    if (!fp) return ip;
    
    if (fgets(ip, sizeof(ip), fp) != NULL) {
        // 移除换行符
        char *newline = strchr(ip, '\n');
        if (newline) *newline = '\0';
    }
    
    pclose(fp);
    return ip;
}
