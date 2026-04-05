#include "remote_transport.h"

#include "mqtt_client.h"
#include "ws_client.h"
#include "../utils/app_config.h"
#include "../utils/logger.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    remote_transport_config_t config;
    remote_transport_state_callback_t state_callback;
    void *state_callback_user_data;
    bool initialized;
} remote_transport_ctx_t;

static remote_transport_ctx_t g_remote_transport_ctx;

static void trim_in_place(char *text)
{
    char *start;
    char *end;

    if (!text || !text[0]) {
        return;
    }

    start = text;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
}

static int is_host_list(const char *host)
{
    return host && strchr(host, ',') != NULL;
}

static int try_connect_host_port(const char *host, uint16_t port, int timeout_ms)
{
    char port_text[16];
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *ai;
    int rc;

    if (!host || !host[0] || port == 0) {
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
    rc = getaddrinfo(host, port_text, &hints, &result);
    if (rc != 0) {
        return -1;
    }

    for (ai = result; ai; ai = ai->ai_next) {
        int fd;
        int flags;
        int so_error = 0;
        socklen_t so_error_len = sizeof(so_error);
        fd_set write_fds;
        struct timeval tv;

        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }

        flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }

        rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc == 0) {
            close(fd);
            freeaddrinfo(result);
            return 0;
        }
        if (errno != EINPROGRESS) {
            close(fd);
            continue;
        }

        FD_ZERO(&write_fds);
        FD_SET(fd, &write_fds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        rc = select(fd + 1, NULL, &write_fds, NULL, &tv);
        if (rc > 0 &&
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) == 0 &&
            so_error == 0) {
            close(fd);
            freeaddrinfo(result);
            return 0;
        }

        close(fd);
    }

    freeaddrinfo(result);
    return -1;
}

static void resolve_host_candidates(const char *host_value,
                                    uint16_t port,
                                    char *resolved_host,
                                    size_t resolved_host_size)
{
    char host_copy[128];
    char *saveptr = NULL;
    char *token;
    char first_candidate[128] = {0};

    if (!resolved_host || resolved_host_size == 0) {
        return;
    }
    resolved_host[0] = '\0';

    if (!host_value || !host_value[0]) {
        return;
    }

    strncpy(host_copy, host_value, sizeof(host_copy) - 1);
    host_copy[sizeof(host_copy) - 1] = '\0';

    if (!is_host_list(host_copy)) {
        strncpy(resolved_host, host_copy, resolved_host_size - 1);
        resolved_host[resolved_host_size - 1] = '\0';
        return;
    }

    token = strtok_r(host_copy, ",", &saveptr);
    while (token) {
        trim_in_place(token);
        if (token[0]) {
            if (!first_candidate[0]) {
                strncpy(first_candidate, token, sizeof(first_candidate) - 1);
                first_candidate[sizeof(first_candidate) - 1] = '\0';
            }
            if (try_connect_host_port(token, port, 800) == 0) {
                strncpy(resolved_host, token, resolved_host_size - 1);
                resolved_host[resolved_host_size - 1] = '\0';
                log_info("selected reachable server host: %s:%u", resolved_host, (unsigned)port);
                return;
            }
        }
        token = strtok_r(NULL, ",", &saveptr);
    }

    if (first_candidate[0]) {
        strncpy(resolved_host, first_candidate, resolved_host_size - 1);
        resolved_host[resolved_host_size - 1] = '\0';
        log_warn("no reachable host found in list, fallback to first candidate: %s:%u",
                 resolved_host, (unsigned)port);
    }
}


static void forward_state_callback(bool connected, const char *host, uint16_t port, void *user_data)
{
    (void)user_data;

    if (g_remote_transport_ctx.state_callback) {
        g_remote_transport_ctx.state_callback(
            connected,
            host,
            port,
            g_remote_transport_ctx.state_callback_user_data);
    }
}

const char *remote_transport_mode_to_string(remote_transport_mode_t mode)
{
    return (mode == REMOTE_TRANSPORT_MODE_MQTT) ? "mqtt" : "websocket";
}

remote_transport_mode_t remote_transport_get_mode(void)
{
    return g_remote_transport_ctx.config.mode;
}

int remote_transport_init(const remote_transport_config_t *config)
{
    if (!config) {
        log_error("remote_transport_init: config is NULL");
        return -1;
    }

    memset(&g_remote_transport_ctx, 0, sizeof(g_remote_transport_ctx));
    memcpy(&g_remote_transport_ctx.config, config, sizeof(g_remote_transport_ctx.config));

    if (config->mode == REMOTE_TRANSPORT_MODE_MQTT) {
        mqtt_config_t mqtt_cfg;
        memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));
        strncpy(mqtt_cfg.host, config->mqtt_host, sizeof(mqtt_cfg.host) - 1);
        mqtt_cfg.port = config->mqtt_port;
        strncpy(mqtt_cfg.client_id, config->mqtt_client_id, sizeof(mqtt_cfg.client_id) - 1);
        strncpy(mqtt_cfg.username, config->mqtt_username, sizeof(mqtt_cfg.username) - 1);
        strncpy(mqtt_cfg.password, config->mqtt_password, sizeof(mqtt_cfg.password) - 1);
        mqtt_cfg.keepalive_s = config->mqtt_keepalive_s;
        mqtt_cfg.qos = config->mqtt_qos;
        strncpy(mqtt_cfg.topic_prefix, config->mqtt_topic_prefix, sizeof(mqtt_cfg.topic_prefix) - 1);
        mqtt_cfg.use_tls = config->mqtt_use_tls;
        if (mqtt_client_init(&mqtt_cfg) < 0) {
            return -1;
        }
        mqtt_client_register_state_callback(forward_state_callback, NULL);
    } else {
        ws_config_t ws_cfg;
        memset(&ws_cfg, 0, sizeof(ws_cfg));
        strncpy(ws_cfg.host, config->ws_host, sizeof(ws_cfg.host) - 1);
        ws_cfg.port = config->ws_port;
        strncpy(ws_cfg.path, config->ws_path, sizeof(ws_cfg.path) - 1);
        ws_cfg.use_ssl = config->ws_use_ssl;
        ws_cfg.reconnect_interval_ms = config->ws_reconnect_interval_ms;
        ws_cfg.keepalive_interval_s = config->ws_keepalive_interval_s;
        if (ws_client_init(&ws_cfg) < 0) {
            return -1;
        }
        ws_client_register_state_callback(forward_state_callback, NULL);
    }

    g_remote_transport_ctx.initialized = true;
    log_info("远程传输层初始化完成: mode=%s", remote_transport_mode_to_string(config->mode));
    return 0;
}

int remote_transport_init_from_app_config(void)
{
    remote_transport_config_t config;
    char resolved_ws_host[sizeof(config.ws_host)];
    char resolved_mqtt_host[sizeof(config.mqtt_host)];
    memset(&config, 0, sizeof(config));
    memset(resolved_ws_host, 0, sizeof(resolved_ws_host));
    memset(resolved_mqtt_host, 0, sizeof(resolved_mqtt_host));

    config.mode = (g_app_config.transport_mode == APP_TRANSPORT_MQTT)
        ? REMOTE_TRANSPORT_MODE_MQTT
        : REMOTE_TRANSPORT_MODE_WEBSOCKET;

    resolve_host_candidates(g_app_config.ws_host, g_app_config.ws_port,
                            resolved_ws_host, sizeof(resolved_ws_host));
    resolve_host_candidates(g_app_config.mqtt_host, g_app_config.mqtt_port,
                            resolved_mqtt_host, sizeof(resolved_mqtt_host));

    strncpy(config.ws_host,
            resolved_ws_host[0] ? resolved_ws_host : g_app_config.ws_host,
            sizeof(config.ws_host) - 1);
    config.ws_port = g_app_config.ws_port;
    strncpy(config.ws_path, g_app_config.ws_path, sizeof(config.ws_path) - 1);
    config.ws_use_ssl = g_app_config.ws_use_ssl;
    config.ws_reconnect_interval_ms = (int)g_app_config.ws_reconnect_interval_ms;
    config.ws_keepalive_interval_s = (int)g_app_config.ws_keepalive_interval_s;

    strncpy(config.mqtt_host,
            resolved_mqtt_host[0] ? resolved_mqtt_host : g_app_config.mqtt_host,
            sizeof(config.mqtt_host) - 1);
    config.mqtt_port = g_app_config.mqtt_port;
    strncpy(config.mqtt_client_id, g_app_config.mqtt_client_id, sizeof(config.mqtt_client_id) - 1);
    strncpy(config.mqtt_username, g_app_config.mqtt_username, sizeof(config.mqtt_username) - 1);
    strncpy(config.mqtt_password, g_app_config.mqtt_password, sizeof(config.mqtt_password) - 1);
    config.mqtt_keepalive_s = (int)g_app_config.mqtt_keepalive_s;
    config.mqtt_qos = (int)g_app_config.mqtt_qos;
    strncpy(config.mqtt_topic_prefix, g_app_config.mqtt_topic_prefix, sizeof(config.mqtt_topic_prefix) - 1);
    config.mqtt_use_tls = g_app_config.mqtt_use_tls;

    return remote_transport_init(&config);
}

void remote_transport_deinit(void)
{
    if (!g_remote_transport_ctx.initialized) {
        return;
    }

    if (g_remote_transport_ctx.config.mode == REMOTE_TRANSPORT_MODE_MQTT) {
        mqtt_client_deinit();
    } else {
        ws_client_deinit();
    }

    memset(&g_remote_transport_ctx, 0, sizeof(g_remote_transport_ctx));
}

int remote_transport_start(void)
{
    if (!g_remote_transport_ctx.initialized) {
        return -1;
    }

    if (g_remote_transport_ctx.config.mode == REMOTE_TRANSPORT_MODE_MQTT) {
        return mqtt_client_start();
    }
    return ws_client_start();
}

void remote_transport_stop(void)
{
    if (!g_remote_transport_ctx.initialized) {
        return;
    }

    if (g_remote_transport_ctx.config.mode == REMOTE_TRANSPORT_MODE_MQTT) {
        mqtt_client_stop();
    } else {
        ws_client_stop();
    }
}

bool remote_transport_is_connected(void)
{
    if (!g_remote_transport_ctx.initialized) {
        return false;
    }

    if (g_remote_transport_ctx.config.mode == REMOTE_TRANSPORT_MODE_MQTT) {
        return mqtt_client_is_connected();
    }
    return ws_client_is_connected();
}

void remote_transport_register_state_callback(remote_transport_state_callback_t callback, void *user_data)
{
    g_remote_transport_ctx.state_callback = callback;
    g_remote_transport_ctx.state_callback_user_data = user_data;

    if (!g_remote_transport_ctx.initialized) {
        return;
    }

    if (g_remote_transport_ctx.config.mode == REMOTE_TRANSPORT_MODE_MQTT) {
        mqtt_client_register_state_callback(forward_state_callback, NULL);
    } else {
        ws_client_register_state_callback(forward_state_callback, NULL);
    }
}

int remote_transport_send_json(const char *json_str)
{
    if (!g_remote_transport_ctx.initialized) {
        return -1;
    }

    if (g_remote_transport_ctx.config.mode == REMOTE_TRANSPORT_MODE_MQTT) {
        return mqtt_client_send_json(json_str);
    }
    return ws_client_send_json(json_str);
}

int remote_transport_send_request_json(const char *json_str)
{
    if (!g_remote_transport_ctx.initialized) {
        return -1;
    }

    if (g_remote_transport_ctx.config.mode == REMOTE_TRANSPORT_MODE_MQTT) {
        return mqtt_client_send_request_json(json_str);
    }
    return ws_client_send_json(json_str);
}

int remote_transport_send_reply_json(const char *json_str)
{
    if (!g_remote_transport_ctx.initialized) {
        return -1;
    }

    if (g_remote_transport_ctx.config.mode == REMOTE_TRANSPORT_MODE_MQTT) {
        return mqtt_client_send_reply_json(json_str);
    }
    return ws_client_send_json(json_str);
}

void remote_transport_publish_event(const char *event_type, const char *payload)
{
    if (!g_remote_transport_ctx.initialized || !event_type || !payload) {
        return;
    }

    if (g_remote_transport_ctx.config.mode == REMOTE_TRANSPORT_MODE_MQTT) {
        mqtt_client_publish_event(event_type, payload);
    } else {
        ws_client_publish_event(event_type, payload);
    }
}

void remote_transport_report_can_frame(int channel, const char *frame_text)
{
    if (!g_remote_transport_ctx.initialized || !frame_text) {
        return;
    }

    if (g_remote_transport_ctx.config.mode == REMOTE_TRANSPORT_MODE_MQTT) {
        mqtt_client_report_can_frame(channel, frame_text);
    } else {
        ws_client_report_can_frame(channel, frame_text);
    }
}

const char *remote_transport_get_device_id(void)
{
    if (!g_remote_transport_ctx.initialized) {
        return "";
    }

    if (g_remote_transport_ctx.config.mode == REMOTE_TRANSPORT_MODE_MQTT) {
        return mqtt_client_get_device_id();
    }
    return ws_client_get_device_id();
}

int remote_transport_get_server_info(char *host, size_t host_size, uint16_t *port)
{
    if (!g_remote_transport_ctx.initialized) {
        return -1;
    }

    if (g_remote_transport_ctx.config.mode == REMOTE_TRANSPORT_MODE_MQTT) {
        return mqtt_client_get_server_info(host, host_size, port);
    }
    return ws_client_get_server_info(host, host_size, port);
}
