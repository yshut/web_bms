/**
 * @file ws_client.c
 * @brief WebSocket远程控制客户端实现
 */

#include "ws_client.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdint.h>

/* WebSocket帧操作码 */
#define WS_OPCODE_TEXT    0x01
#define WS_OPCODE_BINARY  0x02
#define WS_OPCODE_CLOSE   0x08
#define WS_OPCODE_PING    0x09
#define WS_OPCODE_PONG    0x0A

typedef struct {
    char *line;
    int channel;
    double timestamp_s;
    uint64_t seq;
} ws_can_frame_entry_t;

/* WebSocket客户端上下文 */
typedef struct {
    /* 配置 */
    ws_config_t config;
    
    /* 连接状态 */
    ws_state_t state;
    int socket_fd;
    pthread_t thread;
    bool thread_running;
    bool should_stop;
    
    /* 回调 */
    ws_state_callback_t state_callback;
    void *state_callback_user_data;
    
    /* 设备ID */
    char device_id[33];  // 32位十六进制字符串 + \0
    
    /* CAN帧缓冲（批量上报） */
    ws_can_frame_entry_t *can_frame_buffer;
    int can_frame_count;
    int can_frame_capacity;
    pthread_mutex_t can_buffer_mutex;
    uint64_t last_can_flush_time;  // 毫秒级时间戳
    uint64_t next_can_seq;
    
    /* 互斥锁 */
    pthread_mutex_t mutex;
    
    /* 重连计时 */
    time_t last_connect_attempt;
    int reconnect_backoff_ms;
    
} ws_client_ctx_t;

static ws_client_ctx_t g_ws_ctx = {
    .socket_fd = -1,
    .thread_running = false,
    .should_stop = false,
    .state = WS_STATE_DISCONNECTED,
    .can_frame_buffer = NULL,
    .can_frame_count = 0,
    .can_frame_capacity = 0,
    .reconnect_backoff_ms = 4000,
};

/* 前向声明 */
static void* ws_client_thread(void *arg);
static int ws_connect(void);
static void ws_disconnect(void);
static int ws_do_handshake(void);
static int ws_send_frame(uint8_t opcode, const uint8_t *payload, size_t payload_len);
static int ws_receive_frame(void);
static void ws_handle_message(const char *msg, size_t len);
static void ws_flush_can_frames(void);
static void generate_device_id(char *id_buf, size_t buf_size);
static size_t json_escape_string(const char *src, char *dst, size_t dst_size);

/**
 * @brief 生成设备ID（基于MAC地址或系统信息的哈希）
 */
static void generate_device_id(char *id_buf, size_t buf_size)
{
    // 简化实现：读取hostname作为设备ID
    char hostname[256] = {0};
    gethostname(hostname, sizeof(hostname) - 1);
    
    // 简单哈希
    unsigned long hash = 5381;
    for (const char *p = hostname; *p; p++) {
        hash = ((hash << 5) + hash) + (unsigned char)*p;
    }
    
    snprintf(id_buf, buf_size, "%016lx", hash);
}

/**
 * @brief 初始化WebSocket客户端
 */
int ws_client_init(const ws_config_t *config)
{
    if (!config) {
        log_error("ws_client_init: config is NULL");
        return -1;
    }
    
    pthread_mutex_init(&g_ws_ctx.mutex, NULL);
    pthread_mutex_init(&g_ws_ctx.can_buffer_mutex, NULL);
    
    memcpy(&g_ws_ctx.config, config, sizeof(ws_config_t));
    
    // 生成设备ID
    generate_device_id(g_ws_ctx.device_id, sizeof(g_ws_ctx.device_id));
    
    // 初始化CAN帧缓冲
    g_ws_ctx.can_frame_capacity = 1000;
    g_ws_ctx.can_frame_buffer = (ws_can_frame_entry_t*)calloc(
        g_ws_ctx.can_frame_capacity, sizeof(ws_can_frame_entry_t));
    
    // 初始化时间戳（毫秒）
    struct timeval tv;
    gettimeofday(&tv, NULL);
    g_ws_ctx.last_can_flush_time = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    
    log_info("WebSocket客户端初始化完成, 设备ID: %s", g_ws_ctx.device_id);
    
    return 0;
}

/**
 * @brief 反初始化WebSocket客户端
 */
void ws_client_deinit(void)
{
    ws_client_stop();
    
    pthread_mutex_lock(&g_ws_ctx.can_buffer_mutex);
    if (g_ws_ctx.can_frame_buffer) {
        for (int i = 0; i < g_ws_ctx.can_frame_count; i++) {
            free(g_ws_ctx.can_frame_buffer[i].line);
        }
        free(g_ws_ctx.can_frame_buffer);
        g_ws_ctx.can_frame_buffer = NULL;
    }
    pthread_mutex_unlock(&g_ws_ctx.can_buffer_mutex);
    
    pthread_mutex_destroy(&g_ws_ctx.mutex);
    pthread_mutex_destroy(&g_ws_ctx.can_buffer_mutex);
    
    log_info("WebSocket客户端已关闭");
}

/**
 * @brief 启动WebSocket连接
 */
int ws_client_start(void)
{
    pthread_mutex_lock(&g_ws_ctx.mutex);
    
    if (g_ws_ctx.thread_running) {
        pthread_mutex_unlock(&g_ws_ctx.mutex);
        log_warn("WebSocket客户端已经在运行");
        return 0;
    }
    
    g_ws_ctx.should_stop = false;
    g_ws_ctx.thread_running = true;
    
    if (pthread_create(&g_ws_ctx.thread, NULL, ws_client_thread, NULL) != 0) {
        log_error("创建WebSocket线程失败: %s", strerror(errno));
        g_ws_ctx.thread_running = false;
        pthread_mutex_unlock(&g_ws_ctx.mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&g_ws_ctx.mutex);
    
    log_info("WebSocket客户端已启动");
    return 0;
}

/**
 * @brief 停止WebSocket连接
 */
void ws_client_stop(void)
{
    pthread_mutex_lock(&g_ws_ctx.mutex);
    
    if (!g_ws_ctx.thread_running) {
        pthread_mutex_unlock(&g_ws_ctx.mutex);
        return;
    }
    
    g_ws_ctx.should_stop = true;
    pthread_mutex_unlock(&g_ws_ctx.mutex);
    
    // 等待线程结束
    pthread_join(g_ws_ctx.thread, NULL);
    
    ws_disconnect();
    
    log_info("WebSocket客户端已停止");
}

/**
 * @brief 获取连接状态
 */
ws_state_t ws_client_get_state(void)
{
    pthread_mutex_lock(&g_ws_ctx.mutex);
    ws_state_t state = g_ws_ctx.state;
    pthread_mutex_unlock(&g_ws_ctx.mutex);
    return state;
}

/**
 * @brief 检查是否已连接
 */
bool ws_client_is_connected(void)
{
    pthread_mutex_lock(&g_ws_ctx.mutex);
    bool connected = (g_ws_ctx.state >= WS_STATE_CONNECTED);
    pthread_mutex_unlock(&g_ws_ctx.mutex);
    return connected;
}

/**
 * @brief 注册连接状态回调
 */
void ws_client_register_state_callback(ws_state_callback_t callback, void *user_data)
{
    pthread_mutex_lock(&g_ws_ctx.mutex);
    g_ws_ctx.state_callback = callback;
    g_ws_ctx.state_callback_user_data = user_data;
    pthread_mutex_unlock(&g_ws_ctx.mutex);
}

/**
 * @brief 获取设备ID
 */
const char* ws_client_get_device_id(void)
{
    return g_ws_ctx.device_id;
}

/**
 * @brief 获取服务器信息
 */
int ws_client_get_server_info(char *host, size_t host_size, uint16_t *port)
{
    if (!host || !port) return -1;
    
    pthread_mutex_lock(&g_ws_ctx.mutex);
    strncpy(host, g_ws_ctx.config.host, host_size - 1);
    host[host_size - 1] = '\0';
    *port = g_ws_ctx.config.port;
    pthread_mutex_unlock(&g_ws_ctx.mutex);
    
    return 0;
}

/**
 * @brief WebSocket客户端线程
 */
static void* ws_client_thread(void *arg)
{
    (void)arg;
    
    log_info("WebSocket客户端线程启动");
    
    while (!g_ws_ctx.should_stop) {
        // 检查是否需要连接
        if (g_ws_ctx.state == WS_STATE_DISCONNECTED) {
            time_t now = time(NULL);
            if (now - g_ws_ctx.last_connect_attempt >= g_ws_ctx.config.reconnect_interval_ms / 1000) {
                g_ws_ctx.last_connect_attempt = now;
                ws_connect();
            }
        }
        
        // 如果已连接，接收消息
        if (g_ws_ctx.state >= WS_STATE_CONNECTED && g_ws_ctx.socket_fd >= 0) {
            int ret = ws_receive_frame();
            if (ret < 0) {
                log_warn("WebSocket接收失败，断开连接");
                ws_disconnect();
            }
        }
        
        // 智能刷新CAN帧缓冲：时间触发或数量触发
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint64_t now_ms = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
        
        pthread_mutex_lock(&g_ws_ctx.can_buffer_mutex);
        int frame_count = g_ws_ctx.can_frame_count;
        pthread_mutex_unlock(&g_ws_ctx.can_buffer_mutex);
        
        // 触发条件1: 超过200ms（保证流畅性）
        // 触发条件2: 积累了50帧或更多（避免网络拥塞）
        bool should_flush = (now_ms - g_ws_ctx.last_can_flush_time >= 200) ||  // 200ms
                           (frame_count >= 50);  // 50帧
        
        if (should_flush && frame_count > 0) {
            ws_flush_can_frames();
            g_ws_ctx.last_can_flush_time = now_ms;
        }
        
        usleep(50000);  // 50ms检查一次
    }
    
    g_ws_ctx.thread_running = false;
    log_info("WebSocket客户端线程退出");
    
    return NULL;
}

/**
 * @brief 连接到WebSocket服务器
 */
static int ws_connect(void)
{
    log_info("连接到WebSocket服务器: %s:%d", g_ws_ctx.config.host, g_ws_ctx.config.port);
    
    g_ws_ctx.state = WS_STATE_CONNECTING;
    
    // 创建socket
    g_ws_ctx.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_ws_ctx.socket_fd < 0) {
        log_error("创建socket失败: %s", strerror(errno));
        g_ws_ctx.state = WS_STATE_DISCONNECTED;
        return -1;
    }
    
    // 解析服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_ws_ctx.config.port);
    
    struct hostent *he = gethostbyname(g_ws_ctx.config.host);
    if (!he) {
        log_error("解析服务器地址失败: %s", g_ws_ctx.config.host);
        close(g_ws_ctx.socket_fd);
        g_ws_ctx.socket_fd = -1;
        g_ws_ctx.state = WS_STATE_DISCONNECTED;
        return -1;
    }
    
    memcpy(&server_addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    
    // 连接
    if (connect(g_ws_ctx.socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        log_error("连接服务器失败: %s", strerror(errno));
        close(g_ws_ctx.socket_fd);
        g_ws_ctx.socket_fd = -1;
        g_ws_ctx.state = WS_STATE_DISCONNECTED;
        return -1;
    }
    
    // WebSocket握手
    if (ws_do_handshake() < 0) {
        log_error("WebSocket握手失败");
        close(g_ws_ctx.socket_fd);
        g_ws_ctx.socket_fd = -1;
        g_ws_ctx.state = WS_STATE_DISCONNECTED;
        return -1;
    }
    
    g_ws_ctx.state = WS_STATE_CONNECTED;
    log_info("WebSocket连接成功");
    
    // 立即发送device_id事件（让服务器识别设备）
    char device_id_msg[256];
    snprintf(device_id_msg, sizeof(device_id_msg),
             "{\"event\":\"device_id\",\"data\":{\"id\":\"%s\"}}",
             g_ws_ctx.device_id);
    
    if (ws_client_send_json(device_id_msg) < 0) {
        log_warn("发送device_id事件失败");
    } else {
        log_info("已发送device_id: %s", g_ws_ctx.device_id);
    }
    
    // 触发状态回调
    if (g_ws_ctx.state_callback) {
        g_ws_ctx.state_callback(true, g_ws_ctx.config.host, g_ws_ctx.config.port, 
                               g_ws_ctx.state_callback_user_data);
    }
    
    return 0;
}

/**
 * @brief 断开WebSocket连接
 */
static void ws_disconnect(void)
{
    if (g_ws_ctx.socket_fd >= 0) {
        close(g_ws_ctx.socket_fd);
        g_ws_ctx.socket_fd = -1;
    }
    
    if (g_ws_ctx.state != WS_STATE_DISCONNECTED) {
        g_ws_ctx.state = WS_STATE_DISCONNECTED;
        log_info("WebSocket已断开");
        
        // 触发状态回调
        if (g_ws_ctx.state_callback) {
            g_ws_ctx.state_callback(false, g_ws_ctx.config.host, g_ws_ctx.config.port,
                                   g_ws_ctx.state_callback_user_data);
        }
    }
}

/**
 * @brief WebSocket握手
 */
static int ws_do_handshake(void)
{
    // TODO: 完整实现WebSocket握手协议
    // 这里提供一个简化版本的占位实现
    
    char request[1024];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n",
             g_ws_ctx.config.path,
             g_ws_ctx.config.host,
             g_ws_ctx.config.port);
    
    if (send(g_ws_ctx.socket_fd, request, strlen(request), 0) < 0) {
        log_error("发送WebSocket握手请求失败");
        return -1;
    }
    
    // 接收响应
    char response[2048];
    ssize_t n = recv(g_ws_ctx.socket_fd, response, sizeof(response) - 1, 0);
    if (n <= 0) {
        log_error("接收WebSocket握手响应失败");
        return -1;
    }
    response[n] = '\0';
    
    // 简单检查是否包含101 Switching Protocols
    if (strstr(response, "101") == NULL) {
        log_error("WebSocket握手响应无效");
        return -1;
    }
    
    log_info("WebSocket握手成功");
    return 0;
}

/**
 * @brief 发送WebSocket帧
 */
static int ws_send_frame(uint8_t opcode, const uint8_t *payload, size_t payload_len)
{
    if (g_ws_ctx.socket_fd < 0) {
        return -1;
    }
    
    // TODO: 完整实现WebSocket帧协议（包含mask）
    // 这是一个简化实现
    
    uint8_t header[14];
    size_t header_len = 0;
    
    // Byte 0: FIN + opcode
    header[header_len++] = 0x80 | (opcode & 0x0F);
    
    // Byte 1: MASK + payload length
    if (payload_len < 126) {
        header[header_len++] = 0x80 | (uint8_t)payload_len;
    } else if (payload_len < 65536) {
        header[header_len++] = 0x80 | 126;
        header[header_len++] = (uint8_t)(payload_len >> 8);
        header[header_len++] = (uint8_t)(payload_len & 0xFF);
    } else {
        header[header_len++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--) {
            header[header_len++] = (uint8_t)((payload_len >> (i * 8)) & 0xFF);
        }
    }
    
    // Masking key (required for client-to-server)
    uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};  // 简化：使用固定mask
    memcpy(&header[header_len], mask, 4);
    header_len += 4;
    
    // 发送header
    if (send(g_ws_ctx.socket_fd, header, header_len, 0) < 0) {
        log_error("发送WebSocket帧头失败");
        return -1;
    }
    
    // 发送masked payload
    if (payload_len > 0) {
        uint8_t *masked_payload = (uint8_t*)malloc(payload_len);
        for (size_t i = 0; i < payload_len; i++) {
            masked_payload[i] = payload[i] ^ mask[i % 4];
        }
        
        ssize_t sent = send(g_ws_ctx.socket_fd, masked_payload, payload_len, 0);
        free(masked_payload);
        
        if (sent < 0 || (size_t)sent != payload_len) {
            log_error("发送WebSocket帧数据失败");
            return -1;
        }
    }
    
    return 0;
}

/**
 * @brief 接收WebSocket帧
 */
static int ws_receive_frame(void)
{
    // TODO: 完整实现WebSocket帧接收和解析
    // 这是一个简化实现
    
    uint8_t header[2];
    ssize_t n = recv(g_ws_ctx.socket_fd, header, 2, MSG_DONTWAIT);
    
    if (n == 0) {
        return -1;  // 连接关闭
    } else if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // 无数据
        }
        return -1;  // 错误
    }
    
    if (n < 2) {
        return 0;  // 数据不完整
    }
    
    uint8_t opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;
    
    // 读取扩展长度
    if (payload_len == 126) {
        uint8_t len_bytes[2];
        if (recv(g_ws_ctx.socket_fd, len_bytes, 2, 0) != 2) return -1;
        payload_len = ((uint64_t)len_bytes[0] << 8) | len_bytes[1];
    } else if (payload_len == 127) {
        uint8_t len_bytes[8];
        if (recv(g_ws_ctx.socket_fd, len_bytes, 8, 0) != 8) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | len_bytes[i];
        }
    }
    
    // 读取mask key (如果有)
    uint8_t mask[4] = {0};
    if (masked) {
        if (recv(g_ws_ctx.socket_fd, mask, 4, 0) != 4) return -1;
    }
    
    // 读取payload
    if (payload_len > 0 && payload_len < 1024 * 1024) {  // 限制最大1MB
        char *payload = (char*)malloc((size_t)payload_len + 1);
        if (!payload) return -1;
        
        size_t received = 0;
        while (received < payload_len) {
            ssize_t n = recv(g_ws_ctx.socket_fd, payload + received, (size_t)(payload_len - received), 0);
            if (n <= 0) {
                free(payload);
                return -1;
            }
            received += (size_t)n;
        }
        
        // Unmask if needed
        if (masked) {
            for (size_t i = 0; i < payload_len; i++) {
                payload[i] ^= mask[i % 4];
            }
        }
        
        payload[payload_len] = '\0';
        
        // 处理消息
        if (opcode == WS_OPCODE_TEXT) {
            ws_handle_message(payload, (size_t)payload_len);
        } else if (opcode == WS_OPCODE_CLOSE) {
            log_info("收到WebSocket CLOSE帧");
            free(payload);
            return -1;
        } else if (opcode == WS_OPCODE_PING) {
            ws_send_frame(WS_OPCODE_PONG, (uint8_t*)payload, (size_t)payload_len);
        }
        
        free(payload);
    }
    
    return 0;
}

/**
 * @brief 处理接收到的消息
 */
static void ws_handle_message(const char *msg, size_t len)
{
    (void)len;
    
    log_debug("收到WebSocket消息: %s", msg);
    
    // 使用外部命令处理器
    extern int ws_command_handler_process(const char *json_str);
    ws_command_handler_process(msg);
}

/**
 * @brief 发送JSON消息
 */
int ws_client_send_json(const char *json_str)
{
    if (!json_str) return -1;
    
    if (g_ws_ctx.state < WS_STATE_CONNECTED) {
        return -1;
    }
    
    return ws_send_frame(WS_OPCODE_TEXT, (const uint8_t*)json_str, strlen(json_str));
}

/**
 * @brief 发送二进制消息
 */
int ws_client_send_binary(const uint8_t *data, size_t len)
{
    if (!data || len == 0) return -1;
    
    if (g_ws_ctx.state < WS_STATE_CONNECTED) {
        return -1;
    }
    
    return ws_send_frame(WS_OPCODE_BINARY, data, len);
}

/**
 * @brief 上报CAN帧
 */
void ws_client_report_can_frame(int channel, const char *frame_text)
{
    if (!frame_text) return;
    struct timeval tv;
    double timestamp_s;

    gettimeofday(&tv, NULL);
    timestamp_s = (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);
    
    pthread_mutex_lock(&g_ws_ctx.can_buffer_mutex);
    
    if (g_ws_ctx.can_frame_count < g_ws_ctx.can_frame_capacity) {
        ws_can_frame_entry_t *entry = &g_ws_ctx.can_frame_buffer[g_ws_ctx.can_frame_count];
        entry->line = strdup(frame_text);
        entry->channel = channel;
        entry->timestamp_s = timestamp_s;
        entry->seq = ++g_ws_ctx.next_can_seq;
        g_ws_ctx.can_frame_count++;
    }
    
    pthread_mutex_unlock(&g_ws_ctx.can_buffer_mutex);
}

/**
 * @brief 刷新CAN帧缓冲
 */
static void ws_flush_can_frames(void)
{
    pthread_mutex_lock(&g_ws_ctx.can_buffer_mutex);
    
    if (g_ws_ctx.can_frame_count == 0 || g_ws_ctx.state < WS_STATE_CONNECTED) {
        pthread_mutex_unlock(&g_ws_ctx.can_buffer_mutex);
        return;
    }
    
    // 构建包含 seq/timestamp 的 JSON 批量消息，便于服务端去重与补齐
    char *batch_msg = (char*)malloc((size_t)g_ws_ctx.can_frame_count * 512 + 256);
    if (!batch_msg) {
        pthread_mutex_unlock(&g_ws_ctx.can_buffer_mutex);
        return;
    }
    
    strcpy(batch_msg, "{\"event\":\"can_frames\",\"data\":{\"lines\":[");
    for (int i = 0; i < g_ws_ctx.can_frame_count; i++) {
        char escaped_line[384];
        ws_can_frame_entry_t *entry = &g_ws_ctx.can_frame_buffer[i];

        json_escape_string(entry->line ? entry->line : "", escaped_line, sizeof(escaped_line));
        if (i > 0) strcat(batch_msg, ",");
        strcat(batch_msg, "\"");
        strcat(batch_msg, escaped_line);
        strcat(batch_msg, "\"");
    }
    strcat(batch_msg, "],\"frames\":[");
    for (int i = 0; i < g_ws_ctx.can_frame_count; i++) {
        char escaped_line[384];
        char frame_json[640];
        ws_can_frame_entry_t *entry = &g_ws_ctx.can_frame_buffer[i];

        json_escape_string(entry->line ? entry->line : "", escaped_line, sizeof(escaped_line));
        snprintf(frame_json, sizeof(frame_json),
                 "%s{\"line\":\"%s\",\"channel\":%d,\"timestamp\":%.6f,\"ts_ms\":%" PRIu64 ",\"seq\":%" PRIu64 "}",
                 (i > 0) ? "," : "",
                 escaped_line,
                 entry->channel,
                 entry->timestamp_s,
                 (uint64_t)(entry->timestamp_s * 1000.0),
                 entry->seq);
        strcat(batch_msg, frame_json);
        free(entry->line);
        memset(entry, 0, sizeof(*entry));
    }
    strcat(batch_msg, "]}}");
    
    ws_send_frame(WS_OPCODE_TEXT, (const uint8_t*)batch_msg, strlen(batch_msg));
    
    free(batch_msg);
    g_ws_ctx.can_frame_count = 0;
    
    pthread_mutex_unlock(&g_ws_ctx.can_buffer_mutex);
}

static size_t json_escape_string(const char *src, char *dst, size_t dst_size)
{
    size_t out = 0;

    if (!dst || dst_size == 0) {
        return 0;
    }

    if (!src) {
        dst[0] = '\0';
        return 0;
    }

    while (*src && out + 2 < dst_size) {
        unsigned char ch = (unsigned char)(*src++);
        if (ch == '\\' || ch == '"') {
            if (out + 2 >= dst_size) break;
            dst[out++] = '\\';
            dst[out++] = (char)ch;
        } else if (ch == '\n') {
            dst[out++] = '\\';
            dst[out++] = 'n';
        } else if (ch == '\r') {
            dst[out++] = '\\';
            dst[out++] = 'r';
        } else if (ch == '\t') {
            dst[out++] = '\\';
            dst[out++] = 't';
        } else {
            dst[out++] = (char)ch;
        }
    }

    dst[out] = '\0';
    return out;
}

/**
 * @brief 上报事件
 */
void ws_client_publish_event(const char *event_type, const char *payload)
{
    if (!event_type || !payload) return;
    
    if (g_ws_ctx.state < WS_STATE_CONNECTED) {
        return;
    }
    
    char msg[2048];
    snprintf(msg, sizeof(msg), "{\"event\":\"%s\",\"data\":%s}", event_type, payload);
    
    ws_client_send_json(msg);
}

