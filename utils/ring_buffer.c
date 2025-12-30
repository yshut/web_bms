/**
 * @file ring_buffer.c
 * @brief 环形缓冲区实现
 */

#include "ring_buffer.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief 创建环形缓冲区
 */
ring_buffer_t* ring_buffer_create(size_t capacity, bool blocking)
{
    ring_buffer_t *rb = (ring_buffer_t*)malloc(sizeof(ring_buffer_t));
    if (rb == NULL) {
        log_error("分配环形缓冲区结构体失败");
        return NULL;
    }

    rb->buffer = (uint8_t*)malloc(capacity);
    if (rb->buffer == NULL) {
        log_error("分配环形缓冲区内存失败 (大小: %zu)", capacity);
        free(rb);
        return NULL;
    }

    rb->capacity = capacity;
    rb->write_pos = 0;
    rb->read_pos = 0;
    rb->blocking = blocking;

    pthread_mutex_init(&rb->lock, NULL);
    pthread_cond_init(&rb->not_empty, NULL);
    pthread_cond_init(&rb->not_full, NULL);

    log_debug("创建环形缓冲区: capacity=%zu, blocking=%d", capacity, blocking);
    return rb;
}

/**
 * @brief 销毁环形缓冲区
 */
void ring_buffer_destroy(ring_buffer_t *rb)
{
    if (rb == NULL) return;

    pthread_mutex_destroy(&rb->lock);
    pthread_cond_destroy(&rb->not_empty);
    pthread_cond_destroy(&rb->not_full);

    if (rb->buffer != NULL) {
        free(rb->buffer);
    }

    free(rb);
}

/**
 * @brief 写入数据
 */
size_t ring_buffer_write(ring_buffer_t *rb, const void *data, size_t len)
{
    if (rb == NULL || data == NULL || len == 0) {
        return 0;
    }

    pthread_mutex_lock(&rb->lock);

    /* 计算可用空间 */
    size_t available_space = rb->capacity - ring_buffer_available(rb) - 1;

    /* 如果空间不足 */
    if (len > available_space) {
        if (rb->blocking) {
            /* 阻塞等待空间 */
            while (len > available_space) {
                pthread_cond_wait(&rb->not_full, &rb->lock);
                available_space = rb->capacity - ring_buffer_available(rb) - 1;
            }
        } else {
            /* 非阻塞模式：只写入能写的部分 */
            len = available_space;
        }
    }

    if (len == 0) {
        pthread_mutex_unlock(&rb->lock);
        return 0;
    }

    /* 写入数据 */
    size_t write_pos = rb->write_pos;
    const uint8_t *src = (const uint8_t*)data;

    for (size_t i = 0; i < len; i++) {
        rb->buffer[write_pos] = src[i];
        write_pos = (write_pos + 1) % rb->capacity;
    }

    rb->write_pos = write_pos;

    /* 通知读取线程 */
    pthread_cond_signal(&rb->not_empty);
    pthread_mutex_unlock(&rb->lock);

    return len;
}

/**
 * @brief 读取数据
 */
size_t ring_buffer_read(ring_buffer_t *rb, void *data, size_t len)
{
    if (rb == NULL || data == NULL || len == 0) {
        return 0;
    }

    pthread_mutex_lock(&rb->lock);

    /* 计算可用数据 */
    size_t available = ring_buffer_available(rb);

    /* 如果数据不足 */
    if (len > available) {
        if (rb->blocking) {
            /* 阻塞等待数据 */
            while (available == 0) {
                pthread_cond_wait(&rb->not_empty, &rb->lock);
                available = ring_buffer_available(rb);
            }
            if (len > available) {
                len = available;
            }
        } else {
            /* 非阻塞模式：只读取能读的部分 */
            len = available;
        }
    }

    if (len == 0) {
        pthread_mutex_unlock(&rb->lock);
        return 0;
    }

    /* 读取数据 */
    size_t read_pos = rb->read_pos;
    uint8_t *dest = (uint8_t*)data;

    for (size_t i = 0; i < len; i++) {
        dest[i] = rb->buffer[read_pos];
        read_pos = (read_pos + 1) % rb->capacity;
    }

    rb->read_pos = read_pos;

    /* 通知写入线程 */
    pthread_cond_signal(&rb->not_full);
    pthread_mutex_unlock(&rb->lock);

    return len;
}

/**
 * @brief 获取可用数据大小
 */
size_t ring_buffer_available(const ring_buffer_t *rb)
{
    if (rb == NULL) return 0;

    size_t write_pos = rb->write_pos;
    size_t read_pos = rb->read_pos;

    if (write_pos >= read_pos) {
        return write_pos - read_pos;
    } else {
        return rb->capacity - read_pos + write_pos;
    }
}

/**
 * @brief 获取剩余空间大小
 */
size_t ring_buffer_free_space(const ring_buffer_t *rb)
{
    if (rb == NULL) return 0;
    return rb->capacity - ring_buffer_available(rb) - 1;
}

/**
 * @brief 清空缓冲区
 */
void ring_buffer_clear(ring_buffer_t *rb)
{
    if (rb == NULL) return;

    pthread_mutex_lock(&rb->lock);
    rb->read_pos = rb->write_pos;
    pthread_cond_signal(&rb->not_full);
    pthread_mutex_unlock(&rb->lock);
}

/**
 * @brief 检查缓冲区是否为空
 */
bool ring_buffer_is_empty(const ring_buffer_t *rb)
{
    if (rb == NULL) return true;
    return rb->read_pos == rb->write_pos;
}

/**
 * @brief 检查缓冲区是否已满
 */
bool ring_buffer_is_full(const ring_buffer_t *rb)
{
    if (rb == NULL) return false;
    return ring_buffer_free_space(rb) == 0;
}


