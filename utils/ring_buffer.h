/**
 * @file ring_buffer.h
 * @brief 环形缓冲区 - 用于CAN数据高速处理
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/**
 * @brief 环形缓冲区结构体
 */
typedef struct {
    uint8_t *buffer;            // 数据缓冲区
    size_t capacity;            // 容量（字节）
    volatile size_t write_pos;  // 写位置
    volatile size_t read_pos;   // 读位置
    pthread_mutex_t lock;       // 互斥锁
    pthread_cond_t not_empty;   // 非空条件变量
    pthread_cond_t not_full;    // 非满条件变量
    bool blocking;              // 是否阻塞模式
} ring_buffer_t;

/**
 * @brief 创建环形缓冲区
 * @param capacity 容量（字节）
 * @param blocking 是否阻塞模式
 * @return ring_buffer_t* 缓冲区指针，失败返回NULL
 */
ring_buffer_t* ring_buffer_create(size_t capacity, bool blocking);

/**
 * @brief 销毁环形缓冲区
 * @param rb 缓冲区指针
 */
void ring_buffer_destroy(ring_buffer_t *rb);

/**
 * @brief 写入数据
 * @param rb 缓冲区指针
 * @param data 数据指针
 * @param len 数据长度
 * @return size_t 实际写入的字节数
 */
size_t ring_buffer_write(ring_buffer_t *rb, const void *data, size_t len);

/**
 * @brief 读取数据
 * @param rb 缓冲区指针
 * @param data 数据指针（输出）
 * @param len 最大读取长度
 * @return size_t 实际读取的字节数
 */
size_t ring_buffer_read(ring_buffer_t *rb, void *data, size_t len);

/**
 * @brief 获取可用数据大小
 * @param rb 缓冲区指针
 * @return size_t 可用数据大小（字节）
 */
size_t ring_buffer_available(const ring_buffer_t *rb);

/**
 * @brief 获取剩余空间大小
 * @param rb 缓冲区指针
 * @return size_t 剩余空间大小（字节）
 */
size_t ring_buffer_free_space(const ring_buffer_t *rb);

/**
 * @brief 清空缓冲区
 * @param rb 缓冲区指针
 */
void ring_buffer_clear(ring_buffer_t *rb);

/**
 * @brief 检查缓冲区是否为空
 * @param rb 缓冲区指针
 * @return bool 是否为空
 */
bool ring_buffer_is_empty(const ring_buffer_t *rb);

/**
 * @brief 检查缓冲区是否已满
 * @param rb 缓冲区指针
 * @return bool 是否已满
 */
bool ring_buffer_is_full(const ring_buffer_t *rb);

#endif /* RING_BUFFER_H */


