#include "mqtt_client.h"
#include "ws_command_handler.h"

#include "../utils/logger.h"
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MQTT_PKT_CONNECT     0x10
#define MQTT_PKT_CONNACK     0x20
#define MQTT_PKT_PUBLISH     0x30
#define MQTT_PKT_PUBACK      0x40
#define MQTT_PKT_SUBSCRIBE   0x82
#define MQTT_PKT_SUBACK      0x90
#define MQTT_PKT_PINGREQ     0xC0
#define MQTT_PKT_PINGRESP    0xD0
#define MQTT_PKT_DISCONNECT  0xE0

typedef struct {
    char *line;
    int channel;
    double timestamp_s;
    uint64_t seq;
} mqtt_can_frame_entry_t;

typedef struct {
    mqtt_config_t config;
    mqtt_state_callback_t state_callback;
    void *state_callback_user_data;
    bool initialized;
    bool connected;
    bool thread_running;
    bool should_stop;
    int socket_fd;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_mutex_t send_mutex;
    pthread_mutex_t can_buffer_mutex;
    mqtt_can_frame_entry_t *can_frame_buffer;
    int can_frame_count;
    int can_frame_capacity;
    uint64_t last_can_flush_ms;
    uint64_t last_rx_ms;
    uint64_t last_tx_ms;
    uint64_t last_ping_ms;
    uint64_t next_can_seq;
    uint16_t next_packet_id;
    char device_id[128];
    char topic_prefix[128];
    char topic_status[256];
    char topic_hardware[256];
    char topic_can_raw[256];
    char topic_event[256];
    char topic_cmd_request[256];
    char topic_cmd_reply[256];
} mqtt_client_ctx_t;

static mqtt_client_ctx_t g_mqtt_ctx = {
    .socket_fd = -1,
};

static void mqtt_generate_device_id(char *buf, size_t buf_size);
static void mqtt_build_topics(void);
static uint64_t mqtt_now_ms(void);
static uint16_t mqtt_next_packet_id(void);
static int mqtt_write_utf8(uint8_t *dst, size_t dst_size, const char *src);
static int mqtt_encode_remaining_length(uint8_t *dst, size_t dst_size, size_t value);
static int mqtt_send_all(const uint8_t *data, size_t len);
static int mqtt_send_packet(uint8_t first_byte, const uint8_t *body, size_t body_len);
static int mqtt_send_connect_packet(void);
static int mqtt_send_subscribe_packet(const char *topic, int qos);
static int mqtt_send_puback(uint16_t packet_id);
static int mqtt_send_pingreq(void);
static int mqtt_send_disconnect_packet(void);
static int mqtt_publish_topic(const char *topic, const char *payload, int qos, bool retain);
static void mqtt_publish_status(bool connected);
static void mqtt_publish_device_id_event(void);
static int mqtt_connect_socket(void);
static void mqtt_disconnect_socket(bool notify);
static int mqtt_read_packet(uint8_t *first_byte, uint8_t *payload, size_t payload_size, size_t *payload_len);
static int mqtt_handle_packet(uint8_t first_byte, const uint8_t *payload, size_t payload_len);
static void mqtt_flush_can_frames(void);
static void *mqtt_client_thread(void *arg);
static size_t mqtt_json_escape_string(const char *src, char *dst, size_t dst_size);
static void mqtt_notify_state(bool connected);

static uint64_t mqtt_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

static void mqtt_generate_device_id(char *buf, size_t buf_size)
{
    char hostname[128] = {0};
    unsigned long hash = 5381;

    if (!buf || buf_size == 0) {
        return;
    }

    gethostname(hostname, sizeof(hostname) - 1);
    if (hostname[0] == '\0') {
        strncpy(hostname, "lvgl-device", sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
    }

    for (const char *p = hostname; *p; ++p) {
        hash = ((hash << 5) + hash) + (unsigned char)(*p);
    }

    snprintf(buf, buf_size, "lvgl-%08lx", hash & 0xFFFFFFFFul);
}

static void mqtt_build_topics(void)
{
    const char *prefix = g_mqtt_ctx.config.topic_prefix[0] ? g_mqtt_ctx.config.topic_prefix : "app_lvgl";

    strncpy(g_mqtt_ctx.topic_prefix, prefix, sizeof(g_mqtt_ctx.topic_prefix) - 1);
    g_mqtt_ctx.topic_prefix[sizeof(g_mqtt_ctx.topic_prefix) - 1] = '\0';

    snprintf(g_mqtt_ctx.topic_status, sizeof(g_mqtt_ctx.topic_status),
             "%s/device/%s/status", g_mqtt_ctx.topic_prefix, g_mqtt_ctx.device_id);
    snprintf(g_mqtt_ctx.topic_hardware, sizeof(g_mqtt_ctx.topic_hardware),
             "%s/device/%s/hardware", g_mqtt_ctx.topic_prefix, g_mqtt_ctx.device_id);
    snprintf(g_mqtt_ctx.topic_can_raw, sizeof(g_mqtt_ctx.topic_can_raw),
             "%s/device/%s/can/raw", g_mqtt_ctx.topic_prefix, g_mqtt_ctx.device_id);
    snprintf(g_mqtt_ctx.topic_event, sizeof(g_mqtt_ctx.topic_event),
             "%s/device/%s/event", g_mqtt_ctx.topic_prefix, g_mqtt_ctx.device_id);
    snprintf(g_mqtt_ctx.topic_cmd_request, sizeof(g_mqtt_ctx.topic_cmd_request),
             "%s/device/%s/cmd/request", g_mqtt_ctx.topic_prefix, g_mqtt_ctx.device_id);
    snprintf(g_mqtt_ctx.topic_cmd_reply, sizeof(g_mqtt_ctx.topic_cmd_reply),
             "%s/device/%s/cmd/reply", g_mqtt_ctx.topic_prefix, g_mqtt_ctx.device_id);
}

static uint16_t mqtt_next_packet_id(void)
{
    pthread_mutex_lock(&g_mqtt_ctx.mutex);
    g_mqtt_ctx.next_packet_id++;
    if (g_mqtt_ctx.next_packet_id == 0) {
        g_mqtt_ctx.next_packet_id = 1;
    }
    uint16_t packet_id = g_mqtt_ctx.next_packet_id;
    pthread_mutex_unlock(&g_mqtt_ctx.mutex);
    return packet_id;
}

static int mqtt_write_utf8(uint8_t *dst, size_t dst_size, const char *src)
{
    size_t len = src ? strlen(src) : 0;

    if (!dst || dst_size < len + 2 || len > 0xFFFFu) {
        return -1;
    }

    dst[0] = (uint8_t)((len >> 8) & 0xFFu);
    dst[1] = (uint8_t)(len & 0xFFu);
    if (len > 0) {
        memcpy(dst + 2, src, len);
    }
    return (int)(len + 2);
}

static int mqtt_encode_remaining_length(uint8_t *dst, size_t dst_size, size_t value)
{
    size_t idx = 0;

    do {
        uint8_t byte = (uint8_t)(value % 128u);
        value /= 128u;
        if (value > 0) {
            byte |= 0x80u;
        }
        if (!dst || idx >= dst_size) {
            return -1;
        }
        dst[idx++] = byte;
    } while (value > 0);

    return (int)idx;
}

static int mqtt_send_all(const uint8_t *data, size_t len)
{
    size_t offset = 0;

    while (offset < len) {
        ssize_t n = send(g_mqtt_ctx.socket_fd, data + offset, len - offset, 0);
        if (n <= 0) {
            return -1;
        }
        offset += (size_t)n;
    }

    g_mqtt_ctx.last_tx_ms = mqtt_now_ms();
    return 0;
}

static int mqtt_send_packet(uint8_t first_byte, const uint8_t *body, size_t body_len)
{
    uint8_t header[5];
    int header_len;
    int ret = -1;

    if (g_mqtt_ctx.socket_fd < 0) {
        return -1;
    }

    header[0] = first_byte;
    header_len = mqtt_encode_remaining_length(header + 1, sizeof(header) - 1, body_len);
    if (header_len < 0) {
        return -1;
    }

    pthread_mutex_lock(&g_mqtt_ctx.send_mutex);
    if (mqtt_send_all(header, (size_t)header_len + 1u) == 0) {
        ret = 0;
        if (body && body_len > 0) {
            ret = mqtt_send_all(body, body_len);
        }
    }
    pthread_mutex_unlock(&g_mqtt_ctx.send_mutex);
    return ret;
}

static int mqtt_send_connect_packet(void)
{
    uint8_t body[1024];
    size_t pos = 0;
    char will_payload[256];
    int wrote;
    uint8_t flags = 0x02; /* clean session */
    int keepalive = g_mqtt_ctx.config.keepalive_s > 0 ? g_mqtt_ctx.config.keepalive_s : 30;

    wrote = mqtt_write_utf8(body + pos, sizeof(body) - pos, "MQTT");
    if (wrote < 0) return -1;
    pos += (size_t)wrote;
    body[pos++] = 0x04; /* MQTT 3.1.1 */

    /* Use retained will so broker can flip the device offline on link loss. */
    flags |= 0x04;  /* will flag */
    flags |= 0x20;  /* will retain */
    flags |= 0x08;  /* will qos=1 */
    if (g_mqtt_ctx.config.username[0]) flags |= 0x80;
    if (g_mqtt_ctx.config.password[0]) flags |= 0x40;
    body[pos++] = flags;
    body[pos++] = (uint8_t)((keepalive >> 8) & 0xFF);
    body[pos++] = (uint8_t)(keepalive & 0xFF);

    wrote = mqtt_write_utf8(body + pos, sizeof(body) - pos, g_mqtt_ctx.device_id);
    if (wrote < 0) return -1;
    pos += (size_t)wrote;

    snprintf(will_payload, sizeof(will_payload),
             "{\"id\":\"%s\",\"connected\":false,\"transport\":\"mqtt\",\"timestamp_ms\":%" PRIu64 "}",
             g_mqtt_ctx.device_id, mqtt_now_ms());
    wrote = mqtt_write_utf8(body + pos, sizeof(body) - pos, g_mqtt_ctx.topic_status);
    if (wrote < 0) return -1;
    pos += (size_t)wrote;
    wrote = mqtt_write_utf8(body + pos, sizeof(body) - pos, will_payload);
    if (wrote < 0) return -1;
    pos += (size_t)wrote;

    if (g_mqtt_ctx.config.username[0]) {
        wrote = mqtt_write_utf8(body + pos, sizeof(body) - pos, g_mqtt_ctx.config.username);
        if (wrote < 0) return -1;
        pos += (size_t)wrote;
    }
    if (g_mqtt_ctx.config.password[0]) {
        wrote = mqtt_write_utf8(body + pos, sizeof(body) - pos, g_mqtt_ctx.config.password);
        if (wrote < 0) return -1;
        pos += (size_t)wrote;
    }

    return mqtt_send_packet(MQTT_PKT_CONNECT, body, pos);
}

static int mqtt_send_subscribe_packet(const char *topic, int qos)
{
    uint8_t body[512];
    size_t pos = 0;
    int wrote;
    uint16_t packet_id = mqtt_next_packet_id();
    int subscribe_qos = (qos > 0) ? 1 : 0;

    body[pos++] = (uint8_t)((packet_id >> 8) & 0xFF);
    body[pos++] = (uint8_t)(packet_id & 0xFF);
    wrote = mqtt_write_utf8(body + pos, sizeof(body) - pos, topic);
    if (wrote < 0) return -1;
    pos += (size_t)wrote;
    body[pos++] = (uint8_t)subscribe_qos;

    return mqtt_send_packet(MQTT_PKT_SUBSCRIBE, body, pos);
}

static int mqtt_send_puback(uint16_t packet_id)
{
    uint8_t body[2];
    body[0] = (uint8_t)((packet_id >> 8) & 0xFF);
    body[1] = (uint8_t)(packet_id & 0xFF);
    return mqtt_send_packet(MQTT_PKT_PUBACK, body, sizeof(body));
}

static int mqtt_send_pingreq(void)
{
    return mqtt_send_packet(MQTT_PKT_PINGREQ, NULL, 0);
}

static int mqtt_send_disconnect_packet(void)
{
    return mqtt_send_packet(MQTT_PKT_DISCONNECT, NULL, 0);
}

static int mqtt_publish_topic(const char *topic, const char *payload, int qos, bool retain)
{
    uint8_t *body = NULL;
    size_t pos = 0;
    int wrote;
    uint8_t first_byte;
    uint16_t packet_id = 0;
    int publish_qos = qos > 0 ? 1 : 0;
    size_t payload_len = payload ? strlen(payload) : 0;
    size_t body_size = (topic ? strlen(topic) : 0) + payload_len + 8;
    int ret = -1;

    if (!topic || !payload || !g_mqtt_ctx.connected) {
        return -1;
    }

    body = (uint8_t *)malloc(body_size);
    if (!body) {
        return -1;
    }

    wrote = mqtt_write_utf8(body + pos, body_size - pos, topic);
    if (wrote < 0) goto done;
    pos += (size_t)wrote;

    if (publish_qos > 0) {
        packet_id = mqtt_next_packet_id();
        if (pos + 2 > body_size) {
            goto done;
        }
        body[pos++] = (uint8_t)((packet_id >> 8) & 0xFF);
        body[pos++] = (uint8_t)(packet_id & 0xFF);
    }

    if (pos + payload_len > body_size) {
        goto done;
    }
    memcpy(body + pos, payload, payload_len);
    pos += payload_len;

    first_byte = (uint8_t)(MQTT_PKT_PUBLISH | (publish_qos << 1));
    if (retain) {
        first_byte |= 0x01;
    }
    ret = mqtt_send_packet(first_byte, body, pos);

done:
    free(body);
    return ret;
}

static void mqtt_publish_status(bool connected)
{
    char payload[256];

    snprintf(payload, sizeof(payload),
             "{\"id\":\"%s\",\"connected\":%s,\"transport\":\"mqtt\",\"timestamp_ms\":%" PRIu64 "}",
             g_mqtt_ctx.device_id,
             connected ? "true" : "false",
             mqtt_now_ms());
    (void)mqtt_publish_topic(g_mqtt_ctx.topic_status, payload, 1, true);
}

static void mqtt_publish_device_id_event(void)
{
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"event\":\"device_id\",\"data\":{\"id\":\"%s\"}}",
             g_mqtt_ctx.device_id);
    (void)mqtt_publish_topic(g_mqtt_ctx.topic_event, payload, 1, false);
}

static int mqtt_connect_socket(void)
{
    struct sockaddr_in server_addr;
    struct hostent *he;

    g_mqtt_ctx.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_mqtt_ctx.socket_fd < 0) {
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_mqtt_ctx.config.port);

    he = gethostbyname(g_mqtt_ctx.config.host);
    if (!he) {
        close(g_mqtt_ctx.socket_fd);
        g_mqtt_ctx.socket_fd = -1;
        return -1;
    }

    memcpy(&server_addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    if (connect(g_mqtt_ctx.socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(g_mqtt_ctx.socket_fd);
        g_mqtt_ctx.socket_fd = -1;
        return -1;
    }

    g_mqtt_ctx.last_rx_ms = mqtt_now_ms();
    g_mqtt_ctx.last_tx_ms = g_mqtt_ctx.last_rx_ms;
    g_mqtt_ctx.last_ping_ms = 0;
    return 0;
}

static void mqtt_notify_state(bool connected)
{
    if (g_mqtt_ctx.state_callback) {
        g_mqtt_ctx.state_callback(
            connected,
            g_mqtt_ctx.config.host,
            g_mqtt_ctx.config.port,
            g_mqtt_ctx.state_callback_user_data);
    }
}

static void mqtt_disconnect_socket(bool notify)
{
    if (g_mqtt_ctx.socket_fd >= 0) {
        close(g_mqtt_ctx.socket_fd);
        g_mqtt_ctx.socket_fd = -1;
    }
    if (g_mqtt_ctx.connected) {
        g_mqtt_ctx.connected = false;
        if (notify) {
            mqtt_notify_state(false);
        }
    }
}

static int mqtt_read_packet(uint8_t *first_byte, uint8_t *payload, size_t payload_size, size_t *payload_len)
{
    uint8_t len_byte = 0;
    size_t remaining = 0;
    size_t multiplier = 1;
    size_t idx = 0;
    ssize_t n;

    if (!first_byte || !payload || !payload_len) {
        return -1;
    }

    n = recv(g_mqtt_ctx.socket_fd, first_byte, 1, 0);
    if (n <= 0) {
        return -1;
    }

    do {
        n = recv(g_mqtt_ctx.socket_fd, &len_byte, 1, 0);
        if (n <= 0) {
            return -1;
        }
        remaining += (size_t)(len_byte & 0x7Fu) * multiplier;
        multiplier *= 128u;
    } while ((len_byte & 0x80u) != 0u && multiplier <= 128u * 128u * 128u);

    if (remaining > payload_size) {
        return -1;
    }

    while (idx < remaining) {
        n = recv(g_mqtt_ctx.socket_fd, payload + idx, remaining - idx, 0);
        if (n <= 0) {
            return -1;
        }
        idx += (size_t)n;
    }

    g_mqtt_ctx.last_rx_ms = mqtt_now_ms();
    *payload_len = remaining;
    return 0;
}

static int mqtt_handle_packet(uint8_t first_byte, const uint8_t *payload, size_t payload_len)
{
    uint8_t packet_type = (uint8_t)(first_byte & 0xF0u);

    if (packet_type == MQTT_PKT_CONNACK) {
        if (payload_len < 2 || payload[1] != 0x00) {
            return -1;
        }
        return 0;
    }

    if (packet_type == MQTT_PKT_SUBACK || packet_type == MQTT_PKT_PINGRESP || packet_type == MQTT_PKT_PUBACK) {
        return 0;
    }

    if (packet_type == MQTT_PKT_PUBLISH) {
        size_t pos = 0;
        uint16_t topic_len;
        char topic[256];
        const uint8_t *msg_payload = NULL;
        size_t msg_payload_len = 0;
        int qos = (int)((first_byte >> 1) & 0x03u);
        uint16_t packet_id = 0;
        char json_buf[2048];

        if (payload_len < 2) {
            return -1;
        }

        topic_len = (uint16_t)(((uint16_t)payload[0] << 8) | payload[1]);
        pos = 2;
        if (pos + topic_len > payload_len || topic_len >= sizeof(topic)) {
            return -1;
        }
        memcpy(topic, payload + pos, topic_len);
        topic[topic_len] = '\0';
        pos += topic_len;

        if (qos > 0) {
            if (pos + 2 > payload_len) {
                return -1;
            }
            packet_id = (uint16_t)(((uint16_t)payload[pos] << 8) | payload[pos + 1]);
            pos += 2;
        }

        msg_payload = payload + pos;
        msg_payload_len = payload_len - pos;
        if (msg_payload_len >= sizeof(json_buf)) {
            msg_payload_len = sizeof(json_buf) - 1;
        }
        memcpy(json_buf, msg_payload, msg_payload_len);
        json_buf[msg_payload_len] = '\0';

        if (qos == 1 && packet_id != 0) {
            (void)mqtt_send_puback(packet_id);
        }

        if (strcmp(topic, g_mqtt_ctx.topic_cmd_request) == 0) {
            ws_command_handler_process(json_buf);
        }
        return 0;
    }

    return 0;
}

static void mqtt_flush_can_frames(void)
{
    char *json = NULL;
    size_t capacity;
    size_t pos = 0;

    pthread_mutex_lock(&g_mqtt_ctx.can_buffer_mutex);

    if (g_mqtt_ctx.can_frame_count == 0 || !g_mqtt_ctx.connected) {
        pthread_mutex_unlock(&g_mqtt_ctx.can_buffer_mutex);
        return;
    }

    capacity = (size_t)g_mqtt_ctx.can_frame_count * 640u + 256u;
    json = (char *)malloc(capacity);
    if (!json) {
        pthread_mutex_unlock(&g_mqtt_ctx.can_buffer_mutex);
        return;
    }

    pos += (size_t)snprintf(json + pos, capacity - pos, "{\"data\":{\"lines\":[");
    for (int i = 0; i < g_mqtt_ctx.can_frame_count; ++i) {
        char escaped[384];
        mqtt_can_frame_entry_t *entry = &g_mqtt_ctx.can_frame_buffer[i];
        mqtt_json_escape_string(entry->line ? entry->line : "", escaped, sizeof(escaped));
        pos += (size_t)snprintf(json + pos, capacity - pos, "%s\"%s\"",
                                (i > 0) ? "," : "", escaped);
    }
    pos += (size_t)snprintf(json + pos, capacity - pos, "],\"frames\":[");
    for (int i = 0; i < g_mqtt_ctx.can_frame_count; ++i) {
        char escaped[384];
        mqtt_can_frame_entry_t *entry = &g_mqtt_ctx.can_frame_buffer[i];
        mqtt_json_escape_string(entry->line ? entry->line : "", escaped, sizeof(escaped));
        pos += (size_t)snprintf(json + pos, capacity - pos,
                                "%s{\"line\":\"%s\",\"channel\":%d,\"timestamp\":%.6f,"
                                "\"ts_ms\":%" PRIu64 ",\"seq\":%" PRIu64 "}",
                                (i > 0) ? "," : "",
                                escaped,
                                entry->channel,
                                entry->timestamp_s,
                                (uint64_t)(entry->timestamp_s * 1000.0),
                                entry->seq);
        free(entry->line);
        memset(entry, 0, sizeof(*entry));
    }
    pos += (size_t)snprintf(json + pos, capacity - pos, "]}}");
    g_mqtt_ctx.can_frame_count = 0;

    pthread_mutex_unlock(&g_mqtt_ctx.can_buffer_mutex);

    (void)mqtt_publish_topic(g_mqtt_ctx.topic_can_raw, json, g_mqtt_ctx.config.qos, false);
    free(json);
}

static size_t mqtt_json_escape_string(const char *src, char *dst, size_t dst_size)
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

static void *mqtt_client_thread(void *arg)
{
    uint8_t payload[4096];
    (void)arg;

    while (!g_mqtt_ctx.should_stop) {
        if (!g_mqtt_ctx.connected) {
            uint8_t first_byte = 0;
            size_t payload_len = 0;

            if (mqtt_connect_socket() == 0 &&
                mqtt_send_connect_packet() == 0 &&
                mqtt_read_packet(&first_byte, payload, sizeof(payload), &payload_len) == 0 &&
                mqtt_handle_packet(first_byte, payload, payload_len) == 0 &&
                mqtt_send_subscribe_packet(g_mqtt_ctx.topic_cmd_request, g_mqtt_ctx.config.qos) == 0) {
                g_mqtt_ctx.connected = true;
                mqtt_notify_state(true);
                mqtt_publish_status(true);
                mqtt_publish_device_id_event();
                log_info("MQTT已连接: %s:%u topic=%s",
                         g_mqtt_ctx.config.host,
                         (unsigned)g_mqtt_ctx.config.port,
                         g_mqtt_ctx.topic_cmd_request);
            } else {
                mqtt_disconnect_socket(false);
                usleep(4000 * 1000);
                continue;
            }
        }

        {
            fd_set rfds;
            struct timeval tv;
            int sel;
            uint8_t first_byte = 0;
            size_t payload_len = 0;
            uint64_t now_ms = mqtt_now_ms();
            uint64_t keepalive_ms = (uint64_t)((g_mqtt_ctx.config.keepalive_s > 0 ? g_mqtt_ctx.config.keepalive_s : 30) * 1000);

            if (g_mqtt_ctx.can_frame_count > 0 &&
                ((now_ms - g_mqtt_ctx.last_can_flush_ms) >= 200 || g_mqtt_ctx.can_frame_count >= 50)) {
                mqtt_flush_can_frames();
                g_mqtt_ctx.last_can_flush_ms = now_ms;
            }

            if ((now_ms - g_mqtt_ctx.last_tx_ms) >= (keepalive_ms / 2u) &&
                (now_ms - g_mqtt_ctx.last_ping_ms) >= (keepalive_ms / 2u)) {
                if (mqtt_send_pingreq() < 0) {
                    mqtt_disconnect_socket(true);
                    continue;
                }
                g_mqtt_ctx.last_ping_ms = now_ms;
            }

            FD_ZERO(&rfds);
            FD_SET(g_mqtt_ctx.socket_fd, &rfds);
            tv.tv_sec = 0;
            tv.tv_usec = 200 * 1000;
            sel = select(g_mqtt_ctx.socket_fd + 1, &rfds, NULL, NULL, &tv);
            if (sel < 0) {
                mqtt_disconnect_socket(true);
                continue;
            }
            if (sel == 0) {
                continue;
            }

            if (mqtt_read_packet(&first_byte, payload, sizeof(payload), &payload_len) < 0 ||
                mqtt_handle_packet(first_byte, payload, payload_len) < 0) {
                mqtt_disconnect_socket(true);
                continue;
            }
        }
    }

    mqtt_disconnect_socket(true);
    g_mqtt_ctx.thread_running = false;
    return NULL;
}

int mqtt_client_init(const mqtt_config_t *config)
{
    if (!config) {
        log_error("mqtt_client_init: config is NULL");
        return -1;
    }

    memset(&g_mqtt_ctx, 0, sizeof(g_mqtt_ctx));
    memcpy(&g_mqtt_ctx.config, config, sizeof(g_mqtt_ctx.config));
    if (g_mqtt_ctx.config.client_id[0]) {
        strncpy(g_mqtt_ctx.device_id, g_mqtt_ctx.config.client_id, sizeof(g_mqtt_ctx.device_id) - 1);
    } else {
        mqtt_generate_device_id(g_mqtt_ctx.device_id, sizeof(g_mqtt_ctx.device_id));
    }
    mqtt_build_topics();
    pthread_mutex_init(&g_mqtt_ctx.mutex, NULL);
    pthread_mutex_init(&g_mqtt_ctx.send_mutex, NULL);
    pthread_mutex_init(&g_mqtt_ctx.can_buffer_mutex, NULL);
    g_mqtt_ctx.can_frame_capacity = 1000;
    g_mqtt_ctx.can_frame_buffer = (mqtt_can_frame_entry_t *)calloc(
        g_mqtt_ctx.can_frame_capacity, sizeof(mqtt_can_frame_entry_t));
    g_mqtt_ctx.last_can_flush_ms = mqtt_now_ms();
    g_mqtt_ctx.socket_fd = -1;
    g_mqtt_ctx.initialized = true;

    if (g_mqtt_ctx.config.use_tls) {
        log_warn("当前 MQTT 客户端暂不支持 TLS，将按明文 TCP 连接");
    }

    log_info("MQTT客户端配置已加载: %s:%u client_id=%s request_topic=%s",
             g_mqtt_ctx.config.host,
             (unsigned)g_mqtt_ctx.config.port,
             g_mqtt_ctx.device_id,
             g_mqtt_ctx.topic_cmd_request);
    return 0;
}

void mqtt_client_deinit(void)
{
    if (!g_mqtt_ctx.initialized) {
        return;
    }

    mqtt_client_stop();
    pthread_mutex_lock(&g_mqtt_ctx.can_buffer_mutex);
    if (g_mqtt_ctx.can_frame_buffer) {
        for (int i = 0; i < g_mqtt_ctx.can_frame_count; ++i) {
            free(g_mqtt_ctx.can_frame_buffer[i].line);
        }
        free(g_mqtt_ctx.can_frame_buffer);
        g_mqtt_ctx.can_frame_buffer = NULL;
    }
    pthread_mutex_unlock(&g_mqtt_ctx.can_buffer_mutex);
    pthread_mutex_destroy(&g_mqtt_ctx.can_buffer_mutex);
    pthread_mutex_destroy(&g_mqtt_ctx.send_mutex);
    pthread_mutex_destroy(&g_mqtt_ctx.mutex);
    memset(&g_mqtt_ctx, 0, sizeof(g_mqtt_ctx));
    g_mqtt_ctx.socket_fd = -1;
}

int mqtt_client_start(void)
{
    if (!g_mqtt_ctx.initialized) {
        return -1;
    }
    if (g_mqtt_ctx.thread_running) {
        return 0;
    }

    g_mqtt_ctx.should_stop = false;
    g_mqtt_ctx.thread_running = true;
    if (pthread_create(&g_mqtt_ctx.thread, NULL, mqtt_client_thread, NULL) != 0) {
        g_mqtt_ctx.thread_running = false;
        return -1;
    }
    return 0;
}

void mqtt_client_stop(void)
{
    if (!g_mqtt_ctx.thread_running) {
        return;
    }

    g_mqtt_ctx.should_stop = true;
    if (g_mqtt_ctx.connected) {
        mqtt_publish_status(false);
        (void)mqtt_send_disconnect_packet();
    }
    pthread_join(g_mqtt_ctx.thread, NULL);
}

bool mqtt_client_is_connected(void)
{
    return g_mqtt_ctx.connected;
}

void mqtt_client_register_state_callback(mqtt_state_callback_t callback, void *user_data)
{
    g_mqtt_ctx.state_callback = callback;
    g_mqtt_ctx.state_callback_user_data = user_data;
}

int mqtt_client_send_json(const char *json_str)
{
    if (!json_str) {
        return -1;
    }
    return mqtt_publish_topic(g_mqtt_ctx.topic_event, json_str, g_mqtt_ctx.config.qos, false);
}

int mqtt_client_send_request_json(const char *json_str)
{
    if (!json_str) {
        return -1;
    }
    return mqtt_publish_topic(g_mqtt_ctx.topic_cmd_request, json_str, g_mqtt_ctx.config.qos, false);
}

int mqtt_client_send_reply_json(const char *json_str)
{
    if (!json_str) {
        return -1;
    }
    return mqtt_publish_topic(g_mqtt_ctx.topic_cmd_reply, json_str, g_mqtt_ctx.config.qos, false);
}

void mqtt_client_report_can_frame(int channel, const char *frame_text)
{
    struct timeval tv;
    mqtt_can_frame_entry_t *entry = NULL;

    if (!frame_text) {
        return;
    }

    gettimeofday(&tv, NULL);
    pthread_mutex_lock(&g_mqtt_ctx.can_buffer_mutex);
    if (g_mqtt_ctx.can_frame_count < g_mqtt_ctx.can_frame_capacity) {
        entry = &g_mqtt_ctx.can_frame_buffer[g_mqtt_ctx.can_frame_count++];
        entry->line = strdup(frame_text);
        entry->channel = channel;
        entry->timestamp_s = (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);
        entry->seq = ++g_mqtt_ctx.next_can_seq;
    }
    pthread_mutex_unlock(&g_mqtt_ctx.can_buffer_mutex);
}

void mqtt_client_publish_event(const char *event_type, const char *payload)
{
    char json[4096];

    if (!event_type || !payload) {
        return;
    }

    if (strcmp(event_type, "hardware_status") == 0) {
        snprintf(json, sizeof(json), "{\"data\":%s}", payload);
        (void)mqtt_publish_topic(g_mqtt_ctx.topic_hardware, json, 1, false);
        return;
    }

    snprintf(json, sizeof(json), "{\"event\":\"%s\",\"data\":%s}", event_type, payload);
    (void)mqtt_publish_topic(g_mqtt_ctx.topic_event, json, g_mqtt_ctx.config.qos, false);
}

const char *mqtt_client_get_device_id(void)
{
    return g_mqtt_ctx.device_id;
}

int mqtt_client_get_server_info(char *host, size_t host_size, uint16_t *port)
{
    if (!host || host_size == 0 || !port) {
        return -1;
    }

    strncpy(host, g_mqtt_ctx.config.host, host_size - 1);
    host[host_size - 1] = '\0';
    *port = g_mqtt_ctx.config.port;
    return 0;
}

int mqtt_client_publish(const char *topic, const char *payload, int qos, bool retain)
{
    if (!topic || !payload) {
        return -1;
    }
    return mqtt_publish_topic(topic, payload, qos, retain);
}

const char *mqtt_client_get_topic_prefix(void)
{
    return g_mqtt_ctx.topic_prefix;
}
