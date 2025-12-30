#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H

#include "lvgl.h"

/**
 * @file font_manager.h
 * @brief 字体管理器 - 支持动态加载外部 TTF/TTC 字体
 * 
 * 使用说明：
 * 1. 将字体文件（如 simsun.ttc）放置到 /mnt/UDISK/fonts/ 目录
 * 2. 应用启动时会自动尝试加载字体
 * 3. 如果外部字体加载失败，会自动回退到内置字体
 * 
 * 推荐字体路径：
 * - 第一优先：/mnt/UDISK/fonts/simsun.ttc (宋体)
 * - 第二优先：/mnt/UDISK/fonts/msyh.ttc (微软雅黑)
 * - 备选：/usr/share/fonts/simsun.ttc (固件内置)
 */

/**
 * @brief 初始化字体管理器并加载主字体
 * @param font_path 字体文件完整路径（如：/mnt/UDISK/fonts/simsun.ttc）
 * @param size 字体大小（像素，建议 12-24）
 * @return 0=成功, -1=失败（文件不存在或格式错误）
 * @note 会自动检查文件是否存在和可读
 */
int font_manager_init(const char *font_path, int size);

/**
 * @brief 加载额外字体（可用于不同大小或样式）
 * @param font_path 字体文件路径
 * @param size 字体大小
 * @return 字体指针，失败返回NULL
 */
lv_font_t* font_manager_load_font(const char *font_path, int size);

/**
 * @brief 获取当前主字体
 * @return 主字体指针
 */
lv_font_t* font_manager_get_main_font(void);

/**
 * @brief 释放字体资源
 */
void font_manager_deinit(void);

#endif /* FONT_MANAGER_H */

