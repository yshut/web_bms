/**
 * @file touch_drv.c
 * @brief 触摸驱动实现 - 使用evdev
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

/* 触摸设备路径 */
#define TOUCH_DEVICE_PATH "/dev/input/event2"  /* GT9xx电容触摸屏 */

/* 全局变量 */
static int g_touch_fd = -1;
static lv_indev_t *g_indev = NULL;
static touch_config_t g_config;
static int g_last_x = 0;
static int g_last_y = 0;
static bool g_pressed = false;

/* 从输入设备读取ABS范围 */
static void probe_abs_range(int fd, touch_config_t *cfg)
{
    struct input_absinfo abs;
    int have_x = 0, have_y = 0;

    /* 优先使用多点触控坐标范围 */
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs) == 0) {
        cfg->min_x = abs.minimum;
        cfg->max_x = abs.maximum;
        have_x = 1;
    }
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs) == 0) {
        cfg->min_y = abs.minimum;
        cfg->max_y = abs.maximum;
        have_y = 1;
    }

    /* 回退到单点触控坐标范围 */
    if (!have_x && ioctl(fd, EVIOCGABS(ABS_X), &abs) == 0) {
        cfg->min_x = abs.minimum;
        cfg->max_x = abs.maximum;
        have_x = 1;
    }
    if (!have_y && ioctl(fd, EVIOCGABS(ABS_Y), &abs) == 0) {
        cfg->min_y = abs.minimum;
        cfg->max_y = abs.maximum;
        have_y = 1;
    }

    if (!have_x) { cfg->min_x = 0; cfg->max_x = 4095; }
    if (!have_y) { cfg->min_y = 0; cfg->max_y = 4095; }

    printf("[TOUCH_ABS] range X:[%d,%d] Y:[%d,%d]\n", cfg->min_x, cfg->max_x, cfg->min_y, cfg->max_y);
}

/**
 * @brief 触摸读取回调函数
 */
static void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    struct input_event ev;
    static int debug_counter = 0;  // 调试计数器
    
    /* 非阻塞读取触摸事件 */
    while (read(g_touch_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                g_last_x = ev.value;
            } else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                g_last_y = ev.value;
            } else if (ev.code == ABS_MT_TRACKING_ID) {
                /* 多点触控按压/抬起识别：-1 表示抬起，其它值表示按下 */
                if (ev.value == -1) {
                    g_pressed = false;
                } else if (ev.value >= 0) {
                    g_pressed = true;
                }
            } else if (ev.code == ABS_PRESSURE) {
                /* 压力大于0视为按下 */
                g_pressed = (ev.value > 0);
            }
        } else if (ev.type == EV_KEY) {
            if (ev.code == BTN_TOUCH || ev.code == BTN_LEFT || ev.code == BTN_MOUSE) {
                if (ev.value == 0) {
                    g_pressed = false;
                } else if (ev.value == 1) {
                    g_pressed = true;
                }
                // 每10次触摸打印一次调试信息
                if (g_pressed && (debug_counter++ % 10 == 0)) {
                    printf("[TOUCH_DEBUG] BTN event: pressed=%d x=%d y=%d\n", 
                           g_pressed, g_last_x, g_last_y);
                }
            }
        }
    }

    /* 坐标转换和校准 */
    int x = g_last_x;
    int y = g_last_y;
    
    /* 调试：打印原始坐标和映射后坐标 */
    static int map_debug = 0;
    if (g_pressed && (map_debug++ % 30 == 0)) {
        printf("[TOUCH_MAP] Raw: x=%d y=%d, Config: min(%d,%d) max(%d,%d)\n",
               x, y, g_config.min_x, g_config.min_y, g_config.max_x, g_config.max_y);
    }
    
    /* 交换XY（如需） */
    if (g_config.swap_xy) {
        int tmp = x;
        x = y;
        y = tmp;
    }
    
    /* 反转X */
    if (g_config.invert_x) {
        x = g_config.max_x - x + g_config.min_x;
    }
    
    /* 反转Y */
    if (g_config.invert_y) {
        y = g_config.max_y - y + g_config.min_y;
    }
    
    /* 映射到屏幕坐标（线性映射） */
    if (g_config.max_x > g_config.min_x) {
        x = (int)((long long)(x - g_config.min_x) * lv_disp_get_hor_res(NULL) / 
                  (g_config.max_x - g_config.min_x));
    }
    if (g_config.max_y > g_config.min_y) {
        y = (int)((long long)(y - g_config.min_y) * lv_disp_get_ver_res(NULL) / 
                  (g_config.max_y - g_config.min_y));
    }
    
    /* 调试：打印映射后坐标 */
    if (g_pressed && (map_debug % 30 == 1)) {
        printf("[TOUCH_MAP] Screen: x=%d y=%d (res: %dx%d) state=%s\n",
               x, y, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL),
               g_pressed ? "PRESSED" : "RELEASED");
    }
    
    /* 限制范围 */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= lv_disp_get_hor_res(NULL)) x = lv_disp_get_hor_res(NULL) - 1;
    if (y >= lv_disp_get_ver_res(NULL)) y = lv_disp_get_ver_res(NULL) - 1;
    
    /* 填充数据 - 关键：即使没有新事件也要持续更新坐标 */
    data->point.x = x;
    data->point.y = y;
    data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    
    /* 额外调试：打印最终返回给 LVGL 的数据 */
    if (g_pressed && (map_debug % 30 == 2)) {
        printf("[TOUCH_FINAL] Returning to LVGL: x=%d y=%d state=%s\n",
               data->point.x, data->point.y, 
               data->state == LV_INDEV_STATE_PRESSED ? "PRESSED" : "RELEASED");
    }
}

/**
 * @brief 初始化触摸驱动
 */
lv_indev_t* touch_drv_init(void)
{
    touch_config_t config = {
        .device_path = TOUCH_DEVICE_PATH,
        .swap_xy = 0,
        .invert_x = 0,
        .invert_y = 0,
        .min_x = 0,
        .max_x = 0,
        .min_y = 0,
        .max_y = 0,
    };

    /* 环境变量覆盖（便于快速调试） */
    const char *env_dev = getenv("LVGL_TOUCH_DEV");
    const char *env_swap = getenv("LVGL_TOUCH_SWAP_XY");
    const char *env_invx = getenv("LVGL_TOUCH_INVERT_X");
    const char *env_invy = getenv("LVGL_TOUCH_INVERT_Y");
    if (env_dev && *env_dev) config.device_path = env_dev;
    if (env_swap) config.swap_xy = atoi(env_swap);
    if (env_invx) config.invert_x = atoi(env_invx);
    if (env_invy) config.invert_y = atoi(env_invy);

    return touch_drv_init_ex(&config);
}

/**
 * @brief 初始化触摸驱动（自定义配置）
 */
lv_indev_t* touch_drv_init_ex(const touch_config_t *config)
{
    if (g_indev != NULL) {
        log_warn("触摸驱动已经初始化");
        return g_indev;
    }

    /* 保存配置 */
    memcpy(&g_config, config, sizeof(touch_config_t));

    /* 打开触摸设备 */
    g_touch_fd = open(config->device_path, O_RDONLY | O_NONBLOCK);
    if (g_touch_fd < 0) {
        log_error("无法打开触摸设备: %s", config->device_path);
        return NULL;
    }

    /* 获取设备信息 */
    char name[256] = "Unknown";
    ioctl(g_touch_fd, EVIOCGNAME(sizeof(name)), name);
    log_info("触摸设备: %s", name);

    /* 自动探测ABS范围（若未提供） */
    if (g_config.max_x <= g_config.min_x || g_config.max_y <= g_config.min_y) {
        probe_abs_range(g_touch_fd, &g_config);
    }

    printf("[TOUCH_CONF] dev=%s swap_xy=%d invert_x=%d invert_y=%d rangeX[%d,%d] rangeY[%d,%d]\n",
           g_config.device_path, g_config.swap_xy, g_config.invert_x, g_config.invert_y,
           g_config.min_x, g_config.max_x, g_config.min_y, g_config.max_y);

    /* 注册LVGL输入设备 */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    
    g_indev = lv_indev_drv_register(&indev_drv);
    if (g_indev == NULL) {
        log_error("注册触摸驱动失败");
        close(g_touch_fd);
        g_touch_fd = -1;
        return NULL;
    }

    log_info("触摸驱动初始化完成");
    return g_indev;
}

/**
 * @brief 反初始化触摸驱动
 */
void touch_drv_deinit(void)
{
    if (g_touch_fd >= 0) {
        close(g_touch_fd);
        g_touch_fd = -1;
    }
    
    g_indev = NULL;
    
    log_info("触摸驱动已反初始化");
}

/**
 * @brief 触摸校准（预留接口）
 */
int touch_drv_calibrate(void)
{
    log_info("触摸校准功能待实现");
    /* TODO: 实现触摸校准界面 */
    return 0;
}

