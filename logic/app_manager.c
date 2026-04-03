/**
 * @file app_manager.c
 * @brief 应用管理器实现 - 负责页面切换、状态管理等
 */

#include "app_manager.h"
#include "../utils/logger.h"
#include "remote_transport.h"
#include "../ui/ui_home.h"
#include "../ui/ui_can_monitor.h"
#include "../ui/ui_uds.h"
#include "../ui/ui_file_manager.h"
#include "../ui/ui_wifi.h"
#include "../ui/ui_websocket.h"

/* 全局状态 */
typedef struct {
    app_page_t current_page;
    ui_home_t *ui_home;
    ui_can_monitor_t *ui_can_monitor;
    ui_uds_t *ui_uds;
    ui_file_manager_t *ui_file_manager;
    ui_wifi_t *ui_wifi;
    bool initialized;
} app_manager_t;

static app_manager_t g_app_manager = {
    .current_page = APP_PAGE_HOME,
    .ui_home = NULL,
    .ui_can_monitor = NULL,
    .ui_uds = NULL,
    .ui_file_manager = NULL,
    .ui_wifi = NULL,
    .initialized = false,
};

/**
 * @brief 初始化应用管理器
 */
int app_manager_init(void)
{
    if (g_app_manager.initialized) {
        log_warn("应用管理器已经初始化");
        return 0;
    }
    
    log_info("应用管理器初始化...");
    
    // 这里可以初始化一些全局资源
    g_app_manager.current_page = APP_PAGE_HOME;
    g_app_manager.initialized = true;
    
    log_info("应用管理器初始化完成");
    return 0;
}

/**
 * @brief 清理应用管理器
 */
void app_manager_deinit(void)
{
    if (!g_app_manager.initialized) {
        return;
    }
    
    log_info("应用管理器清理...");
    
    // 清理资源
    g_app_manager.initialized = false;
    
    log_info("应用管理器清理完成");
}

/**
 * @brief 切换到指定页面
 */
int app_manager_switch_to_page(app_page_t page)
{
    if (!g_app_manager.initialized) {
        log_error("应用管理器未初始化");
        return -1;
    }
    
    if (g_app_manager.current_page == page) {
        log_debug("已经在页面: %d", page);
        return 0;
    }
    
    log_info("切换页面: %d -> %d", g_app_manager.current_page, page);
    
    // 创建或显示目标页面
    switch (page) {
        case APP_PAGE_HOME:
            log_info("返回主页");
            // 主页面如果不存在则创建，否则直接显示
            if (!g_app_manager.ui_home) {
                g_app_manager.ui_home = ui_home_create();
                // 注册主界面实例（用于WebSocket状态更新）
                if (g_app_manager.ui_home) {
                    extern void ui_home_register_instance(ui_home_t *ui);
                    ui_home_register_instance(g_app_manager.ui_home);
                }
            }
            if (g_app_manager.ui_home) {
                lv_scr_load(g_app_manager.ui_home->screen);
                
                // 强制更新WebSocket连接状态（解决返回主页时显示离线的问题）
                if (remote_transport_is_connected()) {
                    char host[128];
                    uint16_t port;
                    if (remote_transport_get_server_info(host, sizeof(host), &port) == 0) {
                        extern void ui_home_update_server_status(ui_home_t *ui, bool connected, const char *host, uint16_t port);
                        ui_home_update_server_status(g_app_manager.ui_home, true, host, port);
                        log_info("主页面：更新WebSocket状态为已连接");
                    }
                } else {
                    extern void ui_home_update_server_status(ui_home_t *ui, bool connected, const char *host, uint16_t port);
                    ui_home_update_server_status(g_app_manager.ui_home, false, NULL, 0);
                    log_info("主页面：更新WebSocket状态为未连接");
                }
            }
            break;
            
        case APP_PAGE_CAN_MONITOR:
            log_info("切换到CAN监控");
            g_app_manager.ui_can_monitor = ui_can_monitor_create();
            if (g_app_manager.ui_can_monitor) {
                lv_scr_load(g_app_manager.ui_can_monitor->screen);
            }
            break;
            
        case APP_PAGE_UDS:
            log_info("切换到UDS诊断");
            g_app_manager.ui_uds = ui_uds_create();
            if (g_app_manager.ui_uds) {
                lv_scr_load(g_app_manager.ui_uds->screen);
            }
            break;
            
        case APP_PAGE_FILE_MANAGER:
            log_info("切换到文件管理");
            g_app_manager.ui_file_manager = ui_file_manager_create();
            if (g_app_manager.ui_file_manager) {
                lv_scr_load(g_app_manager.ui_file_manager->screen);
            }
            break;
            
        case APP_PAGE_WIFI:
            log_info("切换到WiFi设置");
            g_app_manager.ui_wifi = ui_wifi_create();
            if (g_app_manager.ui_wifi) {
                lv_scr_load(g_app_manager.ui_wifi->screen);
            }
            break;
            
        case APP_PAGE_WEBSOCKET:
            log_info("切换到WebSocket配置");
            {
                lv_obj_t *ws_screen = lv_obj_create(NULL);
                lv_obj_t *ws_ui = ui_websocket_create(ws_screen);
                if (ws_ui) {
                    lv_scr_load(ws_screen);
                }
            }
            break;
        
        default:
            log_error("未知页面: %d", page);
            return -1;
    }
    
    // 销毁旧的子页面（在切换屏幕之后，确保不删除活动屏幕）
    if (g_app_manager.current_page != APP_PAGE_HOME && g_app_manager.current_page != page) {
        switch (g_app_manager.current_page) {
            case APP_PAGE_CAN_MONITOR:
                if (g_app_manager.ui_can_monitor) {
                    ui_can_monitor_destroy(g_app_manager.ui_can_monitor);
                    g_app_manager.ui_can_monitor = NULL;
                }
                break;
            case APP_PAGE_UDS:
                if (g_app_manager.ui_uds) {
                    ui_uds_destroy(g_app_manager.ui_uds);
                    g_app_manager.ui_uds = NULL;
                }
                break;
            case APP_PAGE_FILE_MANAGER:
                if (g_app_manager.ui_file_manager) {
                    ui_file_manager_destroy(g_app_manager.ui_file_manager);
                    g_app_manager.ui_file_manager = NULL;
                }
                break;
            case APP_PAGE_WIFI:
                if (g_app_manager.ui_wifi) {
                    ui_wifi_destroy(g_app_manager.ui_wifi);
                    g_app_manager.ui_wifi = NULL;
                }
                break;
            case APP_PAGE_WEBSOCKET:
                ui_websocket_destroy();
                break;
            default:
                break;
        }
    }
    
    g_app_manager.current_page = page;
    return 0;
}

/**
 * @brief 获取当前页面
 */
app_page_t app_manager_get_current_page(void)
{
    return g_app_manager.current_page;
}

