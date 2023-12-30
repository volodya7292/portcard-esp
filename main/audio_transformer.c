#include "audio_transformer.h"
#include <memory.h>
#include <math.h>

#define PACKET_SAMPLES 480
#define IN_CHANNELS 8
#define OUT_CHANNELS 2
#define SAMPLE_BYTESIZE 2
#define IN_PACKET_SIZE (IN_CHANNELS * PACKET_SAMPLES * SAMPLE_BYTESIZE)

static RingbufHandle_t m_in_rb = NULL;
static RingbufHandle_t m_out_rb = NULL;

static uint16_t in_samples[IN_CHANNELS * PACKET_SAMPLES];
static uint16_t out_samples[OUT_CHANNELS * PACKET_SAMPLES];

static void receive_full_buffer() {
    uint32_t left_to_receive = sizeof(in_samples);

    while (left_to_receive > 0) {
        size_t received_size = 0;
        void* bytes = xRingbufferReceiveUpTo(m_in_rb, &received_size, portMAX_DELAY, left_to_receive);

        uint32_t dst_offset = sizeof(in_samples) - left_to_receive;
        memcpy((uint8_t*)in_samples + dst_offset, bytes, received_size);

        left_to_receive -= received_size;
        vRingbufferReturnItem(m_in_rb, bytes);
    }
}

static void transformer_task(void *args) {
    while (1) {
        receive_full_buffer();

        for (int i = 0; i < PACKET_SAMPLES; i++) {
            uint16_t v1 = in_samples[i * IN_CHANNELS];
            uint16_t v2 = in_samples[i * IN_CHANNELS + 1];

            out_samples[i * OUT_CHANNELS] = v1;
            out_samples[i * OUT_CHANNELS + 1] = v2;
        }

        xRingbufferSend(m_out_rb, out_samples, sizeof(out_samples), 0);
    }

    vTaskDelete(NULL);
}

void init_audio_transformer(RingbufHandle_t in_buf, RingbufHandle_t out_buf) {
    m_in_rb = in_buf;
    m_out_rb = out_buf;

    xTaskCreate(transformer_task, "transformer_task", 4096, NULL, 3, NULL);
}