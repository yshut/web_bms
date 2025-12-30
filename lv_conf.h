/**
 * @file lv_conf.h
 * LVGL配置文件 for T113-S3
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* 颜色设置 */
#define LV_COLOR_DEPTH 32
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 0
#define LV_COLOR_MIX_ROUND_OFS 0

/* 内存设置 */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (240U * 1024U)  // 240KB（TLSF 最大支持 256KB）
#define LV_MEM_ADR 0
#define LV_MEM_BUF_MAX_NUM 16

/* HAL设置 */
#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30

/* 输入设备 */
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

/* 字体库: FreeType 动态字体加载（用于中文支持） */
#ifndef LV_USE_FREETYPE
#define LV_USE_FREETYPE 1
#endif
#if LV_USE_FREETYPE
#ifndef LV_FREETYPE_CACHE_SIZE
#define LV_FREETYPE_CACHE_SIZE 256
#endif
#endif
#define LV_USE_REFR_DEBUG 0

/* 字体 */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_SIMSUN_16_CJK 1     // 中文字体支持 (宋体 16px, 1000+ 常见汉字)
#define LV_FONT_DEFAULT &lv_font_simsun_16_cjk

/* 组件启用 */
#define LV_USE_BTN 1
#define LV_USE_LABEL 1
#define LV_USE_IMG 1
#define LV_USE_LINE 1
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 1
#define LV_USE_LIST 1
#define LV_USE_CHART 0
#define LV_USE_ROLLER 1
#define LV_USE_KEYBOARD 1
#define LV_USE_CANVAS 0
#define LV_USE_MSGBOX 1
#define LV_USE_SPINBOX 1
#define LV_USE_SPINNER 1
#define LV_USE_TABVIEW 1
#define LV_USE_TILEVIEW 1
#define LV_USE_WIN 1
#define LV_USE_SPAN 0
#define LV_USE_ANIMIMG 0
#define LV_USE_MENU 1
#define LV_USE_LED 1

/* 主题 */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0

/* 布局 */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/* 其他设置 */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_USER_DATA 1
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

/* GPU加速 (如果支持) */
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_NXP_PXP 0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL 0

/* 文件系统 */
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0

/* API */
#define LV_USE_API_MAP 1

#endif /* LV_CONF_H */
