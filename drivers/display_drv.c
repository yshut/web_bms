/**
 * @file display_drv.c
 * @brief 显示驱动实现 - 使用Linux Framebuffer
 */

#include "display_drv.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <string.h>

/* Framebuffer设备路径 */
#define FB_DEVICE_PATH "/dev/fb0"

/* 背光控制路径 */
#define BACKLIGHT_PATH "/sys/class/backlight/backlight/brightness"
#define BACKLIGHT_MAX_PATH "/sys/class/backlight/backlight/max_brightness"

/* 全局变量 */
static int g_fb_fd = -1;
static struct fb_var_screeninfo g_vinfo;
static struct fb_fix_screeninfo g_finfo;
static uint8_t *g_fb_mem = NULL;
static size_t g_fb_mem_size = 0;
static lv_disp_t *g_disp = NULL;
static lv_color_t *g_disp_buf1 = NULL;
static lv_color_t *g_disp_buf2 = NULL;

/**
 * @brief 刷新回调函数 - 将渲染缓冲复制到framebuffer
 */
static void display_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    if (g_fb_mem == NULL) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    int32_t x, y;
    uint32_t *fb_ptr;
    
    /* 逐行复制 */
    for (y = area->y1; y <= area->y2; y++) {
        fb_ptr = (uint32_t*)(g_fb_mem + (y * g_finfo.line_length) + (area->x1 * (g_vinfo.bits_per_pixel / 8)));
        
        for (x = area->x1; x <= area->x2; x++) {
            /* 从LVGL颜色转换到framebuffer格式 */
            lv_color_t c = *color_p;
            
            if (g_vinfo.bits_per_pixel == 32) {
                /* ARGB8888 */
                *fb_ptr = (0xFF << 24) | (c.ch.red << 16) | (c.ch.green << 8) | c.ch.blue;
            } else if (g_vinfo.bits_per_pixel == 16) {
                /* RGB565 */
                uint16_t color16 = ((c.ch.red >> 3) << 11) | 
                                   ((c.ch.green >> 2) << 5) | 
                                   (c.ch.blue >> 3);
                *(uint16_t*)fb_ptr = color16;
            }
            
            fb_ptr++;
            color_p++;
        }
    }

    /* 通知LVGL刷新完成 */
    lv_disp_flush_ready(disp_drv);
}

/**
 * @brief 打开framebuffer设备
 */
static int open_framebuffer(const char *dev_path)
{
    /* 打开framebuffer设备 */
    g_fb_fd = open(dev_path, O_RDWR);
    if (g_fb_fd < 0) {
        log_error("无法打开framebuffer设备: %s", dev_path);
        return -1;
    }

    /* 获取可变屏幕信息 */
    if (ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &g_vinfo) < 0) {
        log_error("无法获取可变屏幕信息");
        close(g_fb_fd);
        g_fb_fd = -1;
        return -1;
    }

    /* 获取固定屏幕信息 */
    if (ioctl(g_fb_fd, FBIOGET_FSCREENINFO, &g_finfo) < 0) {
        log_error("无法获取固定屏幕信息");
        close(g_fb_fd);
        g_fb_fd = -1;
        return -1;
    }

    /* 打印屏幕信息 */
    log_info("Framebuffer信息:");
    log_info("  分辨率: %dx%d", g_vinfo.xres, g_vinfo.yres);
    log_info("  虚拟分辨率: %dx%d", g_vinfo.xres_virtual, g_vinfo.yres_virtual);
    log_info("  位深度: %d bpp", g_vinfo.bits_per_pixel);
    log_info("  行字节数: %d", g_finfo.line_length);

    /* 映射framebuffer到内存 */
    g_fb_mem_size = g_finfo.line_length * g_vinfo.yres_virtual;
    g_fb_mem = (uint8_t*)mmap(NULL, g_fb_mem_size, PROT_READ | PROT_WRITE, 
                              MAP_SHARED, g_fb_fd, 0);
    
    if (g_fb_mem == MAP_FAILED) {
        log_error("无法映射framebuffer内存");
        close(g_fb_fd);
        g_fb_fd = -1;
        g_fb_mem = NULL;
        return -1;
    }

    log_info("Framebuffer初始化成功");
    return 0;
}

/**
 * @brief 初始化显示驱动
 */
lv_disp_t* display_drv_init(void)
{
    display_config_t config = {
        .type = DISP_DRV_FBDEV,
        .device_path = FB_DEVICE_PATH,
        .hor_res = 0,  // 自动检测
        .ver_res = 0,
        .rotation = 0,
    };
    
    return display_drv_init_ex(&config);
}

/**
 * @brief 初始化显示驱动（自定义配置）
 */
lv_disp_t* display_drv_init_ex(const display_config_t *config)
{
    if (g_disp != NULL) {
        log_warn("显示驱动已经初始化");
        return g_disp;
    }

    /* 打开framebuffer */
    if (open_framebuffer(config->device_path) < 0) {
        return NULL;
    }

    /* 获取分辨率 */
    int hor_res = (config->hor_res > 0) ? config->hor_res : g_vinfo.xres;
    int ver_res = (config->ver_res > 0) ? config->ver_res : g_vinfo.yres;

    /* 分配显示缓冲区（双缓冲） */
    size_t buf_size = hor_res * ver_res / 10;  // 1/10屏幕大小
    
    g_disp_buf1 = (lv_color_t*)malloc(buf_size * sizeof(lv_color_t));
    g_disp_buf2 = (lv_color_t*)malloc(buf_size * sizeof(lv_color_t));
    
    if (g_disp_buf1 == NULL || g_disp_buf2 == NULL) {
        log_error("无法分配显示缓冲区");
        display_drv_deinit();
        return NULL;
    }

    /* 创建LVGL显示驱动 */
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, g_disp_buf1, g_disp_buf2, buf_size);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.hor_res = hor_res;
    disp_drv.ver_res = ver_res;

    g_disp = lv_disp_drv_register(&disp_drv);
    if (g_disp == NULL) {
        log_error("注册显示驱动失败");
        display_drv_deinit();
        return NULL;
    }

    log_info("显示驱动初始化完成 (分辨率: %dx%d)", hor_res, ver_res);
    
    /* 设置默认背光亮度 */
    display_drv_set_backlight(80);
    
    return g_disp;
}

/**
 * @brief 反初始化显示驱动
 */
void display_drv_deinit(void)
{
    if (g_fb_mem != NULL && g_fb_mem != MAP_FAILED) {
        munmap(g_fb_mem, g_fb_mem_size);
        g_fb_mem = NULL;
    }

    if (g_fb_fd >= 0) {
        close(g_fb_fd);
        g_fb_fd = -1;
    }

    if (g_disp_buf1 != NULL) {
        free(g_disp_buf1);
        g_disp_buf1 = NULL;
    }

    if (g_disp_buf2 != NULL) {
        free(g_disp_buf2);
        g_disp_buf2 = NULL;
    }

    g_disp = NULL;
    
    log_info("显示驱动已反初始化");
}

/**
 * @brief 设置背光亮度
 */
int display_drv_set_backlight(int brightness)
{
    FILE *fp;
    int max_brightness = 255;
    
    /* 读取最大亮度值 */
    fp = fopen(BACKLIGHT_MAX_PATH, "r");
    if (fp != NULL) {
        fscanf(fp, "%d", &max_brightness);
        fclose(fp);
    }
    
    /* 限制范围 */
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    
    /* 计算实际亮度值 */
    int actual_brightness = (brightness * max_brightness) / 100;
    
    /* 写入亮度值 */
    fp = fopen(BACKLIGHT_PATH, "w");
    if (fp == NULL) {
        log_warn("无法打开背光控制文件");
        return -1;
    }
    
    fprintf(fp, "%d", actual_brightness);
    fclose(fp);
    
    log_debug("设置背光亮度: %d%% (%d/%d)", brightness, actual_brightness, max_brightness);
    return 0;
}


