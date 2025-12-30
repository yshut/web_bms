#include "can_worker.h"
#include "can_buffer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/ioctl.h>
#include <net/if.h>

// CAN工作器状态
typedef struct {
    bool running;
    bool can1_enabled;
    bool can2_enabled;
    int can1_socket;
    int can2_socket;
    pthread_t can1_thread;
    pthread_t can2_thread;
    can_message_callback_t callback;
    can_ring_buffer_t *buffer;
} can_worker_state_t;

static can_worker_state_t g_can_worker = {
    .running = false,
    .can1_enabled = false,
    .can2_enabled = false,
    .can1_socket = -1,
    .can2_socket = -1,
    .callback = NULL,
    .buffer = NULL
};

// CAN接收线程
static void* can_receive_thread(void *arg) {
    int socket_fd = *(int*)arg;
    struct can_frame frame;
    char msg_buffer[128];
    
    while (g_can_worker.running) {
        int nbytes = read(socket_fd, &frame, sizeof(struct can_frame));
        if (nbytes > 0) {
            // 格式化CAN消息: ID#DATA
            int pos = snprintf(msg_buffer, sizeof(msg_buffer), "%03X#", frame.can_id & CAN_EFF_MASK);
            for (int i = 0; i < frame.can_dlc; i++) {
                pos += snprintf(msg_buffer + pos, sizeof(msg_buffer) - pos, "%02X", frame.data[i]);
            }
            
            // 添加到缓冲区
            if (g_can_worker.buffer) {
                can_buffer_write(g_can_worker.buffer, msg_buffer);
            }
            
            // 调用回调
            if (g_can_worker.callback) {
                g_can_worker.callback(msg_buffer);
            }
        }
        usleep(1000); // 1ms
    }
    
    return NULL;
}

// 打开CAN接口
static int can_open_socket(const char *ifname, uint32_t bitrate) {
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    
    // 配置CAN接口（使用ip命令）
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ip link set %s type can bitrate %u", ifname, bitrate);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "ip link set %s up", ifname);
    system(cmd);
    
    // 创建socket
    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("socket");
        return -1;
    }
    
    // 绑定接口
    strcpy(ifr.ifr_name, ifname);
    ioctl(s, SIOCGIFINDEX, &ifr);
    
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(s);
        return -1;
    }
    
    return s;
}

int can_worker_init(void) {
    // 创建环形缓冲区
    g_can_worker.buffer = can_buffer_create(10000);
    if (!g_can_worker.buffer) {
        return -1;
    }
    return 0;
}

int can_worker_start(bool can1_enabled, bool can2_enabled, uint32_t bitrate1, uint32_t bitrate2) {
    if (g_can_worker.running) {
        printf("CAN worker already running\n");
        return -1;
    }
    
    g_can_worker.can1_enabled = can1_enabled;
    g_can_worker.can2_enabled = can2_enabled;
    g_can_worker.running = true;
    
    // 启动CAN1
    if (can1_enabled) {
        g_can_worker.can1_socket = can_open_socket("can0", bitrate1);
        if (g_can_worker.can1_socket < 0) {
            printf("Failed to open CAN0\n");
            g_can_worker.running = false;
            return -1;
        }
        pthread_create(&g_can_worker.can1_thread, NULL, can_receive_thread, &g_can_worker.can1_socket);
    }
    
    // 启动CAN2
    if (can2_enabled) {
        g_can_worker.can2_socket = can_open_socket("can1", bitrate2);
        if (g_can_worker.can2_socket < 0) {
            printf("Failed to open CAN1\n");
            // 如果CAN1已启动，继续运行
            if (!can1_enabled) {
                g_can_worker.running = false;
                return -1;
            }
        } else {
            pthread_create(&g_can_worker.can2_thread, NULL, can_receive_thread, &g_can_worker.can2_socket);
        }
    }
    
    printf("CAN worker started (CAN0: %s, CAN1: %s)\n", 
           can1_enabled ? "enabled" : "disabled",
           can2_enabled ? "enabled" : "disabled");
    
    return 0;
}

void can_worker_stop(void) {
    if (!g_can_worker.running) {
        return;
    }
    
    g_can_worker.running = false;
    
    // 等待线程结束
    if (g_can_worker.can1_enabled && g_can_worker.can1_thread) {
        pthread_join(g_can_worker.can1_thread, NULL);
    }
    if (g_can_worker.can2_enabled && g_can_worker.can2_thread) {
        pthread_join(g_can_worker.can2_thread, NULL);
    }
    
    // 关闭socket
    if (g_can_worker.can1_socket >= 0) {
        close(g_can_worker.can1_socket);
        g_can_worker.can1_socket = -1;
        system("ip link set can0 down");
    }
    if (g_can_worker.can2_socket >= 0) {
        close(g_can_worker.can2_socket);
        g_can_worker.can2_socket = -1;
        system("ip link set can1 down");
    }
    
    printf("CAN worker stopped\n");
}

void can_worker_scan(void) {
    printf("Scanning CAN interfaces...\n");
    system("ip link show | grep can");
}

int can_worker_send_frame(const char *frame_str) {
    // 解析帧格式: ID#DATA (例如: 123#0102030405060708)
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    
    char *sharp = strchr(frame_str, '#');
    if (!sharp) {
        printf("Invalid frame format (expected ID#DATA)\n");
        return -1;
    }
    
    // 解析ID
    char id_str[16];
    int id_len = sharp - frame_str;
    strncpy(id_str, frame_str, id_len);
    id_str[id_len] = '\0';
    frame.can_id = strtoul(id_str, NULL, 16);
    
    // 解析数据
    const char *data_str = sharp + 1;
    int data_len = strlen(data_str);
    frame.can_dlc = data_len / 2;
    if (frame.can_dlc > 8) frame.can_dlc = 8;
    
    for (int i = 0; i < frame.can_dlc; i++) {
        char byte_str[3] = {data_str[i*2], data_str[i*2+1], '\0'};
        frame.data[i] = strtoul(byte_str, NULL, 16);
    }
    
    // 发送到所有启用的接口
    int sent = 0;
    if (g_can_worker.can1_socket >= 0) {
        if (write(g_can_worker.can1_socket, &frame, sizeof(frame)) == sizeof(frame)) {
            sent++;
        }
    }
    if (g_can_worker.can2_socket >= 0) {
        if (write(g_can_worker.can2_socket, &frame, sizeof(frame)) == sizeof(frame)) {
            sent++;
        }
    }
    
    printf("Sent CAN frame to %d interface(s)\n", sent);
    return sent > 0 ? 0 : -1;
}

void can_worker_set_callback(can_message_callback_t callback) {
    g_can_worker.callback = callback;
}

bool can_worker_is_running(void) {
    return g_can_worker.running;
}

