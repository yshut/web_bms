#ifndef CAN_BUFFER_H
#define CAN_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

// CAN环形缓冲区
typedef struct {
    char **messages;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    pthread_mutex_t mutex;
} can_ring_buffer_t;

// 创建环形缓冲区
can_ring_buffer_t* can_buffer_create(uint32_t size);

// 销毁环形缓冲区
void can_buffer_destroy(can_ring_buffer_t *buffer);

// 写入消息
int can_buffer_write(can_ring_buffer_t *buffer, const char *message);

// 读取消息
char* can_buffer_read(can_ring_buffer_t *buffer);

// 清空缓冲区
void can_buffer_clear(can_ring_buffer_t *buffer);

// 获取缓冲区大小
uint32_t can_buffer_count(can_ring_buffer_t *buffer);

#endif // CAN_BUFFER_H

