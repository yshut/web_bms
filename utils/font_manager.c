/**
 * @file font_manager.c
 * @brief 字体管理器实现 - 动态加载 TTF 字体
 */

#include "font_manager.h"
#include "logger.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#if LV_USE_FREETYPE
#include "../../package/gui/littlevgl-8/lvgl/src/extra/libs/freetype/lv_freetype.h"

static lv_font_t *g_main_font = NULL;
static bool g_ft_initialized = false;

/**
 * @brief 检查字体文件是否存在且可读
 */
static bool check_font_file_exists(const char *font_path)
{
    struct stat st;
    if (access(font_path, F_OK) != 0) {
        return false;  // 文件不存在
    }
    
    if (access(font_path, R_OK) != 0) {
        log_warn("字体文件存在但不可读: %s", font_path);
        return false;  // 文件不可读
    }
    
    if (stat(font_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            log_warn("路径是目录而非文件: %s", font_path);
            return false;  // 是目录
        }
        if (st.st_size < 1024) {
            log_warn("字体文件太小 (%ld 字节): %s", st.st_size, font_path);
            return false;  // 文件太小，可能损坏
        }
    }
    
    return true;
}

int font_manager_init(const char *font_path, int size)
{
    static lv_ft_info_t info;
    
    // 检查文件是否存在
    if (!check_font_file_exists(font_path)) {
        return -1;  // 文件不存在或不可读
    }
    
    // 初始化 FreeType（只需一次）
    if (!g_ft_initialized) {
        // 参数: 缓存大小(KB), 最大字体数, 子像素渲染
        // TLSF 内存池最大 256KB，FreeType 缓存需要控制在合理范围
        if (!lv_freetype_init(64, 2, 0)) {
            log_error("FreeType 初始化失败");
            return -1;
        }
        g_ft_initialized = true;
        log_info("FreeType 库初始化成功 (缓存:64KB, 最大字体:2)");
    }
    
    // 配置字体信息
    info.name = font_path;
    info.weight = size;
    info.style = FT_FONT_STYLE_NORMAL;
    info.mem = NULL;
    
    // 加载字体
    log_info("正在加载字体文件: %s (大小: %d)", font_path, size);
    if (lv_ft_font_init(&info)) {
        g_main_font = info.font;
        return 0;
    } else {
        log_error("FreeType 无法解析字体文件: %s", font_path);
        return -1;
    }
}

lv_font_t* font_manager_load_font(const char *font_path, int size)
{
    static lv_ft_info_t info[10];  // 支持最多10个额外字体
    static int font_count = 0;
    
    if (font_count >= 10) {
        log_error("字体数量超过限制（最多10个额外字体）");
        return NULL;
    }
    
    if (!g_ft_initialized) {
        log_error("FreeType 未初始化，请先调用 font_manager_init()");
        return NULL;
    }
    
    // 检查文件是否存在
    if (!check_font_file_exists(font_path)) {
        log_warn("额外字体文件不存在: %s", font_path);
        return NULL;
    }
    
    info[font_count].name = font_path;
    info[font_count].weight = size;
    info[font_count].style = FT_FONT_STYLE_NORMAL;
    info[font_count].mem = NULL;
    
    if (lv_ft_font_init(&info[font_count])) {
        log_info("额外字体加载成功: %s (大小: %d)", font_path, size);
        return info[font_count++].font;
    }
    
    log_error("额外字体加载失败: %s", font_path);
    return NULL;
}

lv_font_t* font_manager_get_main_font(void)
{
    // 如果主字体已加载，返回主字体
    if (g_main_font) {
        return g_main_font;
    }
    
    // 否则返回内置备用字体
    log_warn("使用备用字体");
    return &lv_font_simsun_16_cjk;
}

void font_manager_deinit(void)
{
    // FreeType 会自动管理字体内存
    g_main_font = NULL;
    log_info("字体管理器已清理");
}

#else  /* LV_USE_FREETYPE */

// FreeType 未启用时的空实现
int font_manager_init(const char *font_path, int size)
{
    (void)font_path;
    (void)size;
    log_warn("FreeType 未启用，无法加载外部字体");
    return -1;
}

lv_font_t* font_manager_load_font(const char *font_path, int size)
{
    (void)font_path;
    (void)size;
    return NULL;
}

lv_font_t* font_manager_get_main_font(void)
{
    // 返回内置字体
    return &lv_font_simsun_16_cjk;
}

void font_manager_deinit(void)
{
    // 无需操作
}

#endif /* LV_USE_FREETYPE */

