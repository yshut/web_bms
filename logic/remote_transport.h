#ifndef REMOTE_TRANSPORT_H
#define REMOTE_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REMOTE_TRANSPORT_MODE_WEBSOCKET = 0,
    REMOTE_TRANSPORT_MODE_MQTT = 1,
} remote_transport_mode_t;

typedef struct {
    remote_transport_mode_t mode;
    char ws_host[128];
    uint16_t ws_port;
    char ws_path[128];
    bool ws_use_ssl;
    int ws_reconnect_interval_ms;
    int ws_keepalive_interval_s;
    char mqtt_host[128];
    uint16_t mqtt_port;
    char mqtt_client_id[128];
    char mqtt_username[64];
    char mqtt_password[128];
    int mqtt_keepalive_s;
    int mqtt_qos;
    char mqtt_topic_prefix[128];
    bool mqtt_use_tls;
} remote_transport_config_t;

typedef void (*remote_transport_state_callback_t)(bool connected, const char *host, uint16_t port, void *user_data);

int remote_transport_init(const remote_transport_config_t *config);
int remote_transport_init_from_app_config(void);
void remote_transport_deinit(void);
int remote_transport_start(void);
void remote_transport_stop(void);
bool remote_transport_is_connected(void);
void remote_transport_register_state_callback(remote_transport_state_callback_t callback, void *user_data);
int remote_transport_send_json(const char *json_str);
int remote_transport_send_request_json(const char *json_str);
int remote_transport_send_reply_json(const char *json_str);
void remote_transport_publish_event(const char *event_type, const char *payload);
void remote_transport_report_can_frame(int channel, const char *frame_text);
const char *remote_transport_get_device_id(void);
int remote_transport_get_server_info(char *host, size_t host_size, uint16_t *port);
remote_transport_mode_t remote_transport_get_mode(void);
const char *remote_transport_mode_to_string(remote_transport_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* REMOTE_TRANSPORT_H */
