/**
 * @file ui_uds.c
 * @brief UDS诊断界面实现
 */

#include "ui_uds.h"
#include "../logic/app_manager.h"
#include "../logic/can_handler.h"
#include "../logic/uds_handler.h"
#include "../utils/logger.h"
#include "ui_common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <pthread.h>

#define UDS_SYNC_LOG_LINES 8
#define UDS_SYNC_LOG_LEN 120
#define DEFAULT_TX_ID 0x7F3
#define DEFAULT_RX_ID 0x7FB

typedef struct {
    bool inited;
    bool running;
    int total_percent;
    int seg_index;
    int seg_total;
    char iface[16];
    uint32_t bitrate;
    char tx_id[16];
    char rx_id[16];
    uint32_t block_size;
    char s19_path[PATH_MAX];
    char can_status[96];
    int log_count;
    int log_head;
    char logs[UDS_SYNC_LOG_LINES][UDS_SYNC_LOG_LEN];
} uds_sync_state_t;

typedef struct {
    char iface[16];
    uint32_t bitrate;
    uint32_t tx_id;
    uint32_t rx_id;
    uint32_t block_size;
} uds_param_sync_msg_t;

static pthread_mutex_t g_uds_sync_lock = PTHREAD_MUTEX_INITIALIZER;
static uds_sync_state_t g_uds_sync = {0};
static ui_uds_t *g_active_uds = NULL;

static void uds_sync_init_defaults_locked(void)
{
    if (g_uds_sync.inited) return;
    memset(&g_uds_sync, 0, sizeof(g_uds_sync));
    g_uds_sync.inited = true;
    strncpy(g_uds_sync.iface, "can0", sizeof(g_uds_sync.iface) - 1);
    g_uds_sync.bitrate = 500000;
    strncpy(g_uds_sync.tx_id, "7F3", sizeof(g_uds_sync.tx_id) - 1);
    strncpy(g_uds_sync.rx_id, "7FB", sizeof(g_uds_sync.rx_id) - 1);
    g_uds_sync.block_size = 256;
    strncpy(g_uds_sync.can_status, "CAN状态: 未配置", sizeof(g_uds_sync.can_status) - 1);
    strncpy(g_uds_sync.logs[0], "[系统] 就绪", sizeof(g_uds_sync.logs[0]) - 1);
    g_uds_sync.log_count = 1;
    g_uds_sync.log_head = 1 % UDS_SYNC_LOG_LINES;
}

static void uds_sync_init_defaults(void)
{
    pthread_mutex_lock(&g_uds_sync_lock);
    uds_sync_init_defaults_locked();
    pthread_mutex_unlock(&g_uds_sync_lock);
}

static uint16_t uds_bitrate_to_dropdown_index(uint32_t bitrate)
{
    if (bitrate == 125000) return 0;
    if (bitrate == 250000) return 1;
    if (bitrate == 1000000) return 3;
    return 2;
}

static const char *uds_iface_to_channel_label(const char *iface)
{
    return (iface && strcmp(iface, "can1") == 0) ? "CAN2" : "CAN1";
}

static void uds_sync_set_params_locked(const char *iface, uint32_t bitrate, uint32_t tx_id, uint32_t rx_id, uint32_t block_size)
{
    uds_sync_init_defaults_locked();
    if (iface && iface[0]) {
        strncpy(g_uds_sync.iface, iface, sizeof(g_uds_sync.iface) - 1);
        g_uds_sync.iface[sizeof(g_uds_sync.iface) - 1] = '\0';
    }
    if (bitrate > 0) g_uds_sync.bitrate = bitrate;
    if (tx_id > 0) snprintf(g_uds_sync.tx_id, sizeof(g_uds_sync.tx_id), "%X", tx_id);
    if (rx_id > 0) snprintf(g_uds_sync.rx_id, sizeof(g_uds_sync.rx_id), "%X", rx_id);
    if (block_size > 0) g_uds_sync.block_size = block_size;
}

static void uds_sync_set_path_locked(const char *path)
{
    uds_sync_init_defaults_locked();
    if (!path) return;
    strncpy(g_uds_sync.s19_path, path, sizeof(g_uds_sync.s19_path) - 1);
    g_uds_sync.s19_path[sizeof(g_uds_sync.s19_path) - 1] = '\0';
}

static void uds_sync_set_running_locked(bool running)
{
    uds_sync_init_defaults_locked();
    g_uds_sync.running = running;
}

static void uds_sync_set_progress_locked(int total_percent, int seg_index, int seg_total)
{
    uds_sync_init_defaults_locked();
    if (total_percent < 0) total_percent = 0;
    if (total_percent > 100) total_percent = 100;
    g_uds_sync.total_percent = total_percent;
    g_uds_sync.seg_index = seg_index;
    g_uds_sync.seg_total = seg_total;
    if (total_percent >= 100) {
        g_uds_sync.running = false;
    }
}

static void uds_sync_set_can_status_locked(const char *status)
{
    uds_sync_init_defaults_locked();
    if (!status) return;
    strncpy(g_uds_sync.can_status, status, sizeof(g_uds_sync.can_status) - 1);
    g_uds_sync.can_status[sizeof(g_uds_sync.can_status) - 1] = '\0';
}

static void uds_sync_append_log_locked(const char *line)
{
    size_t len = 0;
    char *dst = NULL;
    uds_sync_init_defaults_locked();
    if (!line || !line[0]) return;
    dst = g_uds_sync.logs[g_uds_sync.log_head];
    len = strlen(line);
    if (len >= UDS_SYNC_LOG_LEN) len = UDS_SYNC_LOG_LEN - 1;
    memcpy(dst, line, len);
    dst[len] = '\0';
    g_uds_sync.log_head = (g_uds_sync.log_head + 1) % UDS_SYNC_LOG_LINES;
    if (g_uds_sync.log_count < UDS_SYNC_LOG_LINES) g_uds_sync.log_count++;
}

static void uds_sync_clear_logs_locked(void)
{
    uds_sync_init_defaults_locked();
    memset(g_uds_sync.logs, 0, sizeof(g_uds_sync.logs));
    g_uds_sync.log_count = 0;
    g_uds_sync.log_head = 0;
}

static void uds_sync_copy(uds_sync_state_t *out)
{
    if (!out) return;
    pthread_mutex_lock(&g_uds_sync_lock);
    uds_sync_init_defaults_locked();
    *out = g_uds_sync;
    pthread_mutex_unlock(&g_uds_sync_lock);
}

static void uds_sync_update_from_widgets(ui_uds_t *uds)
{
    char ch_buf[8] = {0};
    char br_buf[16] = {0};
    const char *sel_if = "can0";
    const char *tx_str = NULL;
    const char *rx_str = NULL;
    const char *blk_str = NULL;
    const char *s19 = NULL;
    uint32_t bitrate = 500000;
    uint32_t tx_id = DEFAULT_TX_ID;
    uint32_t rx_id = DEFAULT_RX_ID;
    uint32_t block_size = 256;

    if (!uds) return;
    if (uds->channel_dd) {
        lv_dropdown_get_selected_str(uds->channel_dd, ch_buf, sizeof(ch_buf));
        sel_if = (!strcmp(ch_buf, "CAN2")) ? "can1" : "can0";
    }
    if (uds->bitrate_dd) {
        lv_dropdown_get_selected_str(uds->bitrate_dd, br_buf, sizeof(br_buf));
        bitrate = (uint32_t)strtoul(br_buf, NULL, 10);
        if (bitrate == 0) bitrate = 500000;
    }
    tx_str = uds->tx_id_input ? lv_textarea_get_text(uds->tx_id_input) : NULL;
    rx_str = uds->rx_id_input ? lv_textarea_get_text(uds->rx_id_input) : NULL;
    blk_str = uds->blk_size_input ? lv_textarea_get_text(uds->blk_size_input) : NULL;
    s19 = uds->s19_path_input ? lv_textarea_get_text(uds->s19_path_input) : NULL;
    if (tx_str && tx_str[0]) tx_id = (uint32_t)strtoul(tx_str, NULL, 16);
    if (rx_str && rx_str[0]) rx_id = (uint32_t)strtoul(rx_str, NULL, 16);
    if (blk_str && blk_str[0]) block_size = (uint32_t)strtoul(blk_str, NULL, 10);

    pthread_mutex_lock(&g_uds_sync_lock);
    uds_sync_set_params_locked(sel_if, bitrate, tx_id, rx_id, block_size);
    uds_sync_set_path_locked(s19);
    pthread_mutex_unlock(&g_uds_sync_lock);
}

static void uds_sync_render_logs(ui_uds_t *uds, const uds_sync_state_t *state)
{
    int start = 0;
    int idx = 0;
    int i = 0;

    if (!uds || !uds->log_list || !state) return;
    lv_obj_clean(uds->log_list);
    if (state->log_count <= 0) {
        return;
    }
    start = (state->log_head - state->log_count + UDS_SYNC_LOG_LINES) % UDS_SYNC_LOG_LINES;
    for (i = 0; i < state->log_count; i++) {
        idx = (start + i) % UDS_SYNC_LOG_LINES;
        if (!state->logs[idx][0]) continue;
        lv_obj_t *item = lv_list_add_text(uds->log_list, state->logs[idx]);
        if (item) {
            lv_obj_set_style_text_font(item, ui_common_get_font(), 0);
            lv_obj_add_style(item, &g_style_label_normal, 0);
        }
    }
}

static void uds_sync_apply_to_widgets(ui_uds_t *uds, bool include_logs)
{
    uds_sync_state_t state;
    char seg_buf[48];

    if (!uds) return;
    uds_sync_copy(&state);

    if (uds->channel_dd) {
        lv_dropdown_set_selected(uds->channel_dd, strcmp(state.iface, "can1") == 0 ? 1 : 0);
    }
    if (uds->bitrate_dd) {
        lv_dropdown_set_selected(uds->bitrate_dd, uds_bitrate_to_dropdown_index(state.bitrate));
    }
    if (uds->tx_id_input) lv_textarea_set_text(uds->tx_id_input, state.tx_id);
    if (uds->rx_id_input) lv_textarea_set_text(uds->rx_id_input, state.rx_id);
    if (uds->blk_size_input) {
        char blk_buf[16];
        snprintf(blk_buf, sizeof(blk_buf), "%u", state.block_size);
        lv_textarea_set_text(uds->blk_size_input, blk_buf);
    }
    if (uds->s19_path_input) lv_textarea_set_text(uds->s19_path_input, state.s19_path);
    if (uds->can_status_label) lv_label_set_text(uds->can_status_label, state.can_status);
    if (uds->total_progress_bar) lv_bar_set_value(uds->total_progress_bar, state.total_percent, LV_ANIM_OFF);
    if (uds->total_progress_label) {
        char txt[32];
        snprintf(txt, sizeof(txt), "总进度: %d%%", state.total_percent);
        lv_label_set_text(uds->total_progress_label, txt);
    }
    if (uds->segment_progress_label) {
        if (state.seg_total > 0) {
            snprintf(seg_buf, sizeof(seg_buf), "分段: %d/%d", state.seg_index, state.seg_total);
            lv_label_set_text(uds->segment_progress_label, seg_buf);
        } else {
            lv_label_set_text(uds->segment_progress_label, "");
        }
    }
    if (uds->start_btn) {
        if (state.running) {
            lv_obj_add_state(uds->start_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(uds->start_btn, LV_STATE_DISABLED);
        }
    }
    if (include_logs) {
        uds_sync_render_logs(uds, &state);
    }
}

static void uds_apply_snapshot_async_cb(void *user_data)
{
    (void)user_data;
    if (!g_active_uds) return;
    uds_sync_apply_to_widgets(g_active_uds, true);
}

/* 输入框与软键盘交互 */
static void input_focus_cb(lv_event_t *e)
{
    ui_uds_t *uds = (ui_uds_t *)lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_target(e);
    lv_obj_scroll_to_view_recursive(obj, LV_ANIM_OFF);
    if (uds && uds->kb) {
        lv_keyboard_set_textarea(uds->kb, obj);
        if (uds->kb_cont) lv_obj_clear_flag(uds->kb_cont, LV_OBJ_FLAG_HIDDEN);
        if (uds->kb_preview) {
            const char *txt = lv_textarea_get_text(obj);
            lv_textarea_set_text(uds->kb_preview, txt ? txt : "");
        }
    }
}

static void input_value_changed_cb(lv_event_t *e)
{
    ui_uds_t *uds = (ui_uds_t *)lv_event_get_user_data(e);
    if (!uds) return;
    lv_obj_t *obj = lv_event_get_target(e);
    const char *txt = obj ? lv_textarea_get_text(obj) : "";
    if (uds->kb_preview) {
        lv_textarea_set_text(uds->kb_preview, txt ? txt : "");
    }
    uds_sync_update_from_widgets(uds);
}

static void kb_hide_btn_event_cb(lv_event_t *e)
{
    ui_uds_t *uds = (ui_uds_t *)lv_event_get_user_data(e);
    if (!uds) return;
    if (uds->kb_cont) lv_obj_add_flag(uds->kb_cont, LV_OBJ_FLAG_HIDDEN);
    if (uds->kb) lv_keyboard_set_textarea(uds->kb, NULL);
}

/* ========== UDS回调到LVGL主线程的桥接 ========== */
typedef struct {
    ui_uds_t *uds;
    int total_percent;
    int seg_index;
    int seg_total;
} uds_prog_msg_t;

typedef struct {
    ui_uds_t *uds;
    char *line;
} uds_log_msg_t;

static void uds_lvgl_apply_progress(void *p)
{
    uds_prog_msg_t *m = (uds_prog_msg_t*)p;
    if (m && m->uds) {
        if (m->uds->total_progress_bar) lv_bar_set_value(m->uds->total_progress_bar, m->total_percent, LV_ANIM_OFF);
        if (m->uds->total_progress_label) {
            char txt[32];
            snprintf(txt, sizeof(txt), "总进度: %d%%", m->total_percent);
            lv_label_set_text(m->uds->total_progress_label, txt);
        }
        if (m->uds->segment_progress_label) {
            char seg_txt[48];
            if (m->seg_total > 0) {
                snprintf(seg_txt, sizeof(seg_txt), "分段: %d/%d", m->seg_index, m->seg_total);
                lv_label_set_text(m->uds->segment_progress_label, seg_txt);
            } else {
                lv_label_set_text(m->uds->segment_progress_label, "");
            }
        }
    }
    if (m) free(m);
}

static void uds_lvgl_append_log(void *p)
{
    uds_log_msg_t *m = (uds_log_msg_t*)p;
    if (m && m->uds && m->uds->log_list && m->line) {
        /* 限制列表项数量，避免内存占用过大（TLSF 内存池最大 256KB） */
        const uint32_t MAX_ITEMS = 80;
        uint32_t cnt = lv_obj_get_child_cnt(m->uds->log_list);
        if (cnt > MAX_ITEMS) {
            uint32_t remove = cnt - MAX_ITEMS + 1;
            for (uint32_t i = 0; i < remove; i++) {
                lv_obj_t *child = lv_obj_get_child(m->uds->log_list, 0);
                if (child) lv_obj_del(child);
            }
        }

        lv_obj_t *it = lv_list_add_text(m->uds->log_list, m->line);
        if (it) {
            lv_obj_set_style_text_font(it, ui_common_get_font(), 0);
            lv_obj_add_style(it, &g_style_label_normal, 0);
        }
        
        /* 检测刷写完成或错误，重新启用开始刷写按钮 */
        if (m->uds->start_btn && 
            (strstr(m->line, "刷写完成") || strstr(m->line, "刷写成功") ||
             strstr(m->line, "ERROR") || strstr(m->line, "失败") || 
             strstr(m->line, "STOP"))) {
            lv_obj_clear_state(m->uds->start_btn, LV_STATE_DISABLED);
            log_info("刷写结束，重新启用开始按钮");
        }
    }
    if (m) {
        if (m->line) free(m->line);
        free(m);
    }
}

/* C回调实现（在线程中被调用）*/
static void uds_progress_cb_impl(int total_percent, int seg_index, int seg_total, void *user)
{
    ui_uds_t *uds = (ui_uds_t*)user;
    uds_prog_msg_t *msg = (uds_prog_msg_t*)malloc(sizeof(uds_prog_msg_t));
    pthread_mutex_lock(&g_uds_sync_lock);
    uds_sync_set_progress_locked(total_percent, seg_index, seg_total);
    pthread_mutex_unlock(&g_uds_sync_lock);
    if (!uds) uds = g_active_uds;
    if (!msg) return;
    msg->uds = uds;
    msg->total_percent = total_percent;
    msg->seg_index = seg_index;
    msg->seg_total = seg_total;
    lv_async_call(uds_lvgl_apply_progress, msg);
}

static void uds_log_cb_impl(const char *line, void *user)
{
    ui_uds_t *uds = (ui_uds_t*)user;
    uds_log_msg_t *msg = (uds_log_msg_t*)malloc(sizeof(uds_log_msg_t));
    pthread_mutex_lock(&g_uds_sync_lock);
    uds_sync_append_log_locked(line);
    if (line && (strstr(line, "刷写完成") || strstr(line, "刷写成功") ||
                 strstr(line, "ERROR") || strstr(line, "失败") || strstr(line, "STOP"))) {
        g_uds_sync.running = false;
    }
    pthread_mutex_unlock(&g_uds_sync_lock);
    if (!uds) uds = g_active_uds;
    if (!msg) return;
    msg->uds = uds;
    if (line) {
        /* 截断超长日志，减少内存压力 */
        size_t len = strlen(line);
        const size_t MAX_LEN = 256;
        if (len > MAX_LEN) len = MAX_LEN;
        msg->line = (char*)malloc(len + 1);
        if (msg->line) { memcpy(msg->line, line, len); msg->line[len] = '\0'; }
    } else {
        msg->line = NULL;
    }
    lv_async_call(uds_lvgl_append_log, msg);
}

/* ========== 文件选择对话框 ========== */
typedef struct {
    ui_uds_t *uds;
    lv_obj_t *modal;
    lv_obj_t *list;
    lv_obj_t *path_label;
    char cur_dir[PATH_MAX];
} file_dlg_ctx_t;

static bool has_ext_s19(const char *name)
{
    size_t n = strlen(name);
    return (n >= 4 && (strcasecmp(name + n - 4, ".s19") == 0));
}

static void file_dlg_close(file_dlg_ctx_t *ctx)
{
    if (!ctx) return;
    if (ctx->modal) lv_obj_del(ctx->modal);
    free(ctx);
}

static void file_dlg_populate(file_dlg_ctx_t *ctx);

static void file_item_event_cb(lv_event_t *e)
{
    file_dlg_ctx_t *ctx = (file_dlg_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t *btn = lv_event_get_target(e);
    const char *name = lv_list_get_btn_text(ctx->list, btn);
    if (!name) return;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", ctx->cur_dir, name);

    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        // 进入目录
        strncpy(ctx->cur_dir, path, sizeof(ctx->cur_dir) - 1);
        ctx->cur_dir[sizeof(ctx->cur_dir) - 1] = '\0';
        file_dlg_populate(ctx);
        return;
    }

    if (has_ext_s19(name)) {
        // 选择文件，填入路径并关闭
        if (ctx->uds && ctx->uds->s19_path_input) {
            lv_textarea_set_text(ctx->uds->s19_path_input, path);
            uds_sync_update_from_widgets(ctx->uds);
        }
        file_dlg_close(ctx);
    }
}

static void file_dlg_populate(file_dlg_ctx_t *ctx)
{
    if (!ctx) return;
    if (ctx->path_label) lv_label_set_text(ctx->path_label, ctx->cur_dir);
    if (ctx->list) lv_obj_clean(ctx->list);

    DIR *d = opendir(ctx->cur_dir);
    if (!d) return;
    struct dirent *ent;

    // 目录优先
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char p[PATH_MAX];
        snprintf(p, sizeof(p), "%s/%s", ctx->cur_dir, ent->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
            lv_obj_t *b = lv_list_add_btn(ctx->list, LV_SYMBOL_DIRECTORY, ent->d_name);
            lv_obj_add_event_cb(b, file_item_event_cb, LV_EVENT_CLICKED, ctx);
        }
    }
    rewinddir(d);
    // .s19 文件
    while ((ent = readdir(d)) != NULL) {
        if (has_ext_s19(ent->d_name)) {
            lv_obj_t *b = lv_list_add_btn(ctx->list, LV_SYMBOL_FILE, ent->d_name);
            lv_obj_add_event_cb(b, file_item_event_cb, LV_EVENT_CLICKED, ctx);
        }
    }
    closedir(d);
}

static void file_dlg_up_event_cb(lv_event_t *e)
{
    file_dlg_ctx_t *ctx = (file_dlg_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    // 上一级，但限制在 /mnt/UDISK 根内
    char *slash = strrchr(ctx->cur_dir, '/');
    if (slash && slash != ctx->cur_dir) {
        *slash = '\0';
        if (strcmp(ctx->cur_dir, "/mnt") == 0) strcpy(ctx->cur_dir, "/mnt/UDISK");
        file_dlg_populate(ctx);
    }
}

static void file_dlg_refresh_event_cb(lv_event_t *e)
{
    file_dlg_ctx_t *ctx = (file_dlg_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    file_dlg_populate(ctx);
}

static void file_dlg_cancel_event_cb(lv_event_t *e)
{
    file_dlg_ctx_t *ctx = (file_dlg_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    file_dlg_close(ctx);
}

static void open_file_dialog(ui_uds_t *uds)
{
    file_dlg_ctx_t *ctx = (file_dlg_ctx_t *)malloc(sizeof(file_dlg_ctx_t));
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->uds = uds;
    strncpy(ctx->cur_dir, "/mnt/UDISK", sizeof(ctx->cur_dir) - 1);

    ctx->modal = lv_obj_create(uds->screen);
    lv_obj_set_size(ctx->modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(ctx->modal, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(ctx->modal, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(ctx->modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dlg = lv_obj_create(ctx->modal);
    lv_obj_set_size(dlg, LV_PCT(90), LV_PCT(80));
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(dlg, 0, 0);

    lv_obj_t *title = lv_label_create(dlg);
    lv_label_set_text(title, "Select S19 file");
    lv_obj_set_style_text_font(title, ui_common_get_font(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

    ctx->path_label = lv_label_create(dlg);
    lv_label_set_text(ctx->path_label, ctx->cur_dir);
    lv_obj_set_style_text_font(ctx->path_label, ui_common_get_font(), 0);
    lv_obj_align(ctx->path_label, LV_ALIGN_TOP_LEFT, 10, 30);

    // 控制按钮：Up, Refresh, Cancel
    lv_obj_t *up_btn = lv_btn_create(dlg);
    lv_obj_set_size(up_btn, 80, 30);
    lv_obj_align(up_btn, LV_ALIGN_TOP_RIGHT, -190, 6);
    lv_obj_add_event_cb(up_btn, file_dlg_up_event_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *up_label = lv_label_create(up_btn);
    lv_label_set_text(up_label, LV_SYMBOL_UP " Up");
    lv_obj_set_style_text_font(up_label, ui_common_get_font(), 0);
    lv_obj_center(up_label);

    lv_obj_t *refresh_btn = lv_btn_create(dlg);
    lv_obj_set_size(refresh_btn, 90, 30);
    lv_obj_align(refresh_btn, LV_ALIGN_TOP_RIGHT, -100, 6);
    lv_obj_add_event_cb(refresh_btn, file_dlg_refresh_event_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_label, LV_SYMBOL_REFRESH " Refresh");
    lv_obj_set_style_text_font(refresh_label, ui_common_get_font(), 0);
    lv_obj_center(refresh_label);

    lv_obj_t *cancel_btn = lv_btn_create(dlg);
    lv_obj_set_size(cancel_btn, 90, 30);
    lv_obj_align(cancel_btn, LV_ALIGN_TOP_RIGHT, -10, 6);
    lv_obj_add_event_cb(cancel_btn, file_dlg_cancel_event_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, ui_common_get_font(), 0);
    lv_obj_center(cancel_label);

    ctx->list = lv_list_create(dlg);
    lv_obj_set_size(ctx->list, LV_PCT(98), LV_PCT(100) - 70);
    lv_obj_align(ctx->list, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(ctx->list, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(ctx->list, LV_SCROLLBAR_MODE_AUTO);

    file_dlg_populate(ctx);
}

static void browse_btn_event_cb(lv_event_t *e)
{
    ui_uds_t *uds = (ui_uds_t *)lv_event_get_user_data(e);
    if (!uds) return;
    open_file_dialog(uds);
}

static void dropdown_value_changed_cb(lv_event_t *e)
{
    ui_uds_t *uds = (ui_uds_t *)lv_event_get_user_data(e);
    if (!uds) return;
    uds_sync_update_from_widgets(uds);
}

static void back_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        log_info("从刷写诊断返回主页");
        app_manager_switch_to_page(APP_PAGE_HOME);
    }
}

static void scan_can_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        log_info("扫描CAN接口");
        // TODO: 实现CAN接口扫描
        ui_uds_t *uds = (ui_uds_t *)lv_event_get_user_data(e);
        if (uds && uds->log_list) {
            lv_obj_t *item = lv_list_add_text(uds->log_list, "[System] Start scanning CAN...");
            if (item) {
                lv_obj_set_style_text_font(item, ui_common_get_font(), 0);
            }
        }
    }
}

static void configure_can_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        log_info("配置CAN接口");
        ui_uds_t *uds = (ui_uds_t *)lv_event_get_user_data(e);
        if (!uds) return;
        
        // 读取通道与波特率
        char ch_buf[8];
        lv_dropdown_get_selected_str(uds->channel_dd, ch_buf, sizeof(ch_buf));
        const char *sel_if = (!strcmp(ch_buf, "CAN2")) ? "can1" : "can0";
        char br_buf[16];
        lv_dropdown_get_selected_str(uds->bitrate_dd, br_buf, sizeof(br_buf));
        uint32_t bitrate = (uint32_t)strtoul(br_buf, NULL, 10);

        // 配置并初始化所选接口
        if (can_handler_configure(sel_if, bitrate) < 0 || can_handler_init(sel_if, bitrate) < 0) {
            log_error("CAN初始化失败");
            if (uds->log_list) {
            lv_obj_t *item = lv_list_add_text(uds->log_list, "[ERROR] CAN init failed");
                if (item) {
                    lv_obj_set_style_text_font(item, ui_common_get_font(), 0);
                    lv_obj_set_style_text_color(item, lv_color_hex(0xFF0000), 0);
                }
            }
            return;
        }
        
        if (uds->log_list) {
            char line[96];
            snprintf(line, sizeof(line), "[OK] CAN configured (%s %u)", sel_if, bitrate);
            lv_obj_t *item = lv_list_add_text(uds->log_list, line);
            if (item) {
                lv_obj_set_style_text_font(item, ui_common_get_font(), 0);
                lv_obj_set_style_text_color(item, lv_color_hex(0x00FF00), 0);
            }
        }
        
        // 更新状态标签
        if (uds->can_status_label) {
            char status[96];
            snprintf(status, sizeof(status), "CAN状态: 已配置 (%s)", sel_if);
            lv_label_set_text(uds->can_status_label, status);
            pthread_mutex_lock(&g_uds_sync_lock);
            uds_sync_set_can_status_locked(status);
            pthread_mutex_unlock(&g_uds_sync_lock);
        }
        uds_sync_update_from_widgets(uds);
    }
}

static int uds_start_with_values(ui_uds_t *uds,
                                 const char *sel_if,
                                 uint32_t bitrate,
                                 uint32_t tx,
                                 uint32_t rx,
                                 uint32_t blk,
                                 const char *s19)
{
    uds_config_t cfg;

    if (!sel_if || !s19 || !s19[0]) {
        pthread_mutex_lock(&g_uds_sync_lock);
        uds_sync_append_log_locked("[ERROR] 未设置S19文件路径");
        pthread_mutex_unlock(&g_uds_sync_lock);
        lv_async_call(uds_apply_snapshot_async_cb, NULL);
        return -1;
    }

    pthread_mutex_lock(&g_uds_sync_lock);
    uds_sync_set_params_locked(sel_if, bitrate, tx, rx, blk);
    uds_sync_set_path_locked(s19);
    pthread_mutex_unlock(&g_uds_sync_lock);

    if (can_handler_configure(sel_if, bitrate) < 0 || can_handler_init(sel_if, bitrate) < 0) {
        pthread_mutex_lock(&g_uds_sync_lock);
        uds_sync_append_log_locked("[ERROR] CAN init failed");
        pthread_mutex_unlock(&g_uds_sync_lock);
        lv_async_call(uds_apply_snapshot_async_cb, NULL);
        return -1;
    }

    cfg.interface = sel_if;
    cfg.tx_id = tx;
    cfg.rx_id = rx;
    cfg.block_size = blk;
    cfg.s19_path = s19;
    if (uds_init(&cfg) != 0) {
        pthread_mutex_lock(&g_uds_sync_lock);
        uds_sync_append_log_locked("[ERROR] UDS init failed");
        pthread_mutex_unlock(&g_uds_sync_lock);
        lv_async_call(uds_apply_snapshot_async_cb, NULL);
        return -1;
    }

    uds_register_progress_cb(uds_progress_cb_impl, uds ? uds : g_active_uds);
    uds_register_log_cb(uds_log_cb_impl, uds ? uds : g_active_uds);

    if (uds_start() != 0) {
        pthread_mutex_lock(&g_uds_sync_lock);
        uds_sync_set_running_locked(false);
        uds_sync_append_log_locked("[ERROR] UDS start failed");
        pthread_mutex_unlock(&g_uds_sync_lock);
        lv_async_call(uds_apply_snapshot_async_cb, NULL);
        return -1;
    }

    pthread_mutex_lock(&g_uds_sync_lock);
    uds_sync_set_running_locked(true);
    uds_sync_set_progress_locked(0, 0, 0);
    uds_sync_append_log_locked("[START] UDS flashing...");
    pthread_mutex_unlock(&g_uds_sync_lock);

    if (uds && uds->start_btn) {
        lv_obj_add_state(uds->start_btn, LV_STATE_DISABLED);
        log_info("开始刷写，禁用开始按钮");
    }
    lv_async_call(uds_apply_snapshot_async_cb, NULL);
    return 0;
}

static void start_flash_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        log_info("开始UDS刷写");
        ui_uds_t *uds = (ui_uds_t *)lv_event_get_user_data(e);
        if (!uds) return;
        
        // 读取配置
        char ch_buf[8], br_buf[16];
        lv_dropdown_get_selected_str(uds->channel_dd, ch_buf, sizeof(ch_buf));
        const char *sel_if = (!strcmp(ch_buf, "CAN2")) ? "can1" : "can0";
        lv_dropdown_get_selected_str(uds->bitrate_dd, br_buf, sizeof(br_buf));
        uint32_t bitrate = (uint32_t)strtoul(br_buf, NULL, 10);

        const char *tx_str = lv_textarea_get_text(uds->tx_id_input);
        const char *rx_str = lv_textarea_get_text(uds->rx_id_input);
        const char *blk_str = lv_textarea_get_text(uds->blk_size_input);
        const char *s19   = lv_textarea_get_text(uds->s19_path_input);
        uint32_t tx = strtoul(tx_str, NULL, 16);
        uint32_t rx = strtoul(rx_str, NULL, 16);
        uint32_t blk = strtoul(blk_str, NULL, 10);
        uds_sync_update_from_widgets(uds);
        uds_start_with_values(uds, sel_if, bitrate, tx, rx, blk, s19);
    }
}

static void stop_flash_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        log_info("停止UDS刷写");
        ui_uds_t *uds = (ui_uds_t *)lv_event_get_user_data(e);
        if (!uds) return;
        uds_stop();
        uds_deinit();
        pthread_mutex_lock(&g_uds_sync_lock);
        uds_sync_set_running_locked(false);
        uds_sync_append_log_locked("[STOP] UDS flashing stopped");
        pthread_mutex_unlock(&g_uds_sync_lock);
        /* 重新启用开始刷写按钮 */
        if (uds->start_btn) {
            lv_obj_clear_state(uds->start_btn, LV_STATE_DISABLED);
            log_info("停止刷写，重新启用开始按钮");
        }
        lv_async_call(uds_apply_snapshot_async_cb, NULL);
    }
}

ui_uds_t* ui_uds_create(void)
{
    log_info("创建UDS诊断界面...");

    uds_sync_init_defaults();
    ui_uds_t *uds = malloc(sizeof(ui_uds_t));
    if (!uds) return NULL;
    memset(uds, 0, sizeof(*uds));
    g_active_uds = uds;

    uds->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(uds->screen, lv_color_hex(0xF0F0F0), 0);
    
    /* 顶部标题栏 */
    lv_obj_t *header = lv_obj_create(uds->screen);
    lv_obj_set_size(header, LV_PCT(100), 60);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 80, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " 返回");
    lv_obj_set_style_text_font(back_label, ui_common_get_font(), 0);
    lv_obj_center(back_label);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "刷写诊断");
    lv_obj_set_style_text_font(title, ui_common_get_font(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(title);
    
    /* 可滚动主体 */
    lv_obj_t *body = lv_obj_create(uds->screen);
    int32_t body_h = lv_obj_get_height(uds->screen) - 60;
    if (body_h < 50) body_h = 50;
    lv_obj_set_size(body, LV_PCT(100), body_h);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_style_pad_all(body, 6, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_style_pad_bottom(body, 120, 0);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_outline_width(body, 0, 0);

    /* CAN配置区域 */
    lv_obj_t *can_config_cont = lv_obj_create(body);
    lv_obj_set_size(can_config_cont, LV_PCT(100), 120);
    lv_obj_set_style_bg_color(can_config_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(can_config_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(can_config_cont, 0, 0);
    
    // CAN status label
    uds->can_status_label = lv_label_create(can_config_cont);
    lv_label_set_text(uds->can_status_label, "CAN状态: 未配置");
    lv_obj_set_style_text_font(uds->can_status_label, ui_common_get_font(), 0);
    lv_obj_align(uds->can_status_label, LV_ALIGN_TOP_LEFT, 10, 8);
    
    // 通道/波特率
    lv_obj_t *ch_label = lv_label_create(can_config_cont);
    lv_label_set_text(ch_label, "通道:");
    lv_obj_set_style_text_font(ch_label, ui_common_get_font(), 0);
    lv_obj_align(ch_label, LV_ALIGN_TOP_LEFT, 10, 36);
    uds->channel_dd = lv_dropdown_create(can_config_cont);
    lv_dropdown_set_options_static(uds->channel_dd, "CAN1\nCAN2");
    lv_obj_set_size(uds->channel_dd, 90, 32);
    lv_obj_align(uds->channel_dd, LV_ALIGN_TOP_LEFT, 70, 30);
    lv_obj_add_event_cb(uds->channel_dd, dropdown_value_changed_cb, LV_EVENT_VALUE_CHANGED, uds);

    lv_obj_t *br_label = lv_label_create(can_config_cont);
    lv_label_set_text(br_label, "波特率:");
    lv_obj_set_style_text_font(br_label, ui_common_get_font(), 0);
    lv_obj_align(br_label, LV_ALIGN_TOP_LEFT, 175, 36);
    uds->bitrate_dd = lv_dropdown_create(can_config_cont);
    lv_dropdown_set_options_static(uds->bitrate_dd, "125000\n250000\n500000\n1000000");
    lv_dropdown_set_selected(uds->bitrate_dd, 2);
    lv_obj_set_size(uds->bitrate_dd, 110, 32);
    lv_obj_align(uds->bitrate_dd, LV_ALIGN_TOP_LEFT, 235, 30);
    lv_obj_add_event_cb(uds->bitrate_dd, dropdown_value_changed_cb, LV_EVENT_VALUE_CHANGED, uds);

    // IDs 与块大小
    lv_obj_t *tx_label = lv_label_create(can_config_cont);
    lv_label_set_text(tx_label, "发送ID:");
    lv_obj_set_style_text_font(tx_label, ui_common_get_font(), 0);
    lv_obj_align(tx_label, LV_ALIGN_TOP_LEFT, 365, 36);
    uds->tx_id_input = lv_textarea_create(can_config_cont);
    lv_obj_set_size(uds->tx_id_input, 80, 30);
    lv_obj_align(uds->tx_id_input, LV_ALIGN_TOP_LEFT, 420, 30);
    lv_textarea_set_one_line(uds->tx_id_input, true);
    lv_textarea_set_text(uds->tx_id_input, "7F3");
    lv_obj_add_event_cb(uds->tx_id_input, input_focus_cb, LV_EVENT_FOCUSED, uds);
    lv_obj_add_event_cb(uds->tx_id_input, input_focus_cb, LV_EVENT_CLICKED, uds);
    lv_obj_add_event_cb(uds->tx_id_input, input_value_changed_cb, LV_EVENT_VALUE_CHANGED, uds);

    lv_obj_t *rx_label = lv_label_create(can_config_cont);
    lv_label_set_text(rx_label, "接收ID:");
    lv_obj_set_style_text_font(rx_label, ui_common_get_font(), 0);
    lv_obj_align(rx_label, LV_ALIGN_TOP_LEFT, 510, 36);
    uds->rx_id_input = lv_textarea_create(can_config_cont);
    lv_obj_set_size(uds->rx_id_input, 80, 30);
    lv_obj_align(uds->rx_id_input, LV_ALIGN_TOP_LEFT, 565, 30);
    lv_textarea_set_one_line(uds->rx_id_input, true);
    lv_textarea_set_text(uds->rx_id_input, "7FB");
    lv_obj_add_event_cb(uds->rx_id_input, input_focus_cb, LV_EVENT_FOCUSED, uds);
    lv_obj_add_event_cb(uds->rx_id_input, input_focus_cb, LV_EVENT_CLICKED, uds);
    lv_obj_add_event_cb(uds->rx_id_input, input_value_changed_cb, LV_EVENT_VALUE_CHANGED, uds);

    lv_obj_t *blk_label = lv_label_create(can_config_cont);
    lv_label_set_text(blk_label, "块大小:");
    lv_obj_set_style_text_font(blk_label, ui_common_get_font(), 0);
    lv_obj_align(blk_label, LV_ALIGN_TOP_LEFT, 655, 36);
    uds->blk_size_input = lv_textarea_create(can_config_cont);
    lv_obj_set_size(uds->blk_size_input, 70, 30);
    lv_obj_align(uds->blk_size_input, LV_ALIGN_TOP_LEFT, 710, 30);
    lv_textarea_set_one_line(uds->blk_size_input, true);
    lv_textarea_set_text(uds->blk_size_input, "256");
    lv_obj_add_event_cb(uds->blk_size_input, input_focus_cb, LV_EVENT_FOCUSED, uds);
    lv_obj_add_event_cb(uds->blk_size_input, input_focus_cb, LV_EVENT_CLICKED, uds);
    lv_obj_add_event_cb(uds->blk_size_input, input_value_changed_cb, LV_EVENT_VALUE_CHANGED, uds);

    // CAN config buttons
    lv_obj_t *scan_can_btn = lv_btn_create(can_config_cont);
    lv_obj_set_size(scan_can_btn, 90, 32);
    lv_obj_align(scan_can_btn, LV_ALIGN_TOP_LEFT, 790, 30);
    lv_obj_add_event_cb(scan_can_btn, scan_can_btn_event_cb, LV_EVENT_CLICKED, uds);
    lv_obj_t *scan_label = lv_label_create(scan_can_btn);
    lv_label_set_text(scan_label, "扫描");
    lv_obj_set_style_text_font(scan_label, ui_common_get_font(), 0);
    lv_obj_center(scan_label);
    
    lv_obj_t *config_can_btn = lv_btn_create(can_config_cont);
    lv_obj_set_size(config_can_btn, 90, 32);
    lv_obj_align(config_can_btn, LV_ALIGN_TOP_LEFT, 885, 30);
    lv_obj_add_event_cb(config_can_btn, configure_can_btn_event_cb, LV_EVENT_CLICKED, uds);
    lv_obj_t *config_label = lv_label_create(config_can_btn);
    lv_label_set_text(config_label, "配置");
    lv_obj_set_style_text_font(config_label, ui_common_get_font(), 0);
    lv_obj_center(config_label);
    
    /* 文件路径区域 */
    lv_obj_t *file_cont = lv_obj_create(body);
    lv_obj_set_size(file_cont, LV_PCT(100), 70);
    lv_obj_set_style_bg_color(file_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(file_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(file_cont, 0, 0);

    lv_obj_t *s19_label = lv_label_create(file_cont);
    lv_label_set_text(s19_label, "S19:");
    lv_obj_set_style_text_font(s19_label, ui_common_get_font(), 0);
    lv_obj_align(s19_label, LV_ALIGN_TOP_LEFT, 10, 10);
    uds->s19_path_input = lv_textarea_create(file_cont);
    lv_obj_set_size(uds->s19_path_input, 700, 32);
    lv_obj_align(uds->s19_path_input, LV_ALIGN_TOP_LEFT, 50, 6);
    lv_textarea_set_one_line(uds->s19_path_input, true);
    /* 为中文文件名启用UTF-8，LVGL默认即为UTF-8，仅需确保字体支持中文 */
    lv_textarea_set_placeholder_text(uds->s19_path_input, "/mnt/UDISK/固件.s19");
    lv_obj_add_event_cb(uds->s19_path_input, input_focus_cb, LV_EVENT_FOCUSED, uds);
    lv_obj_add_event_cb(uds->s19_path_input, input_focus_cb, LV_EVENT_CLICKED, uds);
    lv_obj_add_event_cb(uds->s19_path_input, input_value_changed_cb, LV_EVENT_VALUE_CHANGED, uds);

    uds->browse_btn = lv_btn_create(file_cont);
    lv_obj_set_size(uds->browse_btn, 90, 32);
    lv_obj_align(uds->browse_btn, LV_ALIGN_TOP_LEFT, 760, 6);
    lv_obj_add_event_cb(uds->browse_btn, browse_btn_event_cb, LV_EVENT_CLICKED, uds);
    lv_obj_t *browse_label = lv_label_create(uds->browse_btn);
    lv_label_set_text(browse_label, "浏览");
    lv_obj_set_style_text_font(browse_label, ui_common_get_font(), 0);
    lv_obj_center(browse_label);

    /* 进度条区域 */
    lv_obj_t *progress_cont = lv_obj_create(body);
    lv_obj_set_size(progress_cont, LV_PCT(100), 110);
    lv_obj_set_style_bg_color(progress_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(progress_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(progress_cont, 0, 0);
    
    // Total progress
    uds->total_progress_label = lv_label_create(progress_cont);
    lv_label_set_text(uds->total_progress_label, "总进度: 0%");
    lv_obj_set_style_text_font(uds->total_progress_label, ui_common_get_font(), 0);
    lv_obj_align(uds->total_progress_label, LV_ALIGN_TOP_LEFT, 10, 10);
    
    uds->total_progress_bar = lv_bar_create(progress_cont);
    lv_obj_set_size(uds->total_progress_bar, LV_PCT(90), 20);
    lv_obj_align(uds->total_progress_bar, LV_ALIGN_TOP_LEFT, 10, 35);
    lv_bar_set_value(uds->total_progress_bar, 0, LV_ANIM_OFF);
    
    // Segment progress
    uds->segment_progress_label = lv_label_create(progress_cont);
    lv_label_set_text(uds->segment_progress_label, "");
    lv_obj_set_style_text_font(uds->segment_progress_label, ui_common_get_font(), 0);
    lv_obj_align(uds->segment_progress_label, LV_ALIGN_TOP_LEFT, 10, 60);
    
    uds->segment_progress_bar = lv_bar_create(progress_cont);
    lv_obj_set_size(uds->segment_progress_bar, LV_PCT(90), 20);
    lv_obj_align(uds->segment_progress_bar, LV_ALIGN_TOP_LEFT, 10, 85);
    lv_bar_set_value(uds->segment_progress_bar, 0, LV_ANIM_OFF);

    /* 仅显示Total进度，隐藏Segment进度 */
    lv_obj_add_flag(uds->segment_progress_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(uds->segment_progress_bar, LV_OBJ_FLAG_HIDDEN);
    
    /* 控制按钮 */
    lv_obj_t *control_cont = lv_obj_create(body);
    lv_obj_set_size(control_cont, LV_PCT(100), 60);
    lv_obj_set_flex_flow(control_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(control_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(control_cont, 0, 0);
    
    uds->start_btn = lv_btn_create(control_cont);
    lv_obj_set_size(uds->start_btn, 140, 40);
    lv_obj_add_event_cb(uds->start_btn, start_flash_btn_event_cb, LV_EVENT_CLICKED, uds);
    lv_obj_t *start_label = lv_label_create(uds->start_btn);
    lv_label_set_text(start_label, LV_SYMBOL_PLAY " 开始刷写");
    lv_obj_set_style_text_font(start_label, ui_common_get_font(), 0);
    lv_obj_center(start_label);
    
    uds->stop_btn = lv_btn_create(control_cont);
    lv_obj_set_size(uds->stop_btn, 140, 40);
    lv_obj_add_event_cb(uds->stop_btn, stop_flash_btn_event_cb, LV_EVENT_CLICKED, uds);
    lv_obj_t *stop_label = lv_label_create(uds->stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP " 停止");
    lv_obj_set_style_text_font(stop_label, ui_common_get_font(), 0);
    lv_obj_center(stop_label);
    
    /* 日志显示区域 */
    uds->log_list = lv_list_create(body);
    lv_obj_set_size(uds->log_list, LV_PCT(100), 260);
    lv_obj_set_style_bg_color(uds->log_list, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(uds->log_list, LV_SCROLLBAR_MODE_OFF);
    
    /* 软键盘 */
    uds->kb_cont = lv_obj_create(uds->screen);
    lv_disp_t *disp = lv_disp_get_default();
    int32_t ver_res = disp ? lv_disp_get_ver_res(disp) : 600;
    int32_t kb_h = (ver_res * 45) / 100;
    if (kb_h < 240) kb_h = 240;
    if (kb_h > 340) kb_h = 340;
    lv_obj_set_size(uds->kb_cont, LV_PCT(100), kb_h);
    lv_obj_align(uds->kb_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(uds->kb_cont, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_style_border_width(uds->kb_cont, 0, 0);
    lv_obj_add_flag(uds->kb_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(uds->kb_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(uds->kb_cont, LV_FLEX_FLOW_COLUMN);

    uds->kb_preview = lv_textarea_create(uds->kb_cont);
    lv_obj_set_size(uds->kb_preview, LV_PCT(100), 36);
    lv_textarea_set_one_line(uds->kb_preview, true);
    lv_textarea_set_placeholder_text(uds->kb_preview, "当前输入...");
    lv_obj_clear_flag(uds->kb_preview, LV_OBJ_FLAG_CLICKABLE);

    uds->kb_hide_btn = lv_btn_create(uds->kb_cont);
    lv_obj_set_size(uds->kb_hide_btn, 60, 32);
    lv_obj_align(uds->kb_hide_btn, LV_ALIGN_TOP_RIGHT, -6, 2);
    lv_obj_add_event_cb(uds->kb_hide_btn, kb_hide_btn_event_cb, LV_EVENT_CLICKED, uds);
    lv_obj_t *kb_hide_label = lv_label_create(uds->kb_hide_btn);
    lv_label_set_text(kb_hide_label, "隐藏");
    lv_obj_center(kb_hide_label);

    uds->kb = lv_keyboard_create(uds->kb_cont);
    lv_obj_set_size(uds->kb, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(uds->kb, 1);
    lv_obj_set_style_text_font(uds->kb, ui_common_get_font(), LV_PART_ITEMS);
    lv_obj_set_style_pad_all(uds->kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_row(uds->kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_column(uds->kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_min_height(uds->kb, 48, LV_PART_ITEMS);
    lv_obj_set_style_pad_gap(uds->kb, 6, LV_PART_MAIN);
    lv_keyboard_set_mode(uds->kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    // 同步当前波特率到UI下拉框
    uint32_t current_bitrate = can_handler_get_bitrate();
    if (current_bitrate > 0) {
        pthread_mutex_lock(&g_uds_sync_lock);
        uds_sync_init_defaults_locked();
        g_uds_sync.bitrate = current_bitrate;
        pthread_mutex_unlock(&g_uds_sync_lock);
        log_info("同步UDS页面CAN波特率: %u bps", current_bitrate);
    }

    uds_sync_apply_to_widgets(uds, true);
    log_info("UDS diagnostic UI created");
    return uds;
}

int ui_uds_remote_set_file(const char *path)
{
    int rc = -1;
    if (!path || !path[0]) return -1;

    pthread_mutex_lock(&g_uds_sync_lock);
    uds_sync_set_path_locked(path);
    pthread_mutex_unlock(&g_uds_sync_lock);

    rc = uds_set_file_path(path);
    if (rc == 0) {
        lv_async_call(uds_apply_snapshot_async_cb, NULL);
    }
    return rc;
}

void ui_uds_remote_set_params(const char *iface, uint32_t bitrate, uint32_t tx_id, uint32_t rx_id, uint32_t block_size)
{
    pthread_mutex_lock(&g_uds_sync_lock);
    uds_sync_set_params_locked(iface, bitrate, tx_id, rx_id, block_size);
    pthread_mutex_unlock(&g_uds_sync_lock);
    lv_async_call(uds_apply_snapshot_async_cb, NULL);
}

int ui_uds_remote_start(void)
{
    uds_sync_state_t state;
    uint32_t tx_id = DEFAULT_TX_ID;
    uint32_t rx_id = DEFAULT_RX_ID;

    uds_sync_copy(&state);
    if (state.tx_id[0]) tx_id = (uint32_t)strtoul(state.tx_id, NULL, 16);
    if (state.rx_id[0]) rx_id = (uint32_t)strtoul(state.rx_id, NULL, 16);
    return uds_start_with_values(g_active_uds,
                                 state.iface[0] ? state.iface : "can0",
                                 state.bitrate ? state.bitrate : 500000,
                                 tx_id,
                                 rx_id,
                                 state.block_size ? state.block_size : 256,
                                 state.s19_path);
}

void ui_uds_remote_stop(void)
{
    uds_stop();
    uds_deinit();
    pthread_mutex_lock(&g_uds_sync_lock);
    uds_sync_set_running_locked(false);
    uds_sync_append_log_locked("[STOP] UDS flashing stopped");
    pthread_mutex_unlock(&g_uds_sync_lock);
    lv_async_call(uds_apply_snapshot_async_cb, NULL);
}

void ui_uds_remote_clear_logs(void)
{
    pthread_mutex_lock(&g_uds_sync_lock);
    uds_sync_clear_logs_locked();
    pthread_mutex_unlock(&g_uds_sync_lock);
    lv_async_call(uds_apply_snapshot_async_cb, NULL);
}

static size_t json_append_escaped(char *buf, size_t size, size_t pos, const char *src)
{
    const unsigned char *p = (const unsigned char *)(src ? src : "");
    while (*p && pos + 2 < size) {
        if (*p == '\\' || *p == '"') {
            if (pos + 2 >= size) break;
            buf[pos++] = '\\';
            buf[pos++] = (char)*p;
        } else if (*p == '\n' || *p == '\r') {
            if (pos + 2 >= size) break;
            buf[pos++] = '\\';
            buf[pos++] = 'n';
        } else if (*p == '\t') {
            if (pos + 2 >= size) break;
            buf[pos++] = '\\';
            buf[pos++] = 't';
        } else if (*p >= 0x20) {
            buf[pos++] = (char)*p;
        }
        p++;
    }
    if (pos < size) buf[pos] = '\0';
    return pos;
}

int ui_uds_get_state_json(char *buf, size_t size)
{
    uds_sync_state_t state;
    int start = 0;
    int idx = 0;
    int i = 0;
    size_t pos = 0;
    int written = 0;

    if (!buf || size < 64) return -1;
    uds_sync_copy(&state);

    written = snprintf(buf + pos, size - pos,
                       "{\"running\":%s,\"percent\":%d,\"seg_index\":%d,\"seg_total\":%d,"
                       "\"iface\":\"%s\",\"bitrate\":%u,\"tx_id\":\"%s\",\"rx_id\":\"%s\","
                       "\"block_size\":%u,\"path\":\"",
                       state.running ? "true" : "false",
                       state.total_percent,
                       state.seg_index,
                       state.seg_total,
                       state.iface,
                       state.bitrate,
                       state.tx_id,
                       state.rx_id,
                       state.block_size);
    if (written < 0) return -1;
    pos += (size_t)written;
    if (pos >= size) {
        buf[size - 1] = '\0';
        return -1;
    }
    pos = json_append_escaped(buf, size, pos, state.s19_path);
    written = snprintf(buf + pos, size - pos, "\",\"can_status\":\"");
    if (written < 0) return -1;
    pos += (size_t)written;
    if (pos >= size) {
        buf[size - 1] = '\0';
        return -1;
    }
    pos = json_append_escaped(buf, size, pos, state.can_status);
    written = snprintf(buf + pos, size - pos, "\",\"logs\":[");
    if (written < 0) return -1;
    pos += (size_t)written;
    if (pos >= size) {
        buf[size - 1] = '\0';
        return -1;
    }

    start = (state.log_head - state.log_count + UDS_SYNC_LOG_LINES) % UDS_SYNC_LOG_LINES;
    for (i = 0; i < state.log_count; i++) {
        idx = (start + i) % UDS_SYNC_LOG_LINES;
        if (i > 0) {
            written = snprintf(buf + pos, size - pos, ",");
            if (written < 0) return -1;
            pos += (size_t)written;
        }
        if (pos >= size) break;
        written = snprintf(buf + pos, size - pos, "\"");
        if (written < 0) return -1;
        pos += (size_t)written;
        if (pos >= size) break;
        pos = json_append_escaped(buf, size, pos, state.logs[idx]);
        written = snprintf(buf + pos, size - pos, "\"");
        if (written < 0) return -1;
        pos += (size_t)written;
        if (pos >= size) break;
    }
    if (pos >= size) {
        buf[size - 1] = '\0';
        return -1;
    }
    written = snprintf(buf + pos, size - pos, "]}");
    if (written < 0) return -1;
    return 0;
}

void ui_uds_destroy(ui_uds_t *uds)
{
    if (!uds) return;
    
    log_info("开始销毁UDS诊断界面...");
    
    // 停止CAN（如果已初始化）
    if (can_handler_is_running()) {
        can_handler_stop();
        can_handler_deinit();
    }
    
    // 注意：不要手动删除screen，LVGL会在加载新屏幕时自动清理旧屏幕
    // 只需要清空指针和释放结构体内存
    if (g_active_uds == uds) {
        g_active_uds = NULL;
    }

    // 释放内存
    free(uds);
    
    log_info("UDS诊断界面已销毁");
}
