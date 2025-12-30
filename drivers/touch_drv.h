/**
 * @file touch_drv.h
 * @brief 触摸驱动接口 - 使用evdev输入设备
 */

#ifndef TOUCH_DRV_H
#define TOUCH_DRV_H

#include "lvgl.h"

/**
 * @brief 触摸驱动配置
 */
typedef struct {
    const char *device_path;    // "/dev/input/eventX"
    int swap_xy;                // 交换X/Y坐标
    int invert_x;               // 反转X坐标
    int invert_y;               // 反转Y坐标
    int min_x;                  // X坐标最小值
    int max_x;                  // X坐标最大值
    int min_y;                  // Y坐标最小值
    int max_y;                  // Y坐标最大值
} touch_config_t;

/**
 * @brief 初始化触摸驱动
 * @return lv_indev_t* 输入设备指针，失败返回NULL
 */
lv_indev_t* touch_drv_init(void);

/**
 * @brief 初始化触摸驱动（自定义配置）
 * @param config 配置参数
 * @return lv_indev_t* 输入设备指针，失败返回NULL
 */
lv_indev_t* touch_drv_init_ex(const touch_config_t *config);

/**
 * @brief 反初始化触摸驱动
 */
void touch_drv_deinit(void);

/**
 * @brief 触摸校准
 * @return int 成功返回0，失败返回负数
 */
int touch_drv_calibrate(void);

#endif /* TOUCH_DRV_H */


