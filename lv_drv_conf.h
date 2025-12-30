/**
 * @file lv_drv_conf.h
 * LVGL驱动配置文件
 */

#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

#include "lv_conf.h"

/* 显示驱动 */
#define USE_FBDEV 1
#define USE_DRM 0
#define USE_SDL 0

/* 输入设备驱动 */
#define USE_EVDEV 1
#define USE_XKB 0
#define USE_LIBINPUT 0

/* FBDEV配置 */
#if USE_FBDEV
#  define FBDEV_PATH "/dev/fb0"
#endif

/* EVDEV配置 */
#if USE_EVDEV
#  define EVDEV_NAME "/dev/input/event0"
#  define EVDEV_SWAP_AXES 0
#  define EVDEV_CALIBRATE 0
#endif

#endif /* LV_DRV_CONF_H */

