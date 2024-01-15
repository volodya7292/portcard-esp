#include "audio_transformer.h"
#include <memory.h>
#include <math.h>
#include "esp_dsp.h"
#include "wav.h"
#include "block_convoler.h"
#include "esp_psram.h"
#include "stdatomic.h"

#define IN_CHANNELS 2
#define SLIDING_CHANNELS 2
#define OUT_CHANNELS 2
#define NUM_WINDOWS 2
#define TOTAL_CH_SAMPLES (IN_CHANNELS * PACKET_SAMPLES)
#define IN_PACKET_SIZE (IN_CHANNELS * PACKET_SAMPLES * AUDIO_PROCESS_BPS)
// volume compensation for convolution
#define VOL_COMPENSATION 3.5

static RingbufHandle_t m_in_rb = NULL;
static RingbufHandle_t m_out_rb = NULL;
static atomic_uint_fast32_t m_volume_factor_f32 = 0;
static block_convoler_t fl_left_conv;
static block_convoler_t fl_right_conv;
static block_convoler_t fr_left_conv;
static block_convoler_t fr_right_conv;

static int16_t in_samples[IN_CHANNELS * AUDIO_PROCESS_BLOCK_SIZE];
static int16_t *sliding_blocks;
static int16_t out_samples[OUT_CHANNELS * AUDIO_PROCESS_BLOCK_SIZE];

static EventGroupHandle_t working;
static float *conv_scratch1;
static float *conv_scratch2;

static uint32_t tidx_arr[2] = {0, 1};
static uint32_t tidx_core[2] = {0, 1};

static int16_t *get_sliding_block(uint32_t ch)
{
    uint32_t ch_size = AUDIO_PROCESS_BLOCK_SIZE * NUM_WINDOWS;
    return sliding_blocks + ch * ch_size;
}

static void receive_full_buffer()
{
    uint32_t left_to_receive = sizeof(in_samples);

    while (left_to_receive > 0)
    {
        size_t received_size = 0;
        void *bytes = xRingbufferReceiveUpTo(m_in_rb, &received_size, portMAX_DELAY, left_to_receive);

        uint32_t dst_offset = sizeof(in_samples) - left_to_receive;
        memcpy((uint8_t *)in_samples + dst_offset, bytes, received_size);

        left_to_receive -= received_size;
        vRingbufferReturnItem(m_in_rb, bytes);
    }
}

static void conv_worker(void *args)
{
    uint32_t tidx = ((uint32_t *)args)[0];

    float i16_max = 32767.0f;
    float i16_norm = 1.0f / i16_max;
    float conv_norm = 1.0f / 2.0f * i16_max;

    while (1)
    {
        xEventGroupWaitBits(working, 1 << tidx, pdTRUE, pdTRUE, portMAX_DELAY);

        int16_t *src_ch0 = get_sliding_block(0);
        int16_t *src_ch1 = get_sliding_block(1);
        float *scratch;
        block_convoler_t *conv1;
        block_convoler_t *conv2;

        float m_volume_factor;
        memcpy(&m_volume_factor, &m_volume_factor_f32, sizeof(float));

        float conv_norm_vol = conv_norm * m_volume_factor * VOL_COMPENSATION;

        if (tidx == 0)
        {
            scratch = conv_scratch1;
            conv1 = &fl_left_conv;
            conv2 = &fr_left_conv;
        }
        else
        {
            scratch = conv_scratch2;
            conv1 = &fl_right_conv;
            conv2 = &fr_right_conv;
        }

        for (int i = 0; i < AUDIO_PROCESS_BLOCK_SIZE * 2; i++)
        {
            scratch[i << 1] = src_ch0[i];
            scratch[(i << 1) + 1] = 0.0f;
        }
        dsps_mulc_f32(scratch, scratch, AUDIO_PROCESS_BLOCK_SIZE * 2, i16_norm, 2, 2);
        block_convolver_process(conv1, scratch);
        for (int i = 0; i < AUDIO_PROCESS_BLOCK_SIZE; i++)
        {
            out_samples[i * OUT_CHANNELS + tidx] += scratch[i] * conv_norm_vol;
        }

        for (int i = 0; i < AUDIO_PROCESS_BLOCK_SIZE * 2; i++)
        {
            scratch[i << 1] = src_ch1[i];
            scratch[(i << 1) + 1] = 0.0f;
        }
        dsps_mulc_f32(scratch, scratch, AUDIO_PROCESS_BLOCK_SIZE * 2, i16_norm, 2, 2);
        block_convolver_process(conv2, scratch);
        for (int i = 0; i < AUDIO_PROCESS_BLOCK_SIZE; i++)
        {
            out_samples[i * OUT_CHANNELS + tidx] += scratch[i] * conv_norm_vol;
        }

        xEventGroupSetBits(working, 1 << (tidx + 2));
    }
}

static void do_process()
{
    // Slide windows for overlap-save
    for (int ch = 0; ch < SLIDING_CHANNELS; ch++)
    {
        int16_t *prev_block = get_sliding_block(ch);
        int16_t *curr_block = get_sliding_block(ch) + AUDIO_PROCESS_BLOCK_SIZE;

        // Shift previous samples from the current block to left
        memcpy(prev_block, curr_block, AUDIO_PROCESS_BLOCK_SIZE * AUDIO_PROCESS_BPS);

        // Copy new data to the current block
        for (int i = 0; i < AUDIO_PROCESS_BLOCK_SIZE; i++)
        {
            curr_block[i] = in_samples[i * IN_CHANNELS + ch];
        }
    }

    memset(out_samples, 0, OUT_CHANNELS * AUDIO_PROCESS_BLOCK_SIZE * sizeof(out_samples[0]));

    xEventGroupSetBits(working, (1 << 0) | (1 << 1));
    xEventGroupWaitBits(working, (1 << 2) | (1 << 3), pdTRUE, pdTRUE, portMAX_DELAY);
}

static void transformer_task(void *args)
{
    while (1)
    {
        receive_full_buffer();

        uint32_t t0 = esp_cpu_get_cycle_count();
        do_process();
        uint32_t t1 = esp_cpu_get_cycle_count();

        printf("TT2: %lu\n", t1 - t0);
        xRingbufferSend(m_out_rb, out_samples, sizeof(out_samples), 0);
    }

    vTaskDelete(NULL);
}

void init_audio_transformer(RingbufHandle_t in_buf, RingbufHandle_t out_buf, const uint8_t* fl_wav_start, const uint8_t* fr_wav_start)
{
    m_in_rb = in_buf;
    m_out_rb = out_buf;
    working = xEventGroupCreate();

    sliding_blocks = heap_caps_aligned_calloc(16, SLIDING_CHANNELS * AUDIO_PROCESS_BLOCK_SIZE * NUM_WINDOWS, sizeof(int16_t), MALLOC_CAP_INTERNAL);

    conv_scratch1 = heap_caps_aligned_alloc(16, AUDIO_PROCESS_BLOCK_SIZE * 16, MALLOC_CAP_INTERNAL);
    conv_scratch2 = heap_caps_aligned_alloc(16, AUDIO_PROCESS_BLOCK_SIZE * 16, MALLOC_CAP_INTERNAL);

    // while (1) {
    //     printf("%lu %lu %lu\n", (uint32_t)sliding_blocks, (uint32_t)in_samples, (uint32_t)out_samples);
    // }

    wav_data_t fl_data;
    parse_wav(fl_wav_start, &fl_data);
    wav_data_t fr_data;
    parse_wav(fr_wav_start, &fr_data);

    int16_t *temp = heap_caps_malloc(fl_data.channel_size, MALLOC_CAP_SPIRAM);
    for (int i = 0; i < fl_data.num_samples; i++)
    {
        temp[i] = fl_data.data[i * 2];
    }
    block_convolver_init(&fl_left_conv, conv_scratch1, AUDIO_PROCESS_BLOCK_SIZE, temp, fl_data.num_samples);
    heap_caps_free(temp);

    temp = heap_caps_malloc(fl_data.channel_size, MALLOC_CAP_SPIRAM);
    for (int i = 0; i < fl_data.num_samples; i++)
    {
        temp[i] = fl_data.data[i * 2 + 1];
    }
    block_convolver_init(&fl_right_conv, conv_scratch1, AUDIO_PROCESS_BLOCK_SIZE, temp, fl_data.num_samples);
    heap_caps_free(temp);

    temp = heap_caps_malloc(fr_data.channel_size, MALLOC_CAP_SPIRAM);
    for (int i = 0; i < fr_data.num_samples; i++)
    {
        temp[i] = fr_data.data[i * 2];
    }
    block_convolver_init(&fr_left_conv, conv_scratch1, AUDIO_PROCESS_BLOCK_SIZE, temp, fr_data.num_samples);
    heap_caps_free(temp);

    temp = heap_caps_malloc(fr_data.channel_size, MALLOC_CAP_SPIRAM);
    for (int i = 0; i < fr_data.num_samples; i++)
    {
        temp[i] = fr_data.data[i * 2 + 1];
    }
    block_convolver_init(&fr_right_conv, conv_scratch1, AUDIO_PROCESS_BLOCK_SIZE, temp, fr_data.num_samples);
    heap_caps_free(temp);

    // block_convolver_init(&fr_left_conv, AUDIO_PROCESS_BLOCK_SIZE, fl_data.data, fl_data.channel_size / sizeof(int16_t));
    // block_convolver_init(&fr_right_conv, AUDIO_PROCESS_BLOCK_SIZE, fl_data.data + fl_data.channel_size, fl_data.channel_size / sizeof(int16_t));

    // assert(dsps_fft2r_init_sc16(NULL,AUDIO_PROCESS_BLOCK_SIZE) == ESP_OK);
    // assert(dsps_fft2r_init_sc16(NULL, PROCESS_BLOCK_SIZE) == ESP_OK);
    // dsps_fft4r_rev_tables_init_fc32();

    // assert(dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE) == ESP_OK);
    // assert(dsps_fft4r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE) == ESP_OK);

    xTaskCreate(transformer_task, "transformer_task", 2048, NULL, 2, NULL);
    xTaskCreatePinnedToCore(conv_worker, "conv_worker0", 2048, &tidx_arr[0], 2, NULL, tidx_core[0]);
    xTaskCreatePinnedToCore(conv_worker, "conv_worker1", 2048, &tidx_arr[1], 2, NULL, tidx_core[1]);

    // while (1)
    // {
    //     printf("%i %i %lu\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(MALLOC_CAP_RTCRAM), esp_get_free_heap_size());
    //     printf("%lu %lu\n", (uint32_t)conv_scratch1, (uint32_t)conv_scratch1);
    //     vTaskDelay(1000 / portTICK_PERIOD_MS);
    // }
}

void audio_transformer_set_volume(float volume_factor)
{
    uint32_t var;
    memcpy(&var, &volume_factor, sizeof(float));
    atomic_store(&m_volume_factor_f32, var);
}