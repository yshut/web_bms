#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char host[128];
    uint16_t port;
    char client_id[128];
    char username[64];
    char password[128];
    int keepalive_s;
    int qos;
    char topic_prefix[128];
    bool use_tls;
} mqtt_config_t;

typedef void (*mqtt_state_callback_t)(bool connected, const char *host, uint16_t port, void *user_data);

int mqtt_client_init(const mqtt_config_t *config);
void mqtt_client_deinit(void);
int mqtt_client_start(void);
void mqtt_client_stop(void);
bool mqtt_client_is_connected(void);
void mqtt_client_register_state_callback(mqtt_state_callback_t callback, void *user_data);
int mqtt_client_send_json(const char *json_str);
int mqtt_client_send_request_json(const char *json_str);
int mqtt_client_send_reply_json(const char *json_str);
void mqtt_client_report_can_frame(int channel, const char *frame_text);
void mqtt_client_publish_event(const char *event_type, const char *payload);
int mqtt_client_publish(const char *topic, const char *payload, int qos, bool retain);
const char *mqtt_client_get_device_id(void);
int mqtt_client_get_server_info(char *host, size_t host_size, uint16_t *port);
const char *mqtt_client_get_topic_prefix(void);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_CLIENT_H */
