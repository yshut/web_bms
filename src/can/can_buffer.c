#include "can_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

can_ring_buffer_t* can_buffer_create(uint32_t size) {
    can_ring_buffer_t *buffer = (can_ring_buffer_t*)malloc(sizeof(can_ring_buffer_t));
    if (!buffer) {
        return NULL;
    }
    
    buffer->messages = (char**)calloc(size, sizeof(char*));
    if (!buffer->messages) {
        free(buffer);
        return NULL;
    }
    
    buffer->size = size;
    buffer->head = 0;
    buffer->tail = 0;
    buffer->count = 0;
    pthread_mutex_init(&buffer->mutex, NULL);
    
    return buffer;
}

void can_buffer_destroy(can_ring_buffer_t *buffer) {
    if (!buffer) {
        return;
    }
    
    pthread_mutex_lock(&buffer->mutex);
    
    // 释放所有消息
    for (uint32_t i = 0; i < buffer->size; i++) {
        if (buffer->messages[i]) {
            free(buffer->messages[i]);
        }
    }
    
    free(buffer->messages);
    pthread_mutex_unlock(&buffer->mutex);
    pthread_mutex_destroy(&buffer->mutex);
    free(buffer);
}

int can_buffer_write(can_ring_buffer_t *buffer, const char *message) {
    if (!buffer || !message) {
        return -1;
    }
    
    pthread_mutex_lock(&buffer->mutex);
    
    // 如果缓冲区满，释放最旧的消息
    if (buffer->count >= buffer->size) {
        if (buffer->messages[buffer->tail]) {
            free(buffer->messages[buffer->tail]);
        }
        buffer->tail = (buffer->tail + 1) % buffer->size;
        buffer->count--;
    }
    
    // 写入新消息
    buffer->messages[buffer->head] = strdup(message);
    buffer->head = (buffer->head + 1) % buffer->size;
    buffer->count++;
    
    pthread_mutex_unlock(&buffer->mutex);
    return 0;
}

char* can_buffer_read(can_ring_buffer_t *buffer) {
    if (!buffer || buffer->count == 0) {
        return NULL;
    }
    
    pthread_mutex_lock(&buffer->mutex);
    
    char *message = buffer->messages[buffer->tail];
    buffer->messages[buffer->tail] = NULL;
    buffer->tail = (buffer->tail + 1) % buffer->size;
    buffer->count--;
    
    pthread_mutex_unlock(&buffer->mutex);
    return message;
}

void can_buffer_clear(can_ring_buffer_t *buffer) {
    if (!buffer) {
        return;
    }
    
    pthread_mutex_lock(&buffer->mutex);
    
    // 释放所有消息
    for (uint32_t i = 0; i < buffer->size; i++) {
        if (buffer->messages[i]) {
            free(buffer->messages[i]);
            buffer->messages[i] = NULL;
        }
    }
    
    buffer->head = 0;
    buffer->tail = 0;
    buffer->count = 0;
    
    pthread_mutex_unlock(&buffer->mutex);
}

uint32_t can_buffer_count(can_ring_buffer_t *buffer) {
    if (!buffer) {
        return 0;
    }
    
    pthread_mutex_lock(&buffer->mutex);
    uint32_t count = buffer->count;
    pthread_mutex_unlock(&buffer->mutex);
    
    return count;
}

