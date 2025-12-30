/**
 * @file display_drv.h
 * @brief 显示驱动接口 - 支持Framebuffer和DRM
 */

#ifndef DISPLAY_DRV_H
#define DISPLAY_DRV_H

#include "lvgl.h"

/**
 * @brief 显示驱动类型
 */
typedef enum {
    DISP_DRV_FBDEV = 0,     // Linux Framebuffer
    DISP_DRV_DRM,           // Direct Rendering Manager
} display_drv_type_t;

/**
 * @brief 显示驱动配置
 */
typedef struct {
    display_drv_type_t type;
    const char *device_path;    // "/dev/fb0" 或 "/dev/dri/card0"
    int hor_res;                // 水平分辨率
    int ver_res;                // 垂直分辨率
    int rotation;               // 旋转角度 (0/90/180/270)
} display_config_t;

/**
 * @brief 初始化显示驱动
 * @return lv_disp_t* 显示对象指针，失败返回NULL
 */
lv_disp_t* display_drv_init(void);

/**
 * @brief 初始化显示驱动（自定义配置）
 * @param config 配置参数
 * @return lv_disp_t* 显示对象指针，失败返回NULL
 */
lv_disp_t* display_drv_init_ex(const display_config_t *config);

/**
 * @brief 反初始化显示驱动
 */
void display_drv_deinit(void);

/**
 * @brief 设置背光亮度
 * @param brightness 亮度值 (0-100)
 * @return int 成功返回0，失败返回负数
 */
int display_drv_set_backlight(int brightness);

#endif /* DISPLAY_DRV_H */


