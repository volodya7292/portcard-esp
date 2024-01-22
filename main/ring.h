#ifndef _RING_H_
#define _RING_H_

#include <memory.h>
#include "freertos/FreeRTOS.h"

typedef struct
{
    uint8_t *data;
    uint32_t capacity;
    uint32_t read_start_idx;
    uint32_t readable_size;
    SemaphoreHandle_t mutex;
} ring_buffer_t;

static void ring_buffer_init(ring_buffer_t *buffer, uint32_t capacity)
{
    buffer->data = malloc(capacity);
    buffer->capacity = capacity;
    buffer->read_start_idx = 0;
    buffer->readable_size = 0;
    buffer->mutex = xSemaphoreCreateMutex();
}

static uint32_t ring_buffer_get_writable(ring_buffer_t *buffer)
{
    return buffer->capacity - buffer->readable_size;
}

static uint32_t ring_buffer_get_readable(ring_buffer_t *buffer)
{
    return buffer->readable_size;
}

/// @return `true` if some readable bytes were overwritten.
static bool ring_buffer_push(ring_buffer_t *buffer, uint8_t const *data_in, uint32_t size)
{
    xSemaphoreTake(buffer->mutex, portMAX_DELAY);

    if (size > buffer->capacity)
    {
        data_in = data_in + size - buffer->capacity;
        size = buffer->capacity;
    }

    uint32_t write_start_idx = (buffer->read_start_idx + buffer->readable_size) % buffer->capacity;

    if (write_start_idx + size > buffer->capacity)
    {
        uint32_t first_half_size = buffer->capacity - write_start_idx;
        uint32_t second_half_size = size - first_half_size;
        // Write first half
        memcpy(buffer->data + write_start_idx, data_in, first_half_size);
        // Write second half
        memcpy(buffer->data, data_in + first_half_size, second_half_size);
    }
    else
    {
        memcpy(buffer->data + write_start_idx, data_in, size);
    }

    uint32_t available_write_size = buffer->capacity - buffer->readable_size;
    if (size > available_write_size)
    {
        uint32_t overwritten_size = size - available_write_size;
        buffer->read_start_idx = (buffer->read_start_idx + overwritten_size) % buffer->capacity;
    }

    buffer->readable_size += size;
    if (buffer->readable_size > buffer->capacity)
    {
        buffer->readable_size = buffer->capacity;
    }

    xSemaphoreGive(buffer->mutex);

    return size > available_write_size;
}

/// @return `true` if buffer had at least `size` readable bytes.
static bool ring_buffer_pull(ring_buffer_t *buffer, uint8_t *data_out, uint32_t size)
{
    xSemaphoreTake(buffer->mutex, portMAX_DELAY);

    if (size > buffer->readable_size)
    {
        xSemaphoreGive(buffer->mutex);
        return false;
    }

    if (buffer->read_start_idx + size > buffer->capacity)
    {
        uint32_t first_half_size = buffer->capacity - buffer->read_start_idx;
        uint32_t second_half_size = size - first_half_size;
        // Read first half
        memcpy(data_out, buffer->data + buffer->read_start_idx, first_half_size);
        // Read second half
        memcpy(data_out + first_half_size, buffer->data, second_half_size);
    }
    else
    {
        memcpy(data_out, buffer->data + buffer->read_start_idx, size);
    }

    buffer->read_start_idx = (buffer->read_start_idx + size) % buffer->capacity;
    buffer->readable_size -= size;

    xSemaphoreGive(buffer->mutex);
    return true;
}

#endif