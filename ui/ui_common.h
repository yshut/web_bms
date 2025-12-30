/**
 * @file ui_common.h
 * @brief 通用UI组件和样式定义
 */

#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "lvgl.h"

/* ========== 颜色定义 ========== */
#define COLOR_PRIMARY       lv_color_hex(0x22C55E)  // 主色调（绿色）
#define COLOR_SUCCESS       lv_color_hex(0x10B981)  // 成功（绿色）
#define COLOR_WARNING       lv_color_hex(0xF59E0B)  // 警告（橙色）
#define COLOR_ERROR         lv_color_hex(0xEF4444)  // 错误（红色）
#define COLOR_INFO          lv_color_hex(0x3B82F6)  // 信息（蓝色）
#define COLOR_BG_LIGHT      lv_color_hex(0xF5F5F5)  // 浅背景
#define COLOR_BG_WHITE      lv_color_white()        // 白色背景
#define COLOR_TEXT_PRIMARY  lv_color_hex(0x1F2937)  // 主文本颜色
#define COLOR_TEXT_SECONDARY lv_color_hex(0x6B7280) // 次文本颜色
#define COLOR_BORDER        lv_color_hex(0xD1D5DB)  // 边框颜色

/* ========== 全局样式 ========== */
extern lv_style_t g_style_screen;          // 屏幕样式
extern lv_style_t g_style_container;       // 容器样式
extern lv_style_t g_style_btn_large;       // 大按钮样式
extern lv_style_t g_style_btn_normal;      // 普通按钮样式
extern lv_style_t g_style_label_normal;    // 普通标签样式
extern lv_style_t g_style_btn_small;       // 小按钮样式
extern lv_style_t g_style_label_title;     // 标题标签样式
extern lv_style_t g_style_label_subtitle;  // 副标题标签样式
extern lv_style_t g_style_textarea;        // 文本区域样式
extern lv_style_t g_style_dropdown;        // 下拉框样式

/**
 * @brief 初始化通用UI样式
 */
void ui_common_init(void);

/**
 * @brief 获取当前加载的字体
 * @return 加载的字体指针（如果 FreeType 加载成功）或内置字体
 */
const lv_font_t* ui_common_get_font(void);

/**
 * @brief 创建标题栏
 * @param parent 父对象
 * @param title 标题文本
 * @param show_back_btn 是否显示返回按钮
 * @return lv_obj_t* 标题栏对象
 */
lv_obj_t* ui_create_header(lv_obj_t *parent, const char *title, bool show_back_btn);

/**
 * @brief 创建返回按钮点击事件回调
 */
void ui_back_btn_event_cb(lv_event_t *e);

/**
 * @brief 创建LED指示灯
 * @param parent 父对象
 * @param color LED颜色
 * @return lv_obj_t* LED对象
 */
lv_obj_t* ui_create_led(lv_obj_t *parent, lv_color_t color);

/**
 * @brief 设置LED状态
 * @param led LED对象
 * @param active 是否激活
 */
void ui_set_led_state(lv_obj_t *led, bool active);

/**
 * @brief 显示消息框
 * @param title 标题
 * @param message 消息内容
 * @param type 类型（0:info, 1:success, 2:warning, 3:error）
 */
void ui_show_msgbox(const char *title, const char *message, int type);

/**
 * @brief 显示加载对话框
 * @param message 加载消息
 * @return lv_obj_t* 对话框对象
 */
lv_obj_t* ui_show_loading(const char *message);

/**
 * @brief 关闭加载对话框
 * @param loading 对话框对象
 */
void ui_close_loading(lv_obj_t *loading);

#endif /* UI_COMMON_H */

