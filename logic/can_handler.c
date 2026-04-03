/**
 * @file can_handler.c
 * @brief CAN总线处理实现 - 使用Linux SocketCAN
 */

#include "can_handler.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <errno.h>
#include <time.h>

/* CAN处理器状态 */
typedef struct {
    int socket_fd;                      /* can0 SocketCAN文件描述符 */
    int socket_fd1;                     /* can1 SocketCAN文件描述符（双通道） */
    char interface[16];                 /* can0 接口名 */
    char interface1[16];                /* can1 接口名 */
    uint32_t bitrate;                   /* can0 波特率 */
    uint32_t bitrate1;                  /* can1 波特率 */
    bool running;                       /* 运行状态（控制所有接收线程） */
    bool dual_mode;                     /* 是否双通道模式 */
    pthread_t rx_thread;                /* can0 接收线程 */
    pthread_t rx_thread1;               /* can1 接收线程（双通道） */
    can_frame_callback_t callback;      /* 帧回调函数 */
    void *callback_user_data;           /* 回调用户数据 */
    can_stats_t stats;                  /* 统计信息（合计） */
    pthread_mutex_t mutex;              /* 互斥锁 */
} can_handler_ctx_t;

static can_handler_ctx_t g_can_ctx = {
    .socket_fd = -1,
    .socket_fd1 = -1,
    .running = false,
    .dual_mode = false,
};

/**
 * @brief 配置CAN接口
 */
static int configure_can_interface(const char *interface, uint32_t bitrate)
{
    char cmd[256];
    
    // 关闭接口
    snprintf(cmd, sizeof(cmd), "ip link set %s down", interface);
    if (system(cmd) != 0) {
        log_warn("关闭CAN接口失败（可能本来就是关闭的）");
    }
    
    // 设置波特率，禁用设备级loopback（使用socket层回环进行本地测试），自动重启以恢复bus-off
    snprintf(cmd, sizeof(cmd), "ip link set %s type can bitrate %u restart-ms 100", interface, bitrate);
    if (system(cmd) != 0) {
        log_error("设置CAN波特率失败");
        return -1;
    }
    
    // 提高TX队列长度，避免频繁E-NOBUFS
    snprintf(cmd, sizeof(cmd), "ip link set %s txqueuelen 1024", interface);
    if (system(cmd) != 0) {
        log_warn("设置txqueuelen失败（可忽略）");
    }
    
    // 启动接口
    snprintf(cmd, sizeof(cmd), "ip link set %s up", interface);
    if (system(cmd) != 0) {
        log_error("启动CAN接口失败");
        return -1;
    }
    
    // 启用错误上报（若驱动支持）
    snprintf(cmd, sizeof(cmd), "ip link set %s berr-reporting on", interface);
    if (system(cmd) != 0) {
        log_warn("启用错误上报失败（驱动可能不支持）");
    }
    
    log_info("CAN接口 %s 配置成功，波特率: %u", interface, bitrate);
    return 0;
}

/**
 * @brief 自动检测CAN接口的波特率
 * @param interface CAN接口名称（如"can0"）
 * @param timeout_sec 每个波特率的超时时间（秒）
 * @return 检测到的波特率，如果检测失败返回0
 */
static uint32_t auto_detect_bitrate(const char *interface, int timeout_sec)
{
    log_info("========== 自动检测 %s 波特率 ==========", interface);
    
    // 常用波特率列表（按优先级排序）
    uint32_t bitrates[] = {250000, 500000, 125000, 1000000};
    int num_bitrates = sizeof(bitrates) / sizeof(bitrates[0]);
    
    for (int i = 0; i < num_bitrates; i++) {
        uint32_t test_bitrate = bitrates[i];
        log_info("尝试波特率: %u bps", test_bitrate);
        
        // 配置接口
        if (configure_can_interface(interface, test_bitrate) != 0) {
            log_warn("配置波特率 %u 失败，跳过", test_bitrate);
            continue;
        }
        
        // 创建测试socket
        int test_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (test_fd < 0) {
            log_error("创建测试socket失败: %s", strerror(errno));
            continue;
        }
        
        // 绑定到接口
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, interface, sizeof(ifr.ifr_name) - 1);
        
        if (ioctl(test_fd, SIOCGIFINDEX, &ifr) < 0) {
            log_error("获取接口索引失败: %s", strerror(errno));
            close(test_fd);
            continue;
        }
        
        struct sockaddr_can addr;
        memset(&addr, 0, sizeof(addr));
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        
        if (bind(test_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            log_error("绑定测试socket失败: %s", strerror(errno));
            close(test_fd);
            continue;
        }
        
        // 设置接收超时
        struct timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        setsockopt(test_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        // 尝试接收数据
        struct can_frame frame;
        log_info("监听 %u bps，超时 %d 秒...", test_bitrate, timeout_sec);
        int nbytes = read(test_fd, &frame, sizeof(frame));
        close(test_fd);
        
        if (nbytes == sizeof(frame)) {
            log_info("✓ 检测到数据！确认波特率: %u bps", test_bitrate);
            log_info("  收到CAN ID: 0x%03X, DLC: %d", frame.can_id & CAN_EFF_MASK, frame.can_dlc);
            log_info("========================================");
            return test_bitrate;
        } else if (nbytes < 0 && errno == EAGAIN) {
            log_info("  超时，未收到数据");
        } else if (nbytes < 0) {
            log_warn("  接收错误: %s", strerror(errno));
        }
    }
    
    log_warn("✗ 未能检测到有效波特率");
    log_info("========================================");
    return 0;
}

/**
 * @brief CAN接收线程
 */
static void* can_rx_thread(void *arg)
{
    int which = (int)(intptr_t)arg; // 0: can0, 1: can1
    
    struct can_frame frame;
    struct timespec ts;
    fd_set read_fds;
    struct timeval timeout;
    
    log_info("CAN接收线程启动");
    
    while (g_can_ctx.running) {
        // 使用select带超时，避免阻塞导致无法退出
        FD_ZERO(&read_fds);
        int fd = (which == 1 && g_can_ctx.dual_mode) ? g_can_ctx.socket_fd1 : g_can_ctx.socket_fd;
        if (fd < 0) break;
        FD_SET(fd, &read_fds);
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms超时
        
        int ret = select(fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_error("select错误: %s", strerror(errno));
            break;
        }
        
        if (ret == 0) {
            // 超时，继续循环检查running标志
            continue;
        }
        
        // 有数据可读
        int nbytes = read(fd, &frame, sizeof(frame));
        
        if (nbytes < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            log_error("CAN接收错误: %s", strerror(errno));
            pthread_mutex_lock(&g_can_ctx.mutex);
            g_can_ctx.stats.error_count++;
            pthread_mutex_unlock(&g_can_ctx.mutex);
            continue;
        }
        
        if (nbytes != sizeof(frame)) {
            // 可能是错误帧或其他元数据，忽略大小不符但不中断
            continue;
        }
        
        // 获取时间戳
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t timestamp_us = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
        
        // 转换为应用层帧格式（先清零，确保结构体完全初始化）
        can_frame_t app_frame;
        memset(&app_frame, 0, sizeof(app_frame));
        app_frame.can_id = frame.can_id & CAN_EFF_MASK;
        app_frame.can_dlc = frame.can_dlc;
        memcpy(app_frame.data, frame.data, 8);
        app_frame.is_extended = (frame.can_id & CAN_EFF_FLAG) != 0;
        app_frame.channel = (which == 1) ? 1 : 0;
        app_frame.timestamp_us = timestamp_us;
        
        // 数据有效性验证
        if (app_frame.can_dlc > 8) {
            log_warn("Invalid DLC from kernel: %u, dropping frame", app_frame.can_dlc);
            continue;
        }
        
        // 更新统计
        pthread_mutex_lock(&g_can_ctx.mutex);
        g_can_ctx.stats.rx_count++;
        pthread_mutex_unlock(&g_can_ctx.mutex);
        
        // 调用回调函数（传递channel: 0=can0, 1=can1）
        if (g_can_ctx.callback) {
            g_can_ctx.callback(which, &app_frame, g_can_ctx.callback_user_data);
        }
    }
    
    log_info("CAN接收线程退出");
    return NULL;
}

/**
 * @brief 初始化CAN处理器
 */
int can_handler_init(const char *interface, uint32_t bitrate)
{
    if (g_can_ctx.running) {
        log_warn("CAN处理器已经初始化");
        return 0;
    }
    
    log_info("初始化CAN处理器: %s @ %u bps", interface, bitrate);
    
    // 保存参数
    strncpy(g_can_ctx.interface, interface, sizeof(g_can_ctx.interface) - 1);
    g_can_ctx.bitrate = bitrate;
    
    // 配置CAN接口
    if (configure_can_interface(interface, bitrate) < 0) {
        return -1;
    }
    
    // 创建socket
    g_can_ctx.socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (g_can_ctx.socket_fd < 0) {
        log_error("创建CAN socket失败: %s", strerror(errno));
        return -1;
    }
    
    // 开启回环并接收自身发送帧，便于本地监控
    int opt = 1;
    if (setsockopt(g_can_ctx.socket_fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &opt, sizeof(opt)) < 0) {
        log_warn("启用接收自发帧失败: %s", strerror(errno));
    }
    opt = 1; // Linux默认开启回环，这里显式设置一次
    if (setsockopt(g_can_ctx.socket_fd, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &opt, sizeof(opt)) < 0) {
        log_warn("启用回环失败: %s", strerror(errno));
    }
    
    // 绑定到CAN接口，并订阅错误帧
    struct ifreq ifr;
    strncpy(ifr.ifr_name, interface, sizeof(ifr.ifr_name) - 1);
    if (ioctl(g_can_ctx.socket_fd, SIOCGIFINDEX, &ifr) < 0) {
        log_error("获取CAN接口索引失败: %s", strerror(errno));
        close(g_can_ctx.socket_fd);
        g_can_ctx.socket_fd = -1;
        return -1;
    }
    
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    if (bind(g_can_ctx.socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("绑定CAN socket失败: %s", strerror(errno));
        close(g_can_ctx.socket_fd);
        g_can_ctx.socket_fd = -1;
        return -1;
    }

    // 订阅错误帧，同时保留默认的普通数据帧接收
    can_err_mask_t err_mask = CAN_ERR_MASK;
    if (setsockopt(g_can_ctx.socket_fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask)) < 0) {
        log_warn("设置错误帧订阅失败: %s", strerror(errno));
    }
    
    // 初始化互斥锁
    pthread_mutex_init(&g_can_ctx.mutex, NULL);
    
    // 清空统计信息
    memset(&g_can_ctx.stats, 0, sizeof(g_can_ctx.stats));
    
    log_info("CAN处理器初始化完成");
    return 0;
}

/**
 * @brief 清理CAN处理器
 */
void can_handler_deinit(void)
{
    if (g_can_ctx.running) {
        can_handler_stop();
    }
    
    if (g_can_ctx.socket_fd >= 0) {
        close(g_can_ctx.socket_fd);
        g_can_ctx.socket_fd = -1;
    }
    if (g_can_ctx.socket_fd1 >= 0) {
        close(g_can_ctx.socket_fd1);
        g_can_ctx.socket_fd1 = -1;
    }
    
    pthread_mutex_destroy(&g_can_ctx.mutex);
    g_can_ctx.dual_mode = false;
    g_can_ctx.interface[0] = '\0';
    g_can_ctx.interface1[0] = '\0';
    
    log_info("CAN处理器已清理");
}

/**
 * @brief 启动CAN接收
 */
int can_handler_start(void)
{
    if (g_can_ctx.running) {
        log_warn("CAN接收已经在运行");
        return 0;
    }
    
    if (g_can_ctx.socket_fd < 0 && !g_can_ctx.dual_mode) {
        log_error("CAN socket未初始化");
        return -1;
    }
    
    g_can_ctx.running = true;
    
    // 创建接收线程（can0）
    if (pthread_create(&g_can_ctx.rx_thread, NULL, can_rx_thread, (void*)(intptr_t)0) != 0) {
        log_error("创建CAN接收线程失败");
        g_can_ctx.running = false;
        return -1;
    }
    // 双通道时创建can1线程
    if (g_can_ctx.dual_mode && g_can_ctx.socket_fd1 >= 0) {
        if (pthread_create(&g_can_ctx.rx_thread1, NULL, can_rx_thread, (void*)(intptr_t)1) != 0) {
            log_error("创建CAN1接收线程失败");
        }
    }
    
    log_info("CAN接收已启动");
    return 0;
}

/**
 * @brief 停止CAN接收
 */
void can_handler_stop(void)
{
    if (!g_can_ctx.running) {
        return;
    }
    
    g_can_ctx.running = false;
    
    // 等待接收线程退出
    pthread_join(g_can_ctx.rx_thread, NULL);
    if (g_can_ctx.dual_mode && g_can_ctx.rx_thread1) {
        pthread_join(g_can_ctx.rx_thread1, NULL);
    }
    
    log_info("CAN接收已停止");
}

/**
 * @brief 发送CAN帧
 */
int can_handler_send(const can_frame_t *frame)
{
    if (g_can_ctx.socket_fd < 0) {
        log_error("CAN socket未初始化");
        return -1;
    }
    
    struct can_frame kernel_frame;
    memset(&kernel_frame, 0, sizeof(kernel_frame));
    
    kernel_frame.can_id = frame->can_id;
    if (frame->is_extended) {
        kernel_frame.can_id |= CAN_EFF_FLAG;
    }
    kernel_frame.can_dlc = frame->can_dlc;
    memcpy(kernel_frame.data, frame->data, frame->can_dlc);
    
    // 写发送，若缓冲不足重试几次
    int tries = 0;
    while (tries < 3) {
        int nbytes = write(g_can_ctx.socket_fd, &kernel_frame, sizeof(kernel_frame));
        if (nbytes == (int)sizeof(kernel_frame)) {
            pthread_mutex_lock(&g_can_ctx.mutex);
            g_can_ctx.stats.tx_count++;
            pthread_mutex_unlock(&g_can_ctx.mutex);
            return 0;
        }
        if (errno == ENOBUFS || errno == EAGAIN) {
            // 发送队列满，短暂等待后重试
            usleep(2000); // 2ms
            tries++;
            continue;
        }
        // 其他错误直接失败
        log_error("CAN发送失败: %s", strerror(errno));
        pthread_mutex_lock(&g_can_ctx.mutex);
        g_can_ctx.stats.error_count++;
        pthread_mutex_unlock(&g_can_ctx.mutex);
        return -1;
    }
    
    log_error("CAN发送失败: 发送队列拥塞");
    pthread_mutex_lock(&g_can_ctx.mutex);
    g_can_ctx.stats.error_count++;
    pthread_mutex_unlock(&g_can_ctx.mutex);
    return -1;
}

/**
 * @brief 注册帧接收回调
 */
void can_handler_register_callback(can_frame_callback_t callback, void *user_data)
{
    g_can_ctx.callback = callback;
    g_can_ctx.callback_user_data = user_data;
}

/**
 * @brief 获取统计信息
 */
void can_handler_get_stats(can_stats_t *stats)
{
    pthread_mutex_lock(&g_can_ctx.mutex);
    memcpy(stats, &g_can_ctx.stats, sizeof(can_stats_t));
    pthread_mutex_unlock(&g_can_ctx.mutex);
}

/**
 * @brief 检查CAN是否正在运行
 */
bool can_handler_is_running(void)
{
    return g_can_ctx.running;
}

/**
 * @brief 对外暴露的配置函数（仅配置网络接口，不打开socket）
 */
int can_handler_configure(const char *interface, uint32_t bitrate)
{
    if (!interface || bitrate == 0) {
        return -1;
    }
    return configure_can_interface(interface, bitrate);
}

/**
 * @brief 同时初始化can0与can1（双通道接收）
 */
int can_handler_init_dual(uint32_t bitrate0, uint32_t bitrate1)
{
    if (g_can_ctx.running) {
        log_warn("CAN处理器已经初始化");
        return 0;
    }

    /* CAN0：有配置波特率则直接使用，无则自动检测 */
    if (bitrate0 > 0) {
        log_info("CAN0 使用配置波特率: %u bps（跳过自动检测）", bitrate0);
        if (configure_can_interface("can0", bitrate0) < 0) return -1;
    } else {
        uint32_t detected = auto_detect_bitrate("can0", 2);
        if (detected > 0) {
            log_info("CAN0 自动检测成功，使用波特率: %u", detected);
            bitrate0 = detected;
        } else {
            bitrate0 = 500000;
            log_warn("CAN0 自动检测失败，使用默认波特率: %u", bitrate0);
            if (configure_can_interface("can0", bitrate0) < 0) return -1;
        }
    }

    /* CAN1：有配置波特率则直接使用，无则自动检测 */
    if (bitrate1 > 0) {
        log_info("CAN1 使用配置波特率: %u bps（跳过自动检测）", bitrate1);
        if (configure_can_interface("can1", bitrate1) < 0) return -1;
    } else {
        uint32_t detected = auto_detect_bitrate("can1", 2);
        if (detected > 0) {
            log_info("CAN1 自动检测成功，使用波特率: %u", detected);
            bitrate1 = detected;
        } else {
            bitrate1 = 500000;
            log_warn("CAN1 自动检测失败，使用默认波特率: %u", bitrate1);
            if (configure_can_interface("can1", bitrate1) < 0) return -1;
        }
    }

    g_can_ctx.socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (g_can_ctx.socket_fd < 0) {
        log_error("创建can0 socket失败: %s", strerror(errno));
        return -1;
    }
    g_can_ctx.socket_fd1 = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (g_can_ctx.socket_fd1 < 0) {
        log_error("创建can1 socket失败: %s", strerror(errno));
        close(g_can_ctx.socket_fd);
        g_can_ctx.socket_fd = -1;
        return -1;
    }

    int opt = 1;
    setsockopt(g_can_ctx.socket_fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &opt, sizeof(opt));
    setsockopt(g_can_ctx.socket_fd, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &opt, sizeof(opt));
    setsockopt(g_can_ctx.socket_fd1, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &opt, sizeof(opt));
    setsockopt(g_can_ctx.socket_fd1, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &opt, sizeof(opt));

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "can0", sizeof(ifr.ifr_name) - 1);
    if (ioctl(g_can_ctx.socket_fd, SIOCGIFINDEX, &ifr) < 0) {
        log_error("获取can0索引失败: %s", strerror(errno));
        goto fail;
    }
    struct sockaddr_can addr0 = {0};
    addr0.can_family = AF_CAN;
    addr0.can_ifindex = ifr.ifr_ifindex;
    if (bind(g_can_ctx.socket_fd, (struct sockaddr *)&addr0, sizeof(addr0)) < 0) {
        log_error("绑定can0失败: %s", strerror(errno));
        goto fail;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "can1", sizeof(ifr.ifr_name) - 1);
    if (ioctl(g_can_ctx.socket_fd1, SIOCGIFINDEX, &ifr) < 0) {
        log_error("获取can1索引失败: %s", strerror(errno));
        goto fail;
    }
    struct sockaddr_can addr1 = {0};
    addr1.can_family = AF_CAN;
    addr1.can_ifindex = ifr.ifr_ifindex;
    if (bind(g_can_ctx.socket_fd1, (struct sockaddr *)&addr1, sizeof(addr1)) < 0) {
        log_error("绑定can1失败: %s", strerror(errno));
        goto fail;
    }

    can_err_mask_t err_mask = CAN_ERR_MASK;
    setsockopt(g_can_ctx.socket_fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));
    setsockopt(g_can_ctx.socket_fd1, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));

    pthread_mutex_init(&g_can_ctx.mutex, NULL);
    memset(&g_can_ctx.stats, 0, sizeof(g_can_ctx.stats));
    strcpy(g_can_ctx.interface, "can0");
    strcpy(g_can_ctx.interface1, "can1");
    g_can_ctx.bitrate = bitrate0;
    g_can_ctx.bitrate1 = bitrate1;
    g_can_ctx.dual_mode = true;

    log_info("CAN双通道初始化完成");
    return 0;

fail:
    if (g_can_ctx.socket_fd >= 0) { close(g_can_ctx.socket_fd); g_can_ctx.socket_fd = -1; }
    if (g_can_ctx.socket_fd1 >= 0) { close(g_can_ctx.socket_fd1); g_can_ctx.socket_fd1 = -1; }
    return -1;
}

/**
 * @brief 在指定接口上发送CAN帧（独立socket，不改变当前接收）
 */
int can_handler_send_on(const char *interface, const can_frame_t *frame)
{
    if (!interface || !frame) return -1;
    
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        log_error("创建临时发送socket失败: %s", strerror(errno));
        return -1;
    }
    
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface, sizeof(ifr.ifr_name) - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        log_error("获取接口索引失败: %s", strerror(errno));
        close(s);
        return -1;
    }
    
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("绑定发送socket失败: %s", strerror(errno));
        close(s);
        return -1;
    }
    
    struct can_frame kf;
    memset(&kf, 0, sizeof(kf));
    kf.can_id = frame->can_id | (frame->is_extended ? CAN_EFF_FLAG : 0);
    kf.can_dlc = frame->can_dlc;
    memcpy(kf.data, frame->data, frame->can_dlc);
    
    int ret = -1;
    for (int i = 0; i < 3; i++) {
        int n = write(s, &kf, sizeof(kf));
        if (n == (int)sizeof(kf)) { ret = 0; break; }
        if (errno == ENOBUFS || errno == EAGAIN) { usleep(2000); continue; }
        break;
    }
    close(s);
    return ret;
}

/**
 * @brief 获取当前CAN0波特率
 */
uint32_t can_handler_get_bitrate(void)
{
    pthread_mutex_lock(&g_can_ctx.mutex);
    uint32_t bitrate = g_can_ctx.bitrate;
    pthread_mutex_unlock(&g_can_ctx.mutex);
    return bitrate;
}

/**
 * @brief 获取双通道模式下的波特率
 */
void can_handler_get_bitrate_dual(uint32_t *bitrate0, uint32_t *bitrate1)
{
    if (!bitrate0 || !bitrate1) return;
    
    pthread_mutex_lock(&g_can_ctx.mutex);
    *bitrate0 = g_can_ctx.bitrate;
    *bitrate1 = g_can_ctx.bitrate1;
    pthread_mutex_unlock(&g_can_ctx.mutex);
}

