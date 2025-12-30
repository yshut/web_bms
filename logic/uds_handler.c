/**
 * @file uds_handler.c
 * @brief UDS flashing handler - Full implementation ported from QT version
 */

#include "uds_handler.h"
#include "can_handler.h"
#include "s19_parser.h"
#include "../utils/logger.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <stdio.h>
#include <stdint.h>

/* UDS Service IDs */
#define UDS_SID_SESSION_CONTROL     0x10
#define UDS_SID_ECU_RESET           0x11
#define UDS_SID_SECURITY_ACCESS     0x27
#define UDS_SID_ROUTINE_CONTROL     0x31
#define UDS_SID_REQUEST_DOWNLOAD    0x34
#define UDS_SID_TRANSFER_DATA       0x36
#define UDS_SID_TRANSFER_EXIT       0x37
#define UDS_SID_TESTER_PRESENT      0x3E

/* UDS Subfunctions */
#define UDS_SUB_PROGRAMMING_SESSION 0x02
#define UDS_SUB_REQUEST_SEED        0x05
#define UDS_SUB_SEND_KEY            0x06
#define UDS_SUB_START_ROUTINE       0x01
#define UDS_SUB_HARD_RESET          0x01

/* UDS Response Codes */
#define UDS_RESP_POSITIVE           0x40
#define UDS_RESP_NEGATIVE           0x7F
#define UDS_NRC_PENDING             0x78

/* Security Access Constants */
#define SEC_ECU_KEY                 0x548F08D1
#define SECURITY_SB1                ((((SEC_ECU_KEY & 0x000003FC) >> 2)))
#define SECURITY_SB2                ((((SEC_ECU_KEY & 0x7F800000) >> 23) ^ 0xA5))
#define SECURITY_SB3                ((((SEC_ECU_KEY & 0x001FE000) >> 13) ^ 0x5A))

typedef struct {
    uds_config_t cfg;
    pthread_t thread;
    bool running;
    uds_progress_cb progress_cb;
    void *progress_ud;
    uds_log_cb log_cb;
    void *log_ud;
    int socket_fd;
    char interface[16];
    uint16_t ecu_max_block_size;
} uds_ctx_t;

static uds_ctx_t g_uds = {0};
static bool second_block_cmd_sent = false; /* Flag to prevent duplicate second block command */

/* 远程控制：持久化保存 S19 路径（避免 ws_command_handler 释放后悬空指针） */
static char g_s19_path_buf[512] = {0};
/* 远程控制：持久化保存 UDS 参数（便于网页端同步/配置） */
static char g_uds_iface_buf[16] = "can0";
static uint32_t g_uds_tx_id = 0x7F3;
static uint32_t g_uds_rx_id = 0x7FB;
static uint32_t g_uds_blk_size = 256;

/* 日志级别定义 */
typedef enum {
    UDS_LOG_ERROR = 0,    /* 只记录错误 */
    UDS_LOG_PROGRESS = 1, /* 记录进度信息 */
    UDS_LOG_INFO = 2,     /* 记录重要信息 */
    UDS_LOG_DEBUG = 3     /* 记录所有调试信息 */
} uds_log_level_t;

/* 当前日志级别（默认只显示错误和进度） */
static uds_log_level_t g_log_level = UDS_LOG_PROGRESS;

/* Thread-safe logging with async callback and level filtering */
static void uds_log_ex(uds_log_level_t level, const char *msg)
{
    /* 只输出符合级别的日志 */
    if (level > g_log_level) return;
    
    /* Always log to system first */
    log_info("[UDS] %s", msg);
    
    /* Call UI callback if registered (needs to be thread-safe on UI side) */
    if (g_uds.log_cb) {
        g_uds.log_cb(msg, g_uds.log_ud);
    }
}

/* 便捷宏定义 */
#define uds_log_error(msg)    uds_log_ex(UDS_LOG_ERROR, msg)
#define uds_log_progress(msg) uds_log_ex(UDS_LOG_PROGRESS, msg)
#define uds_log_info(msg)     uds_log_ex(UDS_LOG_INFO, msg)
#define uds_log_debug(msg)    uds_log_ex(UDS_LOG_DEBUG, msg)

/* Calculate security access key from seed */
static void calculate_key(const uint8_t *seed, uint8_t *key_out)
{
    uint32_t seed_val = ((uint32_t)seed[0] << 24) | ((uint32_t)seed[1] << 16) |
                        ((uint32_t)seed[2] << 8) | seed[3];
    
    uint32_t w_last_seed = seed_val;
    uint32_t w_temp = 0;
    
    /* Calculate iteration count */
    int selector = (((SEC_ECU_KEY & 0x00000800) >> 10) | ((SEC_ECU_KEY & 0x00200000) >> 21));
    if (selector == 0) w_temp = ((seed_val & 0xff000000) >> 24);
    else if (selector == 1) w_temp = ((seed_val & 0x00ff0000) >> 16);
    else if (selector == 2) w_temp = ((seed_val & 0x0000ff00) >> 8);
    else w_temp = (seed_val & 0x000000ff);
    
    uint32_t iterations = (((w_temp ^ SECURITY_SB1) & SECURITY_SB2) + SECURITY_SB3);
    
    /* Perform iterations */
    for (uint32_t j = 0; j < iterations; j++) {
        uint32_t bit_calc = ((w_last_seed & 0x40000000) >> 30) ^
                            ((w_last_seed & 0x01000000) >> 24) ^
                            ((w_last_seed & 0x1000) >> 12) ^
                            ((w_last_seed & 0x04) >> 2);
        w_last_seed = (w_last_seed << 1) | bit_calc;
    }
    
    /* Byte reordering */
    if ((SEC_ECU_KEY & 0x00000001) == 0x00000001) {
        w_temp = ((w_last_seed & 0x00FF0000) >> 16) |
                 ((w_last_seed & 0xFF000000) >> 8) |
                 ((w_last_seed & 0x000000FF) << 8) |
                 ((w_last_seed & 0x0000FF00) << 16);
    } else {
        w_temp = w_last_seed;
    }
    
    /* Calculate final key */
    uint32_t key_val = w_temp ^ SEC_ECU_KEY;
    
    key_out[0] = (key_val >> 24) & 0xFF;
    key_out[1] = (key_val >> 16) & 0xFF;
    key_out[2] = (key_val >> 8) & 0xFF;
    key_out[3] = key_val & 0xFF;
}

/* Send single CAN frame */
static int uds_send_frame(int sock, uint32_t can_id, const uint8_t *data, uint8_t dlc)
{
    struct can_frame f;
    memset(&f, 0, sizeof(f));
    f.can_id = can_id & CAN_SFF_MASK;
    f.can_dlc = dlc;
    /* Reduced debug logging for better performance */
    if (dlc > 0 && data) memcpy(f.data, data, dlc);
    
    /* Only log critical frames to reduce overhead */
    /* Uncomment for debugging: */
    /*
    char debug_msg[128];
    snprintf(debug_msg, sizeof(debug_msg),
            "[TX RAW] ID=0x%03X DLC=%d Data=[%02X %02X %02X %02X %02X %02X %02X %02X]",
            can_id & CAN_SFF_MASK, dlc,
            f.data[0], f.data[1], f.data[2], f.data[3],
            f.data[4], f.data[5], f.data[6], f.data[7]);
    uds_log(debug_msg);
    */
    
    int n = write(sock, &f, sizeof(f));
    if (n != (int)sizeof(f)) {
        char err_msg[64];
        snprintf(err_msg, sizeof(err_msg), "[ERROR] Failed to send frame: %s", strerror(errno));
        uds_log_error(err_msg);
        return -1;
    }
    return 0;
}

/* Peek a CAN frame without consuming it (returns 0 if a full frame is available) */
static int can_peek_frame(int sock, struct can_frame *out, int timeout_ms)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int r = select(sock + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0) return -1;

    int n = recv(sock, out, sizeof(*out), MSG_PEEK);
    return (n == (int)sizeof(*out)) ? 0 : -1;
}

/* Wait for CAN response with timeout, filtering by RX ID, with ISO-TP support */
static int uds_wait_resp(int sock, struct can_frame *out, int timeout_ms)
{
    /* Reduced debug logging for performance optimization */
    
    int start_time = timeout_ms;
    static uint8_t isotp_buffer[4096]; /* Buffer for multi-frame response */
    int isotp_offset = 0;
    int isotp_total = 0;
    int isotp_seq_expected = 1;
    bool isotp_mode = false;
    
    while (start_time > 0) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        
        struct timeval tv;
        /* Dynamic wait slice to honor caller timeout; small slices for responsiveness */
        int slice_ms = (start_time > 10) ? 10 : start_time; /* up to 10ms per iteration */
        tv.tv_sec = slice_ms / 1000;
        tv.tv_usec = (slice_ms % 1000) * 1000;
        
        int r = select(sock + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        
        if (r > 0) {
            struct can_frame frame;
            int n = read(sock, &frame, sizeof(frame));
            if (n == (int)sizeof(frame)) {
                uint32_t rx_id = frame.can_id & CAN_SFF_MASK;
                
                /* Check if this is the response we're waiting for (matches RX ID) */
                if (rx_id == g_uds.cfg.rx_id) {
                    uint8_t pci = frame.data[0] >> 4;
                    
                    /* Single Frame (SF) */
                    if (pci == 0) {
                        memcpy(out, &frame, sizeof(frame));
                        /* Reduced logging for performance - only log important frames */
                        
                        /* Check for specific response [04 74 20 07 FA 00 00 00] and send second data block command */
                        if (!second_block_cmd_sent && frame.can_dlc == 8 && 
                            frame.data[0] == 0x04 && frame.data[1] == 0x74 && 
                            frame.data[2] == 0x20 && frame.data[3] == 0x07 && 
                            frame.data[4] == 0xFA && frame.data[5] == 0x00 && 
                            frame.data[6] == 0x00 && frame.data[7] == 0x00) {
                            
                            uds_log_debug("[调试] 检测到特定响应，立即发送第二数据块命令");
                            second_block_cmd_sent = true; /* Mark as sent to prevent duplicates */
                            
                            /* Send second data block command: 17 FA 36 01 */
                            uint8_t second_block_cmd[4] = {0x17, 0xFA, 0x36, 0x01};
                            struct can_frame cmd_frame;
                            memset(&cmd_frame, 0, sizeof(cmd_frame));
                            cmd_frame.can_id = g_uds.cfg.tx_id & CAN_SFF_MASK;
                            cmd_frame.can_dlc = 4;
                            memcpy(cmd_frame.data, second_block_cmd, 4);
                            
                            /* Send directly through socket */
                            int n = write(sock, &cmd_frame, sizeof(cmd_frame));
                            if (n == (int)sizeof(cmd_frame)) {
                                char tx_msg[128];
                                snprintf(tx_msg, sizeof(tx_msg),
                                        "[TX RAW] ID=0x%03X DLC=%d Data=[%02X %02X %02X %02X %02X %02X %02X %02X]",
                                        g_uds.cfg.tx_id & CAN_SFF_MASK, 4,
                                        second_block_cmd[0], second_block_cmd[1], second_block_cmd[2], second_block_cmd[3],
                                        0x00, 0x00, 0x00, 0x00);
                                uds_log_debug(tx_msg);
                                uds_log_debug("[调试] 第二数据块命令发送成功");
                            } else {
                                uds_log_error("[错误] 发送第二数据块命令失败");
                            }
                        }
                        
                        return 0;
                    }
                    /* First Frame (FF) */
                    else if (pci == 1) {
                        isotp_mode = true;
                        isotp_total = ((frame.data[0] & 0x0F) << 8) | frame.data[1];
                        isotp_offset = 0;
                        /* Copy first 6 bytes (from index 2-7) */
                        int first_bytes = (isotp_total >= 6) ? 6 : isotp_total;
                        memcpy(isotp_buffer, &frame.data[2], first_bytes);
                        isotp_offset = first_bytes;
                        isotp_seq_expected = 1;
                        
                        /* Send Flow Control (CTS - Continue To Send) */
                        uint8_t fc[8] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                        uds_send_frame(sock, g_uds.cfg.tx_id, fc, 8);
                        
                        /* Reduced logging for performance */
                    }
                    /* Consecutive Frame (CF) */
                    else if (pci == 2 && isotp_mode) {
                        int seq = frame.data[0] & 0x0F;
                        if (seq == isotp_seq_expected) {
                            int bytes_to_copy = (isotp_total - isotp_offset >= 7) ? 
                                               7 : (isotp_total - isotp_offset);
                            memcpy(&isotp_buffer[isotp_offset], &frame.data[1], bytes_to_copy);
                            isotp_offset += bytes_to_copy;
                            isotp_seq_expected = (isotp_seq_expected + 1) & 0x0F;
                            
                            /* If complete, reconstruct the frame */
                            if (isotp_offset >= isotp_total) {
                                /* Put reassembled data into output frame */
                                memset(out, 0, sizeof(*out));
                                out->can_id = rx_id;
                                out->can_dlc = (isotp_total <= 8) ? isotp_total : 8;
                                memcpy(out->data, isotp_buffer, out->can_dlc);
                                
                                /* Reduced logging for performance */
                                
                                isotp_mode = false;
                                return 0;
                            }
                        } else {
                            uds_log_debug("[调试] ISO-TP序列不匹配");
                            isotp_mode = false;
                        }
                    }
                    /* Flow Control (FC) */
                    else if (pci == 3) {
                        /* Return FC frame to caller (e.g., send_isotp_multi) */
                        memcpy(out, &frame, sizeof(frame));
                        /* Reduced logging for performance */
                        return 0;
                    }
                }
                /* Otherwise, ignore (likely loopback of our TX) */
            }
        }
        
        start_time -= slice_ms;
    }
    
    if (isotp_mode) {
        uds_log_error("[错误] ISO-TP多帧不完整");
    }
    
    return -1; /* Timeout */
}

/* Send ISO-TP single frame (SF) */
static int send_isotp_sf(int sock, uint32_t tx_id, uint8_t sid, const uint8_t *data, uint8_t data_len)
{
    /* 单帧总长度(含SID)最多7字节 */
    if (data_len + 1 > 7) {
        uds_log_error("[错误] ISO-TP单帧负载过长 (len>7)");
        return -1;
    }
    uint8_t frame[8] = {0};
    frame[0] = data_len + 1; /* PCI: SF with length */
    frame[1] = sid;
    if (data && data_len > 0) {
        memcpy(&frame[2], data, data_len > 6 ? 6 : data_len);
    }
    return uds_send_frame(sock, tx_id, frame, 8);
}

/* Send ISO-TP first frame (FF) and consecutive frames (CF) */
static int send_isotp_multi(int sock, uint32_t tx_id, uint8_t sid, const uint8_t *payload, uint16_t payload_len)
{
    uint16_t total_len = payload_len + 1; /* +1 for SID */
    
    /* Send First Frame */
    uint8_t ff[8];
    ff[0] = 0x10 | ((total_len >> 8) & 0x0F);
    ff[1] = total_len & 0xFF;
    ff[2] = sid;
    int first_data_bytes = (payload_len >= 5) ? 5 : payload_len;
    memcpy(&ff[3], payload, first_data_bytes);
    if (first_data_bytes < 5) {
        memset(&ff[3 + first_data_bytes], 0, 5 - first_data_bytes);
    }
    
    if (uds_send_frame(sock, tx_id, ff, 8) != 0) {
        return -1;
    }
    
    /* Wait for Flow Control (FC)
     * - 忽略非FC帧(可能是缓冲区中的旧响应)
     * - 优化等待时间和轮询间隔以提升速度
     */
    struct can_frame fc_frame;
    int waited_ms = 0;
    while (waited_ms < 1500) {
        if (uds_wait_resp(sock, &fc_frame, 50) == 0) {
            uint8_t pci = (fc_frame.data[0] & 0xF0);
            if (pci == 0x30) {
                char dbg[64];
                snprintf(dbg, sizeof(dbg), "[调试] 收到FC: BS=%u ST=%u", fc_frame.data[1], fc_frame.data[2]);
                uds_log_debug(dbg);
                break;
            } else {
                /* Reduced logging for better performance - only log on first occurrence */
                if (waited_ms == 0) {
                    char dbg[96];
                    snprintf(dbg, sizeof(dbg),
                            "[调试] 等待时忽略非FC: [%02X %02X %02X %02X %02X %02X %02X %02X]",
                            fc_frame.data[0], fc_frame.data[1], fc_frame.data[2], fc_frame.data[3],
                            fc_frame.data[4], fc_frame.data[5], fc_frame.data[6], fc_frame.data[7]);
                    uds_log_debug(dbg);
                }
            }
        }
        waited_ms += 50;
    }
    if (waited_ms >= 1500) {
        uds_log_error("[错误] 未收到FC");
        return -1;
    }
    
    /* Parse FC parameters */
    uint8_t fc_status = fc_frame.data[0] & 0x0F; /* 0: CTS, 1: Wait, 2: Overflow */
    uint8_t block_size = fc_frame.data[1];       /* 0: unlimited */
    uint8_t stmin = fc_frame.data[2];            /* 0..0x7F: ms, 0xF1..0xF9: 100us units */

    /* If receiver asks to WAIT, keep waiting until CTS */
    while (fc_status == 0x01) {
        if (uds_wait_resp(sock, &fc_frame, 500) != 0) return -1;
        if ((fc_frame.data[0] & 0xF0) != 0x30) continue; /* not FC, ignore */
        fc_status = fc_frame.data[0] & 0x0F;
        block_size = fc_frame.data[1];
        stmin = fc_frame.data[2];
    }
    if (fc_status == 0x02) {
        uds_log_error("[错误] FC: 接收器溢出");
        return -1;
    }

    /* Send Consecutive Frames honoring BS and STmin */
    int offset = first_data_bytes;
    int seq_num = 1;
    uint32_t st_delay_us = 0;
    if (stmin <= 0x7F) st_delay_us = (uint32_t)stmin * 1000U;
    else if (stmin >= 0xF1 && stmin <= 0xF9) st_delay_us = (uint32_t)(stmin & 0x0F) * 100U;
    else st_delay_us = 0; /* treat others as 0 */

    uint32_t bs_counter = 0;
    
    while (offset < payload_len) {
        uint8_t cf[8];
        cf[0] = 0x20 | (seq_num & 0x0F);
        
        int bytes_to_send = (payload_len - offset >= 7) ? 7 : (payload_len - offset);
        memcpy(&cf[1], &payload[offset], bytes_to_send);
        
        if (bytes_to_send < 7) {
            memset(&cf[1 + bytes_to_send], 0, 7 - bytes_to_send);
        }
        
        if (uds_send_frame(sock, tx_id, cf, 8) != 0) {
            return -1;
        }
        
        offset += bytes_to_send;
        seq_num = (seq_num + 1) & 0x0F;
        bs_counter++;

        /* 在发送间隙快速监听ECU控制帧(FC/Pending)，以适配某些ECU在BS=0时中途下发FC或Pending的行为 */
        /* 优化：减少等待时间，快速响应 */
        {
            struct can_frame ctrl;
            if (uds_wait_resp(sock, &ctrl, 1) == 0) {
                /* Check if this is a non-control frame (like 0x74 response) */
                if (ctrl.can_dlc >= 2 && ctrl.data[1] == (UDS_SID_REQUEST_DOWNLOAD + UDS_RESP_POSITIVE)) {
                    /* Reduced logging - this is expected behavior */
                    /* The specific response detection was already handled in uds_wait_resp if needed */
                    /* This is normal behavior - the 0x74 response was processed where it should be */
                }
                /* Flow Control */
                else if ((ctrl.data[0] & 0xF0) == 0x30) {
                    uint8_t fs = ctrl.data[0] & 0x0F;
                    if (fs == 0x02) { uds_log_error("[错误] FC: 接收器溢出"); return -1; }
                    if (fs == 0x01) {
                        /* WAIT: 暂停直至收到CTS */
                        int waited = 0;
                        while (waited < 3000) {
                            struct can_frame ctl2;
                            if (uds_wait_resp(sock, &ctl2, 250) == 0 && (ctl2.data[0] & 0xF0) == 0x30) {
                                uint8_t fs2 = ctl2.data[0] & 0x0F;
                                if (fs2 == 0x00) {
                                    block_size = ctl2.data[1];
                                    stmin = ctl2.data[2];
                                    if (stmin <= 0x7F) st_delay_us = (uint32_t)stmin * 1000U;
                                    else if (stmin >= 0xF1 && stmin <= 0xF9) st_delay_us = (uint32_t)(stmin & 0x0F) * 100U;
                                    bs_counter = 0;
                                    break;
                                } else                                 if (fs2 == 0x02) {
                                    uds_log_error("[错误] FC: 接收器溢出");
                                    return -1;
                                }
                            }
                            waited += 250;
                        }
                    } else if (fs == 0x00) {
                        /* CTS: 更新BS/ST */
                        block_size = ctrl.data[1];
                        stmin = ctrl.data[2];
                        if (stmin <= 0x7F) st_delay_us = (uint32_t)stmin * 1000U;
                        else if (stmin >= 0xF1 && stmin <= 0xF9) st_delay_us = (uint32_t)(stmin & 0x0F) * 100U;
                        bs_counter = 0;
                    }
                }
                /* Pending负响应：忽略，继续发送CF直至块结束 */
                else if (ctrl.can_dlc >= 4 && ctrl.data[1] == UDS_RESP_NEGATIVE &&
                         ctrl.data[2] == UDS_SID_TRANSFER_DATA && ctrl.data[3] == UDS_NRC_PENDING) {
                    /* ignore */
                }
            }
        }

    /* obey STmin - optimized for faster transfer */
    /* 减少STmin延迟以提升速度，但仍然遵守协议要求 */
    if (st_delay_us > 0) {
        /* 如果STmin小于1ms，考虑不延迟以获得最大速度 */
        if (st_delay_us >= 500) { /* 仅在STmin >= 0.5ms时才延迟 */
            usleep(st_delay_us);
        }
    }

        /* obey Block Size: if non-zero, must pause for next FC after 'block_size' CFs */
        if (block_size != 0 && bs_counter >= block_size && offset < payload_len) {
            /* Wait next FC */
            int waited_ms2 = 0;
            while (waited_ms2 < 1500) {
                struct can_frame next_fc;
                if (uds_wait_resp(sock, &next_fc, 50) == 0) { /* Reduced from 250ms to 50ms */
                    if ((next_fc.data[0] & 0xF0) == 0x30) {
                        uint8_t fs = next_fc.data[0] & 0x0F;
                        if (fs == 0x02) { uds_log_error("[错误] FC: 接收器溢出"); return -1; }
                        if (fs == 0x01) { /* WAIT */ waited_ms2 += 0; continue; }
                        /* CTS */
                        block_size = next_fc.data[1];
                        stmin = next_fc.data[2];
                        if (stmin <= 0x7F) st_delay_us = (uint32_t)stmin * 1000U;
                        else if (stmin >= 0xF1 && stmin <= 0xF9) st_delay_us = (uint32_t)(stmin & 0x0F) * 100U;
                        else st_delay_us = 0;
                        bs_counter = 0;
                        break;
                    }
                }
                waited_ms2 += 50; /* Reduced from 250ms to 50ms */
            }
            if (waited_ms2 >= 1500) { uds_log_error("[错误] 未收到FC (分段发送期间)"); return -1; }
        }
    }
    
    return 0;
}

/* Main UDS flashing thread */
static void* uds_thread(void *arg)
{
    (void)arg;
    second_block_cmd_sent = false; /* Reset flag for new UDS session */
    uds_log_progress("刷写开始...");
    
    /* Create CAN socket */
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        uds_log_error("[错误] 创建CAN socket失败");
        g_uds.running = false;
        return NULL;
    }
    
    /* Disable receive-own-messages to reduce loopback processing overhead */
    int opt = 0;
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &opt, sizeof(opt));
    
    /* Bind to CAN interface */
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, g_uds.interface, sizeof(ifr.ifr_name) - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        uds_log_error("[错误] 获取接口索引失败");
        close(s);
        g_uds.running = false;
        return NULL;
    }
    
    struct sockaddr_can addr = {0};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        uds_log_error("[错误] 绑定CAN socket失败");
        close(s);
        g_uds.running = false;
        return NULL;
    }
    
    /* Set filter for RX ID */
    struct can_filter rfilter;
    rfilter.can_id = g_uds.cfg.rx_id & CAN_SFF_MASK;
    rfilter.can_mask = CAN_SFF_MASK;
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));
    
    g_uds.socket_fd = s;
    
    /* ==== Step 1: Enter Programming Session (10 02) ==== */
    uds_log_info("[TX] 进入编程会话");
    uint8_t prog_req[] = {UDS_SUB_PROGRAMMING_SESSION};
    if (send_isotp_sf(s, g_uds.cfg.tx_id, UDS_SID_SESSION_CONTROL, prog_req, 1) != 0) {
        uds_log_error("[错误] 发送编程会话请求失败");
        goto done;
    }
    
    struct can_frame resp;
    if (uds_wait_resp(s, &resp, 1000) != 0 || resp.can_dlc < 3 ||
        resp.data[1] != (UDS_SID_SESSION_CONTROL + UDS_RESP_POSITIVE) ||
        resp.data[2] != UDS_SUB_PROGRAMMING_SESSION) {
        uds_log_error("[错误] 编程会话响应无效");
        goto done;
    }
    uds_log_info("[RX] 编程会话已进入");
    if (g_uds.progress_cb) g_uds.progress_cb(10, 1, 6, g_uds.progress_ud);
    
    /* ==== Step 2: Security Access - Request Seed (27 05) ==== */
    uds_log_info("[TX] 请求安全种子");
    uint8_t seed_req[] = {UDS_SUB_REQUEST_SEED};
    if (send_isotp_sf(s, g_uds.cfg.tx_id, UDS_SID_SECURITY_ACCESS, seed_req, 1) != 0) {
        uds_log_error("[错误] 发送种子请求失败");
        goto done;
    }
    
    /* Wait for seed response, handle pending */
    int pending_count = 0;
    while (pending_count < 10) {
        if (uds_wait_resp(s, &resp, 1000) == 0) {
            if (resp.can_dlc >= 4 && resp.data[1] == UDS_RESP_NEGATIVE &&
                resp.data[2] == UDS_SID_SECURITY_ACCESS && resp.data[3] == UDS_NRC_PENDING) {
                uds_log_debug("[调试] 种子请求等待中...");
                pending_count++;
                continue;
            }
            break;
        }
        uds_log_error("[错误] 种子响应超时");
        goto done;
    }
    
    if (resp.can_dlc < 7 || resp.data[1] != (UDS_SID_SECURITY_ACCESS + UDS_RESP_POSITIVE) ||
        resp.data[2] != UDS_SUB_REQUEST_SEED) {
        uds_log_error("[错误] 种子响应无效");
        goto done;
    }
    
    uint8_t seed[4];
    memcpy(seed, &resp.data[3], 4);
    uds_log_info("[RX] 种子已接收");
    
    /* Check if already unlocked (seed = 0) */
    if (seed[0] == 0 && seed[1] == 0 && seed[2] == 0 && seed[3] == 0) {
        uds_log_info("[信息] ECU已解锁");
    } else {
        /* ==== Step 3: Security Access - Send Key (27 06) ==== */
        uint8_t key[4];
        calculate_key(seed, key);
        
        char key_str[32];
        snprintf(key_str, sizeof(key_str), "[TX] 发送密钥 KEY=%02X%02X%02X%02X",
                 key[0], key[1], key[2], key[3]);
        uds_log_info(key_str);
        
        uint8_t key_req[5];
        key_req[0] = UDS_SUB_SEND_KEY;
        memcpy(&key_req[1], key, 4);
        
        if (send_isotp_sf(s, g_uds.cfg.tx_id, UDS_SID_SECURITY_ACCESS, key_req, 5) != 0) {
            uds_log_error("[错误] 发送密钥失败");
            goto done;
        }
        
        /* Wait for key response */
        pending_count = 0;
        while (pending_count < 10) {
            if (uds_wait_resp(s, &resp, 1000) == 0) {
                if (resp.can_dlc >= 4 && resp.data[1] == UDS_RESP_NEGATIVE &&
                    resp.data[2] == UDS_SID_SECURITY_ACCESS && resp.data[3] == UDS_NRC_PENDING) {
                    pending_count++;
                    continue;
                }
                break;
            }
            uds_log_error("[错误] 密钥响应超时");
            goto done;
        }
        
        if (resp.can_dlc < 3 || resp.data[1] != (UDS_SID_SECURITY_ACCESS + UDS_RESP_POSITIVE) ||
            resp.data[2] != UDS_SUB_SEND_KEY) {
            uds_log_error("[错误] 密钥响应无效 - 访问被拒绝");
            goto done;
        }
        uds_log_info("[RX] 安全访问已授权");
    }
    
    if (g_uds.progress_cb) g_uds.progress_cb(20, 2, 6, g_uds.progress_ud);
    
    /* ==== Step 4: Parse S19 File ==== */
    uds_log_progress("解析S19文件...");
    s19_file_t *s19 = s19_parse(g_uds.cfg.s19_path);
    if (!s19 || s19->segment_count == 0) {
        uds_log_error("[错误] S19文件解析失败");
        if (s19) s19_free(s19);
        goto done;
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "[信息] S19: %d个分段, 共%u字节",
             s19->segment_count, s19->total_bytes);
    uds_log_info(msg);
    
    /* ==== Step 5: Process each segment ==== */
    uint32_t total_processed = 0;
    
    for (int seg_idx = 0; seg_idx < s19->segment_count; seg_idx++) {
        if (!g_uds.running) {
            uds_log_info("[信息] 用户停止刷写");
            s19_free(s19);
            goto done;
        }
        
        s19_segment_t *seg = &s19->segments[seg_idx];
        
        snprintf(msg, sizeof(msg), "刷写分段 %d/%d (0x%08X, %u字节)",
                 seg_idx + 1, s19->segment_count, seg->address, seg->size);
        uds_log_progress(msg);
        
        /* Request Download for this segment */
        uds_log_info("[TX] 请求下载");
        uint8_t req_dl[11];
        req_dl[0] = 0x00; /* No compression/encryption */
        req_dl[1] = 0x44; /* Address and size format (4 bytes each) */
        req_dl[2] = (seg->address >> 24) & 0xFF;
        req_dl[3] = (seg->address >> 16) & 0xFF;
        req_dl[4] = (seg->address >> 8) & 0xFF;
        req_dl[5] = seg->address & 0xFF;
        req_dl[6] = (seg->size >> 24) & 0xFF;
        req_dl[7] = (seg->size >> 16) & 0xFF;
        req_dl[8] = (seg->size >> 8) & 0xFF;
        req_dl[9] = seg->size & 0xFF;
        
        /* 总长度10(+SID)=11字节，必须用多帧发送 */
        if (send_isotp_multi(s, g_uds.cfg.tx_id, UDS_SID_REQUEST_DOWNLOAD, req_dl, 10) != 0) {
            uds_log_error("[错误] 发送下载请求失败");
            s19_free(s19);
            goto done;
        }
        
        /* Wait for download response (handle early 0x74 and Pending) */
        {
            bool got74 = false;
            
            /* Check if specific response was already processed */
            if (second_block_cmd_sent) {
                uds_log_debug("[调试] 特定响应已处理，模拟0x74响应");
                /* Create a simulated response for upper layer processing */
                memset(&resp, 0, sizeof(resp));
                resp.can_dlc = 8;
                resp.data[0] = 0x04;
                resp.data[1] = 0x74;
                resp.data[2] = 0x20;
                resp.data[3] = 0x07;
                resp.data[4] = 0xFA;
                resp.data[5] = 0x00;
                resp.data[6] = 0x00;
                resp.data[7] = 0x00;
                got74 = true;
            } else {
                /* Peek early 0x74 already queued by ECU */
                struct can_frame peek;
                if (can_peek_frame(s, &peek, 50) == 0) {
                    char peek_msg[128];
                    snprintf(peek_msg, sizeof(peek_msg),
                            "[调试] 预览帧: ID=0x%03X DLC=%d Data=[%02X %02X %02X %02X %02X %02X %02X %02X]",
                            peek.can_id & CAN_SFF_MASK, peek.can_dlc,
                            peek.data[0], peek.data[1], peek.data[2], peek.data[3],
                            peek.data[4], peek.data[5], peek.data[6], peek.data[7]);
                    uds_log_debug(peek_msg);
                    
                    if ((peek.can_id & CAN_SFF_MASK) == (g_uds.cfg.rx_id & CAN_SFF_MASK) &&
                        peek.can_dlc >= 2 && peek.data[1] == (UDS_SID_REQUEST_DOWNLOAD + UDS_RESP_POSITIVE)) {
                        read(s, &resp, sizeof(resp));
                        got74 = true;
                        uds_log_debug("[调试] 预览检测到0x74响应");
                    }
                } else {
                    uds_log_debug("[调试] 无帧可预览");
                }

                if (!got74) {
                    int waited_ms = 0;
                    uds_log_debug("[调试] 开始等待0x74响应循环");
                    while (waited_ms < 5000) {
                        if (uds_wait_resp(s, &resp, 400) == 0) {
                            char wait_msg[128];
                            snprintf(wait_msg, sizeof(wait_msg),
                                    "[调试] 等待收到: DLC=%d Data=[%02X %02X %02X %02X %02X %02X %02X %02X]",
                                    resp.can_dlc,
                                    resp.data[0], resp.data[1], resp.data[2], resp.data[3],
                                    resp.data[4], resp.data[5], resp.data[6], resp.data[7]);
                            uds_log_debug(wait_msg);
                            
                            if (resp.can_dlc >= 4 && resp.data[1] == UDS_RESP_NEGATIVE &&
                                resp.data[2] == UDS_SID_REQUEST_DOWNLOAD && resp.data[3] == UDS_NRC_PENDING) {
                                uds_log_debug("[调试] 收到pending响应，继续等待");
                                continue; /* keep waiting */
                            }
                            if (resp.can_dlc >= 2 && resp.data[1] == (UDS_SID_REQUEST_DOWNLOAD + UDS_RESP_POSITIVE)) {
                                got74 = true;
                                uds_log_debug("[调试] 等待检测到0x74响应");
                                break;
                            }
                            uds_log_debug("[调试] 忽略无关帧");
                            /* Ignore unrelated frames */
                        }
                        waited_ms += 400;
                        
                        /* Check again if specific response was processed during wait */
                        if (second_block_cmd_sent) {
                            uds_log_debug("[调试] 等待期间处理了特定响应，停止等待循环");
                            /* Create a simulated response */
                            memset(&resp, 0, sizeof(resp));
                            resp.can_dlc = 8;
                            resp.data[0] = 0x04;
                            resp.data[1] = 0x74;
                            resp.data[2] = 0x20;
                            resp.data[3] = 0x07;
                            resp.data[4] = 0xFA;
                            resp.data[5] = 0x00;
                            resp.data[6] = 0x00;
                            resp.data[7] = 0x00;
                            got74 = true;
                            break;
                        }
                    }
                }
            }

            if (!got74) {
                uds_log_error("[错误] 下载请求失败或超时");
                s19_free(s19);
                goto done;
            }
        }
        
        /* Parse ECU max block size */
        if (resp.can_dlc >= 5 && resp.data[2] == 0x20) {
            g_uds.ecu_max_block_size = ((uint16_t)resp.data[3] << 8) | resp.data[4];
        }
        uds_log_info("[RX] 下载已接受");
        
        
        /* Transfer Data (36) */
        uint32_t offset = 0;
        uint8_t block_counter = 1;
        uint16_t block_size = g_uds.ecu_max_block_size - 2; /* -2 for SID and counter */
        uint32_t total_blocks = (seg->size + block_size - 1) / block_size;
        
        char seg_msg[128];
        snprintf(seg_msg, sizeof(seg_msg), "[信息] 传输%u个数据块 (块大小=%u)",
                 total_blocks, block_size);
        uds_log_info(seg_msg);
        
        while (offset < seg->size) {
            if (!g_uds.running) {
                uds_log_info("[信息] 用户停止刷写");
                s19_free(s19);
                goto done;
            }
            
            uint16_t chunk_size = (seg->size - offset >= block_size) ? 
                                  block_size : (seg->size - offset);
            
            /* Build transfer data payload: [counter] [data...] */
            uint8_t *transfer_payload = malloc(chunk_size + 1);
            if (!transfer_payload) {
                uds_log_error("[错误] 内存分配失败");
                s19_free(s19);
                goto done;
            }
            
            transfer_payload[0] = block_counter;
            memcpy(&transfer_payload[1], &seg->data[offset], chunk_size);
            
            /* Send transfer data with retry mechanism */
            int send_retries = 0;
            int max_send_retries = 3; /* Allow 3 retry attempts */
            bool send_success = false;
            
            while (send_retries < max_send_retries && !send_success) {
                if (send_isotp_multi(s, g_uds.cfg.tx_id, UDS_SID_TRANSFER_DATA, 
                                    transfer_payload, chunk_size + 1) == 0) {
                    send_success = true;
                } else {
                    send_retries++;
                    if (send_retries < max_send_retries) {
                        char retry_msg[128];
                        snprintf(retry_msg, sizeof(retry_msg),
                                "[警告] 传输数据失败 (块 %d), 重试 %d/%d",
                                block_counter - 1, send_retries, max_send_retries);
                        uds_log_debug(retry_msg);
                        usleep(50000); /* 50ms delay before retry */
                    }
                }
            }
            
            if (!send_success) {
                char final_err[128];
                snprintf(final_err, sizeof(final_err),
                        "[错误] 传输数据失败，已重试%d次 (块 %d)",
                        max_send_retries, block_counter - 1);
                uds_log_error(final_err);
                free(transfer_payload);
                s19_free(s19);
                goto done;
            }
            
            free(transfer_payload);
            
            /* Wait for transfer response - Optimized for faster processing
             * - 忽略FC/无关帧
             * - 允许Pending(0x7F 36 78)，但使用更短的轮询间隔
             */
            {
                int waited_ms = 0;
                bool got_resp = false;
                int max_wait = 3000; /* Reduced from 8s to 3s */
                int poll_interval = 10; /* Reduced from 50ms to 10ms for faster response */
                
                while (waited_ms < max_wait) {
                    if (uds_wait_resp(s, &resp, poll_interval) == 0) {
                        /* Ignore Flow Control while waiting for 0x76/0x7F */
                        if ((resp.data[0] & 0xF0) == 0x30) {
                            continue;
                        }
                        /* Pending: 0x7F 0x36 0x78 */
                        if (resp.can_dlc >= 4 && resp.data[1] == UDS_RESP_NEGATIVE &&
                            resp.data[2] == UDS_SID_TRANSFER_DATA && resp.data[3] == UDS_NRC_PENDING) {
                            /* Reduced logging - log pending less frequently to reduce spam */
                            if (waited_ms % 1000 == 0) {
                char pending_msg[64];
                snprintf(pending_msg, sizeof(pending_msg), 
                        "[调试] 块 %d 等待中... (%dms)", 
                        block_counter - 1, waited_ms);
                uds_log_debug(pending_msg);
                            }
                            /* keep waiting */
                            continue;
                        }
                        got_resp = true;
                        break;
                    }
                    waited_ms += poll_interval;
                }
                if (!got_resp) {
                    char err_msg[128];
                    snprintf(err_msg, sizeof(err_msg),
                            "[错误] 传输响应超时 (块 %d, 偏移 %u/%u) 超时%dms",
                            block_counter - 1, offset, seg->size, waited_ms);
                    uds_log_error(err_msg);
                    s19_free(s19);
                    goto done;
                }
            }
            
            if (resp.can_dlc < 2 || resp.data[1] != (UDS_SID_TRANSFER_DATA + UDS_RESP_POSITIVE)) {
                char err_msg[256];
                if (resp.can_dlc >= 4 && resp.data[1] == UDS_RESP_NEGATIVE) {
                    snprintf(err_msg, sizeof(err_msg),
                            "[错误] 传输数据被拒绝: NRC=0x%02X (块 %d)",
                            resp.data[3], block_counter - 1);
                } else {
                    snprintf(err_msg, sizeof(err_msg),
                            "[错误] 无效的传输响应 (块 %d): [%02X %02X]",
                            block_counter - 1, 
                            resp.can_dlc > 0 ? resp.data[0] : 0,
                            resp.can_dlc > 1 ? resp.data[1] : 0);
                }
                uds_log_error(err_msg);
                s19_free(s19);
                goto done;
            }
            
            offset += chunk_size;
            block_counter = (block_counter % 255) + 1;
            total_processed += chunk_size;
            
            /* Log block progress every 100 blocks or at segment end - DEBUG级别 */
            uint32_t blocks_sent = (offset + block_size - 1) / block_size;
            if ((blocks_sent % 100 == 0) || (offset >= seg->size)) {
                char block_msg[128];
                snprintf(block_msg, sizeof(block_msg),
                        "[调试] 已发送 %u/%u 块 (%u/%u 字节)",
                        blocks_sent, total_blocks, offset, seg->size);
                uds_log_debug(block_msg);
            }
            
            /* Update progress (throttled to every 10%) */
            static int last_percent = -1;
            int percent = (total_processed * 100) / s19->total_bytes;
            if (g_uds.progress_cb && (percent / 10) != (last_percent / 10)) {
                last_percent = percent;
                int display_percent = 20 + (percent * 60 / 100);
                g_uds.progress_cb(display_percent, seg_idx + 1, 
                                 s19->segment_count, g_uds.progress_ud);
                
                /* Log progress every 10% */
                char progress_msg[64];
                snprintf(progress_msg, sizeof(progress_msg), 
                        "刷写进度: %d%% (%u/%u 字节)", 
                        percent, total_processed, s19->total_bytes);
                uds_log_progress(progress_msg);
            }
            
            /* Removed delay for maximum transfer speed - rely on CAN bus flow control */
        }
        
        /* Request Transfer Exit after each segment */
        uds_log_info("[TX] 请求传输退出");
        if (send_isotp_sf(s, g_uds.cfg.tx_id, UDS_SID_TRANSFER_EXIT, NULL, 0) != 0) {
            uds_log_error("[错误] 发送传输退出失败");
            s19_free(s19);
            goto done;
        }
        /* 等待0x77，若收到负响应Pending(0x7F 37 78)则继续等待 - Optimized */
        {
            int waited_ms = 0;
            bool ok = false;
            int max_wait = 5000; /* Reduced from 8s to 5s */
            int poll_interval = 20; /* Reduced from 100ms to 20ms for faster response */
            
            while (waited_ms < max_wait) {
                if (uds_wait_resp(s, &resp, poll_interval) == 0) {
                    if (resp.can_dlc >= 4 && resp.data[1] == UDS_RESP_NEGATIVE &&
                        resp.data[2] == UDS_SID_TRANSFER_EXIT && resp.data[3] == UDS_NRC_PENDING) {
                    /* ECU处理中，继续等 */
                    if (waited_ms % 1000 == 0) {
                        char pending_msg[64];
                        snprintf(pending_msg, sizeof(pending_msg), 
                                "[调试] 传输退出等待中... (已等待%dms)", waited_ms);
                        uds_log_debug(pending_msg);
                    }
                        continue;
                    }
                    if (resp.can_dlc >= 2 && resp.data[1] == (UDS_SID_TRANSFER_EXIT + UDS_RESP_POSITIVE)) {
                        ok = true;
                        break;
                    }
                }
                waited_ms += poll_interval;
            }
            if (!ok) {
                char exit_err[128];
                snprintf(exit_err, sizeof(exit_err), 
                        "[错误] 传输退出被拒绝或超时 %dms (分段 %d/%d)", 
                        waited_ms, seg_idx + 1, s19->segment_count);
                uds_log_error(exit_err);
                s19_free(s19);
                goto done;
            }
            uds_log_info("[RX] 传输退出已接受");
        }
    }
    
    s19_free(s19);
    
    if (g_uds.progress_cb) g_uds.progress_cb(85, 5, 7, g_uds.progress_ud);
    
    /* ==== Step 6: Routine Control (31 01 FF 01 44 00) - Qt-style forced multi-frame ==== */
    uds_log_info("[TX] 例程控制 (激活固件)");

    /* 按 Qt 版本的实现：强制使用多帧发送
     * FF: 10 0D 31 01 FF 01 44 00
     * CF: 21 00 00 00 00 00 00 00
     * （总长度0x0D=13字节，首帧携带6字节，剩余7字节在一帧CF中补零发送）
     */
    {
        /* 发送首帧 */
        uint8_t ff[8] = {0x10, 0x0D, 0x31, 0x01, 0xFF, 0x01, 0x44, 0x00};
        if (uds_send_frame(s, g_uds.cfg.tx_id, ff, 8) != 0) {
            uds_log_error("[错误] 发送例程控制首帧失败");
            goto done;
        }

        /* 等待流控制帧(最多1s) */
        struct can_frame fc;
        int waited_fc = 0;
        bool got_fc = false;
        while (waited_fc < 1000) {
            if (uds_wait_resp(s, &fc, 10) == 0 && ((fc.data[0] & 0xF0) == 0x30)) {
                got_fc = true;
                break;
            }
            waited_fc += 10;
        }
        if (!got_fc) {
            uds_log_error("[错误] 未收到例程控制的FC");
            goto done;
        }

        /* 发送连续帧 */
        uint8_t cf[8] = {0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        if (uds_send_frame(s, g_uds.cfg.tx_id, cf, 8) != 0) {
            uds_log_error("[错误] 发送例程控制连续帧失败");
            goto done;
        }

        /* 发送完例程控制后，立即将进度提升到95% */
        if (g_uds.progress_cb) g_uds.progress_cb(95, 6, 7, g_uds.progress_ud);
    }
    
    /* Wait for routine control response (max 2s, then proceed to reset) */
    {
        int waited_ms = 0;
        bool ok = false;
        int max_wait = 2000; /* Up to 2s for routine control */
        int poll_interval = 100; /* 100ms polling */
        int keepalive_tick = 0;
        
        while (waited_ms < max_wait) {
            if (uds_wait_resp(s, &resp, poll_interval) == 0) {
                if (resp.can_dlc >= 4 && resp.data[1] == UDS_RESP_NEGATIVE &&
                    resp.data[2] == UDS_SID_ROUTINE_CONTROL && resp.data[3] == UDS_NRC_PENDING) {
                    /* ECU processing routine, continue waiting */
                    if (waited_ms % 1000 == 0) {
                        char pending_msg[64];
                        snprintf(pending_msg, sizeof(pending_msg), 
                                "[调试] 例程控制等待中... (已等待%dms)", waited_ms);
                        uds_log_debug(pending_msg);
                    }
                } else if (resp.can_dlc >= 2 && resp.data[1] == (UDS_SID_ROUTINE_CONTROL + UDS_RESP_POSITIVE)) {
                    ok = true;
                    break;
                }
            }

            /* 可选：每3秒发送一次 Tester Present (3E 00) 作为保活（2s窗口通常不会触发） */
            keepalive_tick += poll_interval;
            if (keepalive_tick >= 3000) {
                uint8_t tp_payload[1] = {0x00};
                send_isotp_sf(s, g_uds.cfg.tx_id, UDS_SID_TESTER_PRESENT, tp_payload, 1);
                keepalive_tick = 0;
            }

            waited_ms += poll_interval;
        }
        
        if (!ok) {
            char routine_msg[128];
            snprintf(routine_msg, sizeof(routine_msg), 
                    "[信息] 例程控制超时 %dms, 继续ECU复位", waited_ms);
            uds_log_info(routine_msg);
        } else {
            uds_log_info("[RX] 例程控制已完成");
        }
    }
    
    /* Allow ECU time to process firmware activation (shorter) */
    uds_log_info("[信息] 等待ECU激活固件...");
    usleep(200000); /* 200ms */
    
    if (g_uds.progress_cb) g_uds.progress_cb(95, 6, 7, g_uds.progress_ud);
    
    /* ==== Step 7: ECU Reset (11 01) ==== */
    uds_log_progress("正在重启ECU...");
    uint8_t reset_req[] = {UDS_SUB_HARD_RESET};
    if (send_isotp_sf(s, g_uds.cfg.tx_id, UDS_SID_ECU_RESET, reset_req, 1) != 0) {
        uds_log_error("[错误] 发送复位命令失败");
        goto done;
    }
    /* 发送复位后立即将进度标记为100% */
    if (g_uds.progress_cb) g_uds.progress_cb(100, 7, 7, g_uds.progress_ud);
    
    /* Wait for reset response (optional, ECU may reset before responding) */
    {
        int waited_ms = 0;
        bool got_reset_resp = false;
        while (waited_ms < 3000) { /* 3s max wait for reset response */
            if (uds_wait_resp(s, &resp, 100) == 0) {
                if (resp.can_dlc >= 2 && resp.data[1] == (UDS_SID_ECU_RESET + UDS_RESP_POSITIVE)) {
                    uds_log_info("[RX] ECU复位已确认");
                    got_reset_resp = true;
                    break;
                }
                if (resp.can_dlc >= 4 && resp.data[1] == UDS_RESP_NEGATIVE &&
                    resp.data[2] == UDS_SID_ECU_RESET && resp.data[3] == UDS_NRC_PENDING) {
                    /* ECU is resetting, this is expected */
                    uds_log_debug("[调试] ECU正在复位中...");
                    continue;
                }
            }
            waited_ms += 100;
        }
        if (!got_reset_resp) {
            uds_log_info("[信息] ECU可能已立即复位");
        }
        
        /* 缩短ECU重启等待时间以优化总时长 */
        uds_log_info("[信息] 等待ECU完成重启...");
        sleep(1); /* 1 second */
    }
    
    uds_log_progress("刷写完成！ECU将使用新固件重启");
    if (g_uds.progress_cb) g_uds.progress_cb(100, 7, 7, g_uds.progress_ud);
    
done:
    close(s);
    g_uds.socket_fd = -1;
    g_uds.running = false;
    uds_log_progress("刷写结束");
    return NULL;
}

int uds_init(const uds_config_t *cfg)
{
    if (!cfg || !cfg->interface || !cfg->s19_path) return -1;
    
    memset(&g_uds, 0, sizeof(g_uds));
    g_uds.cfg = *cfg;
    strncpy(g_uds.interface, cfg->interface, sizeof(g_uds.interface) - 1);
    g_uds.socket_fd = -1;
    g_uds.ecu_max_block_size = 0x07FA; /* Default 2042 bytes */
    
    return 0;
}

int uds_start(void)
{
    if (g_uds.running) return 0;
    
    g_uds.running = true;
    if (pthread_create(&g_uds.thread, NULL, uds_thread, NULL) != 0) {
        g_uds.running = false;
        return -1;
    }
    
    return 0;
}

void uds_stop(void)
{
    if (!g_uds.running) return;
    
    g_uds.running = false;
    pthread_join(g_uds.thread, NULL);
}

void uds_deinit(void)
{
    if (g_uds.running) uds_stop();
}

bool uds_is_running(void)
{
    return g_uds.running;
}

void uds_register_progress_cb(uds_progress_cb cb, void *user_data)
{
    g_uds.progress_cb = cb;
    g_uds.progress_ud = user_data;
}

void uds_register_log_cb(uds_log_cb cb, void *user_data)
{
    g_uds.log_cb = cb;
    g_uds.log_ud = user_data;
}

/**
 * @brief 设置S19文件路径（用于远程控制）
 * @param path S19文件路径
 * @return 0成功，-1失败
 */
int uds_set_file_path(const char *path)
{
    if (!path) {
        log_error("S19文件路径为空");
        return -1;
    }
    
    // 检查文件是否存在
    if (access(path, R_OK) != 0) {
        log_error("无法访问S19文件: %s", path);
        return -1;
    }
    
    // 保存文件路径到持久缓冲区，避免调用者释放导致悬空指针
    memset(g_s19_path_buf, 0, sizeof(g_s19_path_buf));
    strncpy(g_s19_path_buf, path, sizeof(g_s19_path_buf) - 1);
    // 保存文件路径到配置
    g_uds.cfg.s19_path = g_s19_path_buf;
    
    log_info("已设置S19文件路径: %s", path);
    return 0;
}

int uds_set_params(const char *iface, uint32_t tx_id, uint32_t rx_id, uint32_t block_size)
{
    if (iface && iface[0]) {
        memset(g_uds_iface_buf, 0, sizeof(g_uds_iface_buf));
        strncpy(g_uds_iface_buf, iface, sizeof(g_uds_iface_buf) - 1);
    }
    if (tx_id) g_uds_tx_id = tx_id;
    if (rx_id) g_uds_rx_id = rx_id;
    if (block_size) g_uds_blk_size = block_size;

    log_info("UDS参数已设置: iface=%s tx=0x%X rx=0x%X blk=%u",
             g_uds_iface_buf, g_uds_tx_id, g_uds_rx_id, g_uds_blk_size);
    return 0;
}

/**
 * @brief 开始UDS刷写（用于远程控制）
 * @return 0成功，-1失败
 */
int uds_start_flash(void)
{
    // 检查是否已设置文件路径
    if (!g_uds.cfg.s19_path || g_uds.cfg.s19_path[0] == '\0') {
        log_error("未设置S19文件路径");
        return -1;
    }
    
    // 若尚未初始化（远程场景可能未进入设备UDS页面），用“网页端设置值/默认值”初始化一次
    if (g_uds.interface[0] == '\0' || g_uds.cfg.tx_id == 0 || g_uds.cfg.rx_id == 0 || g_uds.cfg.block_size == 0) {
        const char *iface = (g_uds_iface_buf[0] ? g_uds_iface_buf : "can0");
        uint32_t tx = g_uds_tx_id ? g_uds_tx_id : 0x7F3;
        uint32_t rx = g_uds_rx_id ? g_uds_rx_id : 0x7FB;
        uint32_t blk = g_uds_blk_size ? g_uds_blk_size : 256;
        uds_config_t cfg = { iface, tx, rx, blk, g_uds.cfg.s19_path };
        if (uds_init(&cfg) != 0) {
            log_error("UDS初始化失败（默认参数）");
            return -1;
        }
        // 注意：uds_init 会清零 g_uds，再次绑定 s19_path 到持久缓冲
        g_uds.cfg.s19_path = g_s19_path_buf[0] ? g_s19_path_buf : cfg.s19_path;
    } else {
        // 确保 cfg.s19_path 指向持久缓冲（防御性）
        if (g_s19_path_buf[0]) g_uds.cfg.s19_path = g_s19_path_buf;
    }

    // 开始刷写
    return uds_start();
}

/**
 * @brief 停止UDS刷写（用于远程控制）
 */
void uds_stop_flash(void)
{
    uds_stop();
}
