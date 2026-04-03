/**
 * @file can_mqtt_engine.h
 * @brief 板端 CAN→MQTT 规则引擎
 *
 * 按照 can_mqtt_rules.json 配置，将 CAN 帧解码后发布到 MQTT。
 * 规则文件存储路径（优先顺序）：
 *   /mnt/UDISK/can_mqtt_rules.json
 *   /mnt/SDCARD/can_mqtt_rules.json
 */

#ifndef CAN_MQTT_ENGINE_H
#define CAN_MQTT_ENGINE_H

#include "can_handler.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  规则数据结构                                                         */
/* ------------------------------------------------------------------ */

typedef enum {
    CAN_MQTT_BYTE_ORDER_LITTLE_ENDIAN = 0,  /* Intel 字节序 */
    CAN_MQTT_BYTE_ORDER_BIG_ENDIAN    = 1,  /* Motorola 字节序 */
} can_mqtt_byte_order_t;

typedef enum {
    CAN_MQTT_PAYLOAD_JSON = 0,  /* {"signal":"xxx","value":1.23,"unit":"V",...} */
    CAN_MQTT_PAYLOAD_RAW  = 1,  /* 仅数值字符串 */
} can_mqtt_payload_mode_t;

typedef struct {
    int  start_bit;    /* LSB 位置（Intel）或 MSB 位置（Motorola）*/
    int  bit_length;
    can_mqtt_byte_order_t byte_order;
    bool is_signed;
    double factor;
    double offset;
    char unit[32];
} can_mqtt_decode_t;

typedef struct {
    char topic_template[256]; /* 支持 {topic_prefix} {device_id} {message_name} {signal_name} {channel} */
    can_mqtt_payload_mode_t payload_mode;
    int  qos;
    bool retain;
} can_mqtt_pub_t;

typedef struct {
    char     id[64];
    char     name[128];
    bool     enabled;
    int      priority;
    /* 匹配条件 */
    char     channel[16];   /* "can0" / "can1" / "any" */
    uint32_t can_id;        /* 不含标志位，仅 ID */
    bool     is_extended;
    bool     match_any_id;  /* true 则忽略 can_id 字段 */
    /* 信号来源信息（仅用于 topic 模板替换） */
    char     message_name[64];
    char     signal_name[64];
    /* 解码 */
    can_mqtt_decode_t decode;
    /* 发布 */
    can_mqtt_pub_t mqtt;
    /* ── 运行时状态（不序列化到 JSON） ──────────────────────────────
     * 实现"仅值变化时发布"：比较原始整数值（未乘系数），
     * 避免浮点精度问题。超过 heartbeat_interval 秒后强制发布一次。 */
    uint64_t last_raw;            /* 上次发布时的原始解码值 */
    bool     has_last_raw;        /* 是否已有上次的值 */
    double   last_publish_ts;     /* 上次发布的 UNIX 时间戳（秒） */
} can_mqtt_rule_t;

/* 心跳强制发布间隔（秒）：即使值未变化，也每隔此秒数发布一次。
 * 设为 0 可禁用心跳（纯按值变化发布）。 */
#define CAN_MQTT_HEARTBEAT_INTERVAL 60

typedef struct {
    int              version;
    time_t           updated_at;
    can_mqtt_rule_t *rules;
    int              rule_count;
    int              rule_capacity;
} can_mqtt_rules_t;

/* ------------------------------------------------------------------ */
/*  接口                                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief 初始化引擎（自动加载规则文件）
 * @return 0 成功
 */
int can_mqtt_engine_init(void);

/** @brief 反初始化 */
void can_mqtt_engine_deinit(void);

/**
 * @brief 从文件加载规则
 * @return 0 成功，-1 失败
 */
int can_mqtt_engine_load_rules(const char *path);

/** @brief 保存规则到文件 */
int can_mqtt_engine_save_rules(const char *path);

/** @brief 按优先顺序加载规则（UDISK → SDCARD） */
int can_mqtt_engine_load_rules_best(char *used_path, size_t used_path_size);

/** @brief 按优先顺序保存规则（UDISK → SDCARD） */
int can_mqtt_engine_save_rules_best(char *used_path, size_t used_path_size);

/**
 * @brief 处理一帧 CAN 数据（由 can_frame_dispatcher 调用）
 */
void can_mqtt_engine_frame_callback(int channel, const can_frame_t *frame, void *user_data);

/** @brief 获取当前规则集（只读引用） */
const can_mqtt_rules_t *can_mqtt_engine_get_rules(void);

/**
 * @brief 用 JSON 字符串替换全部规则（网页 POST 时调用）
 * @return 0 成功，-1 JSON 解析失败
 */
int can_mqtt_engine_set_rules_json(const char *json_str);

/**
 * @brief 将当前规则序列化为 JSON 字符串
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 写入字节数，-1 失败
 */
int can_mqtt_engine_get_rules_json(char *buffer, int buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* CAN_MQTT_ENGINE_H */
