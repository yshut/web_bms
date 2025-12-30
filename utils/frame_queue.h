/**
 * @file frame_queue.h
 * @brief 固定元素大小（can_frame_t）的线程安全队列
 */

#ifndef FRAME_QUEUE_H
#define FRAME_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "../logic/can_handler.h"

typedef struct {
    can_frame_t *buffer;      /* 元素数组，长度为capacity */
    uint32_t capacity;        /* 最大可存放元素个数 */
    uint32_t read_index;      /* 读索引 */
    uint32_t write_index;     /* 写索引 */
    pthread_mutex_t lock;     /* 互斥锁 */
} frame_queue_t;

frame_queue_t* frame_queue_create(uint32_t capacity);
void frame_queue_destroy(frame_queue_t *q);
/* 非阻塞：满则返回false，不写入；成功返回true */
bool frame_queue_push(frame_queue_t *q, const can_frame_t *frame);
/* 非阻塞：空则返回false；成功返回true并拷贝到out */
bool frame_queue_pop(frame_queue_t *q, can_frame_t *out);
/* 当前元素数量 */
uint32_t frame_queue_size(frame_queue_t *q);
/* 清空 */
void frame_queue_clear(frame_queue_t *q);

#endif /* FRAME_QUEUE_H */


