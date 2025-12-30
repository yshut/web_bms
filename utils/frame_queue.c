#include "frame_queue.h"
#include <stdlib.h>
#include <string.h>

frame_queue_t* frame_queue_create(uint32_t capacity)
{
    if (capacity == 0) return NULL;
    frame_queue_t *q = (frame_queue_t*)malloc(sizeof(frame_queue_t));
    if (!q) return NULL;
    q->buffer = (can_frame_t*)malloc(sizeof(can_frame_t) * capacity);
    if (!q->buffer) { free(q); return NULL; }
    q->capacity = capacity;
    q->read_index = 0;
    q->write_index = 0;
    pthread_mutex_init(&q->lock, NULL);
    return q;
}

void frame_queue_destroy(frame_queue_t *q)
{
    if (!q) return;
    pthread_mutex_destroy(&q->lock);
    if (q->buffer) free(q->buffer);
    free(q);
}

static inline bool frame_queue_is_full_unlocked(frame_queue_t *q)
{
    uint32_t next = (q->write_index + 1) % q->capacity;
    return next == q->read_index;
}

static inline bool frame_queue_is_empty_unlocked(frame_queue_t *q)
{
    return q->write_index == q->read_index;
}

bool frame_queue_push(frame_queue_t *q, const can_frame_t *frame)
{
    if (!q || !frame) return false;
    pthread_mutex_lock(&q->lock);
    if (frame_queue_is_full_unlocked(q)) {
        pthread_mutex_unlock(&q->lock);
        return false; /* 丢弃，永不写半帧 */
    }
    q->buffer[q->write_index] = *frame; /* 结构体拷贝 */
    q->write_index = (q->write_index + 1) % q->capacity;
    pthread_mutex_unlock(&q->lock);
    return true;
}

bool frame_queue_pop(frame_queue_t *q, can_frame_t *out)
{
    if (!q || !out) return false;
    pthread_mutex_lock(&q->lock);
    if (frame_queue_is_empty_unlocked(q)) {
        pthread_mutex_unlock(&q->lock);
        return false;
    }
    *out = q->buffer[q->read_index];
    q->read_index = (q->read_index + 1) % q->capacity;
    pthread_mutex_unlock(&q->lock);
    return true;
}

uint32_t frame_queue_size(frame_queue_t *q)
{
    if (!q) return 0;
    pthread_mutex_lock(&q->lock);
    uint32_t size = (q->write_index + q->capacity - q->read_index) % q->capacity;
    pthread_mutex_unlock(&q->lock);
    return size;
}

void frame_queue_clear(frame_queue_t *q)
{
    if (!q) return;
    pthread_mutex_lock(&q->lock);
    q->read_index = q->write_index;
    pthread_mutex_unlock(&q->lock);
}


