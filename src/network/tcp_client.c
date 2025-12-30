#include "tcp_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

// TCP客户端状态
typedef struct {
    int socket_fd;
    bool connected;
    bool running;
    pthread_t recv_thread;
    char host[64];
    uint16_t port;
    tcp_message_callback_t msg_callback;
    tcp_connection_callback_t conn_callback;
} tcp_client_state_t;

static tcp_client_state_t g_tcp_client = {
    .socket_fd = -1,
    .connected = false,
    .running = false,
    .msg_callback = NULL,
    .conn_callback = NULL
};

// 接收线程
static void* tcp_recv_thread(void *arg) {
    char buffer[4096];
    
    while (g_tcp_client.running && g_tcp_client.connected) {
        int n = recv(g_tcp_client.socket_fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            
            // 调用回调
            if (g_tcp_client.msg_callback) {
                g_tcp_client.msg_callback(buffer, n);
            }
        } else if (n == 0) {
            // 连接关闭
            printf("TCP connection closed by server\n");
            g_tcp_client.connected = false;
            
            if (g_tcp_client.conn_callback) {
                g_tcp_client.conn_callback(false);
            }
            break;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                printf("TCP recv error: %s\n", strerror(errno));
                g_tcp_client.connected = false;
                
                if (g_tcp_client.conn_callback) {
                    g_tcp_client.conn_callback(false);
                }
                break;
            }
        }
        
        usleep(10000); // 10ms
    }
    
    return NULL;
}

int tcp_client_init(void) {
    g_tcp_client.socket_fd = -1;
    g_tcp_client.connected = false;
    g_tcp_client.running = false;
    return 0;
}

int tcp_client_connect(const char *host, uint16_t port) {
    if (g_tcp_client.connected) {
        printf("TCP client already connected\n");
        return -1;
    }
    
    // 创建socket
    g_tcp_client.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_tcp_client.socket_fd < 0) {
        printf("Failed to create TCP socket: %s\n", strerror(errno));
        return -1;
    }
    
    // 设置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        printf("Invalid address: %s\n", host);
        close(g_tcp_client.socket_fd);
        g_tcp_client.socket_fd = -1;
        return -1;
    }
    
    // 连接到服务器
    if (connect(g_tcp_client.socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("Failed to connect to %s:%d: %s\n", host, port, strerror(errno));
        close(g_tcp_client.socket_fd);
        g_tcp_client.socket_fd = -1;
        return -1;
    }
    
    g_tcp_client.connected = true;
    g_tcp_client.running = true;
    strncpy(g_tcp_client.host, host, sizeof(g_tcp_client.host) - 1);
    g_tcp_client.port = port;
    
    // 启动接收线程
    pthread_create(&g_tcp_client.recv_thread, NULL, tcp_recv_thread, NULL);
    
    printf("TCP connected to %s:%d\n", host, port);
    
    // 调用回调
    if (g_tcp_client.conn_callback) {
        g_tcp_client.conn_callback(true);
    }
    
    return 0;
}

void tcp_client_disconnect(void) {
    if (!g_tcp_client.connected) {
        return;
    }
    
    g_tcp_client.running = false;
    g_tcp_client.connected = false;
    
    // 等待接收线程结束
    if (g_tcp_client.recv_thread) {
        pthread_join(g_tcp_client.recv_thread, NULL);
    }
    
    // 关闭socket
    if (g_tcp_client.socket_fd >= 0) {
        close(g_tcp_client.socket_fd);
        g_tcp_client.socket_fd = -1;
    }
    
    printf("TCP disconnected\n");
    
    // 调用回调
    if (g_tcp_client.conn_callback) {
        g_tcp_client.conn_callback(false);
    }
}

int tcp_client_send(const char *message, int length) {
    if (!g_tcp_client.connected || g_tcp_client.socket_fd < 0) {
        printf("TCP client not connected\n");
        return -1;
    }
    
    int sent = send(g_tcp_client.socket_fd, message, length, 0);
    if (sent < 0) {
        printf("TCP send error: %s\n", strerror(errno));
        return -1;
    }
    
    return sent;
}

void tcp_client_set_message_callback(tcp_message_callback_t callback) {
    g_tcp_client.msg_callback = callback;
}

void tcp_client_set_connection_callback(tcp_connection_callback_t callback) {
    g_tcp_client.conn_callback = callback;
}

bool tcp_client_is_connected(void) {
    return g_tcp_client.connected;
}

