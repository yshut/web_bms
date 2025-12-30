/**
 * @file ui_common.c
 * @brief 通用UI样式和组件
 */

#include "ui_common.h"
#include "../utils/logger.h"
#include "../utils/font_manager.h"
#include "../utils/app_config.h"
#include <stdbool.h>

/* 全局样式定义 */
lv_style_t g_style_btn_large;
lv_style_t g_style_btn_normal;
lv_style_t g_style_label_title;
lv_style_t g_style_label_normal;

/* 全局字体指针 */
static const lv_font_t *g_loaded_font = NULL;

/**
 * @brief 初始化全局样式
 */
void ui_common_init(void)
{
    log_info("初始化通用UI样式...");
    
    // ========== 字体加载 ==========
    const lv_font_t *loaded_font = &lv_font_simsun_16_cjk; // 默认使用内置字体
    
#if LV_USE_FREETYPE
    // 如果启用了 FreeType，尝试加载外部字体
    // 优先级：SD卡 > UDISK > 固件内置 > 临时目录 > 系统字体
    const char *font_paths[] = {
        "/mnt/SDCARD/fonts/simsun.ttc",     // SD卡 宋体（第一优先级，推荐）
        "/mnt/SDCARD/fonts/msyh.ttc",       // SD卡 微软雅黑
        "/mnt/SDCARD/fonts/simhei.ttf",     // SD卡 黑体
        "/mnt/UDISK/fonts/simsun.ttc",      // UDISK 宋体（备选）
        "/mnt/UDISK/fonts/msyh.ttc",        // UDISK 微软雅黑
        "/mnt/UDISK/fonts/simhei.ttf",      // UDISK 黑体
        "/usr/share/fonts/simsun.ttc",      // 固件内置宋体
        "/tmp/fonts/simsun.ttc",            // 临时目录宋体
        "/usr/share/fonts/truetype/droid/DroidSansFallback.ttf",  // 系统字体
        NULL
    };
    
    log_info("========== 开始加载字体 ==========");
    bool font_loaded = false;
    // 默认 18 像素字体（可在 ws_config.txt 里用 font_size= 覆盖）
    const int font_size = (g_app_config.font_size > 0 ? g_app_config.font_size : 18);
    log_info("字体大小: %d 像素", font_size);
    
    // 如果用户显式指定了字体路径，则优先尝试一次
    if (g_app_config.font_path[0]) {
        log_info("优先加载指定字体: %s", g_app_config.font_path);
        if (font_manager_init(g_app_config.font_path, font_size) == 0) {
            loaded_font = font_manager_get_main_font();
            log_info("✅ 字体加载成功: %s (大小: %dpx)", g_app_config.font_path, font_size);
            font_loaded = true;
        } else {
            log_warn("❌ 指定字体加载失败: %s", g_app_config.font_path);
        }
    }

    for (int i = 0; font_paths[i] != NULL; i++) {
        if (font_loaded) break;
        log_info("尝试加载: %s", font_paths[i]);
        if (font_manager_init(font_paths[i], font_size) == 0) {
            loaded_font = font_manager_get_main_font();
            log_info("✅ 字体加载成功: %s (大小: %dpx)", font_paths[i], font_size);
            font_loaded = true;
            break;
        } else {
            log_info("❌ 字体加载失败: %s", font_paths[i]);
        }
    }
    
    if (!font_loaded) {
        log_warn("⚠️ 所有外部字体加载失败，使用内置字体 lv_font_simsun_16_cjk");
        log_warn("   内置字体仅支持约1000个常用汉字，生僻字可能显示为方框");
    }
    log_info("========== 字体加载完成 ==========");
#else
    log_info("📝 FreeType 未启用，使用内置字体 lv_font_simsun_16_cjk");
    log_info("   内置字体支持约1000个常用汉字");
    log_info("💡 提示：启用 LV_USE_FREETYPE 可加载完整中文字体");
#endif
    
    // 保存加载的字体到全局变量
    g_loaded_font = loaded_font;
    
    /* 大按钮样式 */
    lv_style_init(&g_style_btn_large);
    lv_style_set_radius(&g_style_btn_large, 12);
    lv_style_set_bg_color(&g_style_btn_large, lv_color_hex(0x2196F3));
    lv_style_set_bg_grad_color(&g_style_btn_large, lv_color_hex(0x1976D2));
    lv_style_set_bg_grad_dir(&g_style_btn_large, LV_GRAD_DIR_VER);
    lv_style_set_shadow_width(&g_style_btn_large, 10);
    lv_style_set_shadow_color(&g_style_btn_large, lv_color_hex(0x0000FF));
    lv_style_set_shadow_ofs_y(&g_style_btn_large, 4);
    lv_style_set_border_width(&g_style_btn_large, 0);
    
    /* 普通按钮样式 */
    lv_style_init(&g_style_btn_normal);
    lv_style_set_radius(&g_style_btn_normal, 8);
    lv_style_set_bg_color(&g_style_btn_normal, lv_color_hex(0x2196F3));
    lv_style_set_border_width(&g_style_btn_normal, 0);
    lv_style_set_shadow_width(&g_style_btn_normal, 5);
    lv_style_set_shadow_ofs_y(&g_style_btn_normal, 2);
    
    /* 标题标签样式 - 使用加载的字体 */
    lv_style_init(&g_style_label_title);
    lv_style_set_text_font(&g_style_label_title, loaded_font);
    lv_style_set_text_color(&g_style_label_title, lv_color_hex(0x000000));
    
    /* 普通标签样式 - 使用加载的字体 */
    lv_style_init(&g_style_label_normal);
    lv_style_set_text_font(&g_style_label_normal, loaded_font);
    lv_style_set_text_color(&g_style_label_normal, lv_color_hex(0x333333));
    
    log_info("通用UI样式初始化完成");
}

/**
 * @brief 获取当前加载的字体
 * @return 加载的字体指针
 */
const lv_font_t* ui_common_get_font(void)
{
    // 如果有加载的字体，返回加载的字体
    if (g_loaded_font != NULL) {
        return g_loaded_font;
    }
    
    // 否则返回内置字体
    return &lv_font_simsun_16_cjk;
}

/**
 * @brief 清理全局样式
 */
void ui_common_deinit(void)
{
    lv_style_reset(&g_style_btn_large);
    lv_style_reset(&g_style_btn_normal);
    lv_style_reset(&g_style_label_title);
    lv_style_reset(&g_style_label_normal);
    
#if LV_USE_FREETYPE
    // 清理字体
    font_manager_deinit();
#endif
}

