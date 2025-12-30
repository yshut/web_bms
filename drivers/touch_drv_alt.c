/**
 * @file touch_drv_alt.c
 * @brief 触摸驱动实现 - 基于 littlevgl-8 示例的标准 evdev 方式
 * @note 这是一个备选实现，如果默认的 touch_drv.c 有问题可以使用这个
 */

#include "touch_drv.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <string.h>

/* 触摸设备路径 - 优先尝试 touchscreen 符号链接 */
#define TOUCH_DEVICE_PATH "/dev/input/event2"

/* 全局变量 */
static int evdev_fd = -1;
static int evdev_root_x = 0;
static int evdev_root_y = 0;
static lv_indev_state_t evdev_button = LV_INDEV_STATE_RELEASED;
static lv_indev_t *g_indev = NULL;

/* 配置 */
static int g_swap_xy = 0;
static int g_calibrate = 0;
static int g_min_x = 0;
static int g_max_x = 4095;
static int g_min_y = 0;
static int g_max_y = 4095;

/* 坐标映射函数 */
static int map_coord(int x, int in_min, int in_max, int out_min, int out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/**
 * @brief 触摸读取回调（参考 lv_examples 实现）
 */
static void touch_read_cb_alt(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    struct input_event in;
    static int touch_cnt = 0;

    /* 读取所有可用事件 */
    while (read(evdev_fd, &in, sizeof(struct input_event)) > 0) {
        if (in.type == EV_REL) {
            /* 相对坐标（鼠标） */
            if (in.code == REL_X) {
                #if defined(EVDEV_SWAP_AXES) && EVDEV_SWAP_AXES
                    evdev_root_y += in.value;
                #else
                    evdev_root_x += in.value;
                #endif
            } else if (in.code == REL_Y) {
                #if defined(EVDEV_SWAP_AXES) && EVDEV_SWAP_AXES
                    evdev_root_x += in.value;
                #else
                    evdev_root_y += in.value;
                #endif
            }
        } else if (in.type == EV_ABS) {
            /* 绝对坐标（触摸屏） */
            if (in.code == ABS_X) {
                #if defined(EVDEV_SWAP_AXES) && EVDEV_SWAP_AXES
                    evdev_root_y = in.value;
                #else
                    evdev_root_x = in.value;
                #endif
            } else if (in.code == ABS_Y) {
                #if defined(EVDEV_SWAP_AXES) && EVDEV_SWAP_AXES
                    evdev_root_x = in.value;
                #else
                    evdev_root_y = in.value;
                #endif
            } else if (in.code == ABS_MT_POSITION_X) {
                /* 多点触控 X */
                #if defined(EVDEV_SWAP_AXES) && EVDEV_SWAP_AXES
                    evdev_root_y = in.value;
                #else
                    evdev_root_x = in.value;
                #endif
            } else if (in.code == ABS_MT_POSITION_Y) {
                /* 多点触控 Y */
                #if defined(EVDEV_SWAP_AXES) && EVDEV_SWAP_AXES
                    evdev_root_x = in.value;
                #else
                    evdev_root_y = in.value;
                #endif
            } else if (in.code == ABS_MT_TRACKING_ID) {
                /* 触点ID，-1 表示释放 */
                if (in.value == -1) {
                    evdev_button = LV_INDEV_STATE_RELEASED;
                } else if (in.value >= 0) {
                    evdev_button = LV_INDEV_STATE_PRESSED;
                }
            } else if (in.code == ABS_PRESSURE) {
                /* 压力值 */
                if (in.value == 0) {
                    evdev_button = LV_INDEV_STATE_RELEASED;
                } else if (in.value > 0) {
                    evdev_button = LV_INDEV_STATE_PRESSED;
                }
            }
        } else if (in.type == EV_KEY) {
            /* 按键事件 */
            if (in.code == BTN_MOUSE || in.code == BTN_TOUCH) {
                if (in.value == 0) {
                    evdev_button = LV_INDEV_STATE_RELEASED;
                } else if (in.value == 1) {
                    evdev_button = LV_INDEV_STATE_PRESSED;
                    
                    /* 调试输出 */
                    if (++touch_cnt % 10 == 0) {
                        printf("[TOUCH_ALT] Raw: x=%d y=%d state=%s\n",
                               evdev_root_x, evdev_root_y,
                               evdev_button == LV_INDEV_STATE_PRESSED ? "PRESSED" : "RELEASED");
                    }
                }
            }
        }
    }

    /* 处理坐标 */
    int x = evdev_root_x;
    int y = evdev_root_y;

    /* 是否需要校准/映射 */
    if (g_calibrate) {
        if (drv->disp && drv->disp->driver) {
            x = map_coord(evdev_root_x, g_min_x, g_max_x, 0, drv->disp->driver->hor_res);
            y = map_coord(evdev_root_y, g_min_y, g_max_y, 0, drv->disp->driver->ver_res);
        }
    }

    /* 限制范围 */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (drv->disp && drv->disp->driver) {
        if (x >= drv->disp->driver->hor_res) x = drv->disp->driver->hor_res - 1;
        if (y >= drv->disp->driver->ver_res) y = drv->disp->driver->ver_res - 1;
    }

    /* 填充返回数据 */
    data->point.x = x;
    data->point.y = y;
    data->state = evdev_button;

    /* 调试：显示映射后的坐标 */
    if (evdev_button == LV_INDEV_STATE_PRESSED && touch_cnt % 10 == 1) {
        printf("[TOUCH_ALT] Mapped: x=%d y=%d\n", x, y);
    }
}

/**
 * @brief 探测触摸设备的 ABS 范围
 */
static void probe_abs_range(int fd)
{
    struct input_absinfo abs;
    
    /* 尝试多点触控坐标 */
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs) == 0) {
        g_min_x = abs.minimum;
        g_max_x = abs.maximum;
        printf("[TOUCH_ALT] ABS_MT_POSITION_X: min=%d max=%d\n", g_min_x, g_max_x);
    } else if (ioctl(fd, EVIOCGABS(ABS_X), &abs) == 0) {
        g_min_x = abs.minimum;
        g_max_x = abs.maximum;
        printf("[TOUCH_ALT] ABS_X: min=%d max=%d\n", g_min_x, g_max_x);
    }
    
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs) == 0) {
        g_min_y = abs.minimum;
        g_max_y = abs.maximum;
        printf("[TOUCH_ALT] ABS_MT_POSITION_Y: min=%d max=%d\n", g_min_y, g_max_y);
    } else if (ioctl(fd, EVIOCGABS(ABS_Y), &abs) == 0) {
        g_min_y = abs.minimum;
        g_max_y = abs.maximum;
        printf("[TOUCH_ALT] ABS_Y: min=%d max=%d\n", g_min_y, g_max_y);
    }
}

/**
 * @brief 尝试打开触摸设备
 */
static int try_open_touch_device(const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
        /* 获取设备名称 */
        char name[256] = "Unknown";
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        printf("[TOUCH_ALT] 打开设备: %s (名称: %s)\n", path, name);
        return fd;
    }
    return -1;
}

/**
 * @brief 初始化触摸驱动（备选实现）
 */
lv_indev_t* touch_drv_init_alt(void)
{
    if (g_indev != NULL) {
        log_warn("触摸驱动已经初始化（备选模式）");
        return g_indev;
    }

    /* 环境变量配置 */
    const char *env_dev = getenv("LVGL_TOUCH_DEV");
    const char *env_swap = getenv("LVGL_TOUCH_SWAP_XY");
    const char *env_cal = getenv("LVGL_TOUCH_CALIBRATE");

    if (env_swap) g_swap_xy = atoi(env_swap);
    if (env_cal) g_calibrate = atoi(env_cal);

    /* 尝试打开触摸设备 - 多种路径 */
    const char *try_paths[] = {
        env_dev,                        /* 环境变量指定 */
        "/dev/input/touchscreen",       /* 符号链接（推荐） */
        "/dev/input/event2",            /* GT9xx 常用 */
        "/dev/input/event1",
        "/dev/input/event0",
        NULL
    };

    for (int i = 0; try_paths[i] != NULL; i++) {
        if (try_paths[i] == NULL || try_paths[i][0] == '\0') {
            continue;
        }
        
        evdev_fd = try_open_touch_device(try_paths[i]);
        if (evdev_fd >= 0) {
            /* 成功打开 */
            probe_abs_range(evdev_fd);
            
            /* 设置非阻塞 */
            fcntl(evdev_fd, F_SETFL, O_NONBLOCK);
            
            /* 初始化状态 */
            evdev_root_x = 0;
            evdev_root_y = 0;
            evdev_button = LV_INDEV_STATE_RELEASED;
            
            break;
        }
    }

    if (evdev_fd < 0) {
        log_error("无法打开任何触摸设备");
        return NULL;
    }

    /* 注册 LVGL 输入设备 */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb_alt;
    
    g_indev = lv_indev_drv_register(&indev_drv);
    if (g_indev == NULL) {
        log_error("注册触摸驱动失败（备选模式）");
        close(evdev_fd);
        evdev_fd = -1;
        return NULL;
    }

    log_info("触摸驱动初始化完成（备选模式，swap_xy=%d, calibrate=%d）", 
             g_swap_xy, g_calibrate);
    return g_indev;
}

/**
 * @brief 反初始化触摸驱动
 */
void touch_drv_deinit_alt(void)
{
    if (evdev_fd >= 0) {
        close(evdev_fd);
        evdev_fd = -1;
    }
    
    g_indev = NULL;
    
    log_info("触摸驱动已反初始化（备选模式）");
}


