#ifndef _UTILS_H_
#define _UTILS_H_

#include "freertos/ringbuf.h"
#include <memory.h>

#define IO_AUDIO_FREQ 48000

// Returns `true` on success.
static bool receive_full_buffer(RingbufHandle_t xRingbuffer, uint32_t size, TickType_t max_wait_ticks, void* out_buf)
{
    uint32_t left_to_receive = size;

    while (left_to_receive > 0)
    {
        size_t received_size = 0;
        void *bytes = xRingbufferReceiveUpTo(xRingbuffer, &received_size, max_wait_ticks, left_to_receive);
        if (bytes == NULL) {
            return false;
        }

        uint32_t dst_offset = size - left_to_receive;
        memcpy((uint8_t *)out_buf + dst_offset, bytes, received_size);

        left_to_receive -= received_size;
        vRingbufferReturnItem(xRingbuffer, bytes);
    }
    
    return true;
}

#endif