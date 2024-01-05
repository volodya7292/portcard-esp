#include "audio_transformer.h"
#include <memory.h>
#include <math.h>
#include "esp_dsp.h"
#include "resources.h"
#include "block_convoler.h"
#include "esp_psram.h"
#include "stdatomic.h"

// #define OUT_PACKET_SAMPLES 480
#define IN_CHANNELS 8
#define SLIDING_CHANNELS 2
#define OUT_CHANNELS 2
#define NUM_WINDOWS 2
// #define CMPLX_NUM 2 // complex number has 2 real numbers
#define TOTAL_CH_SAMPLES (IN_CHANNELS * PACKET_SAMPLES)
#define IN_PACKET_SIZE (IN_CHANNELS * PACKET_SAMPLES * AUDIO_PROCESS_BPS)
// #define PROC_CORES 2

// #define WINDOW_N_NUMSAUDIO_PROCESS_BLOCK_SIZE * CMPLX_NUM

static RingbufHandle_t m_in_rb = NULL;
static RingbufHandle_t m_out_rb = NULL;
static volatile float m_volume_factor = 1.0;
static block_convoler_t fl_left_conv;
static block_convoler_t fl_right_conv;
static block_convoler_t fr_left_conv;
static block_convoler_t fr_right_conv;

static int16_t in_samples[IN_CHANNELS * AUDIO_PROCESS_BLOCK_SIZE];
static int16_t *sliding_blocks;
static int16_t out_samples[OUT_CHANNELS * AUDIO_PROCESS_BLOCK_SIZE];
// static int16_t scratch[PROCESS_BLOCK_SIZE * 2]; // complex numbers

extern const uint8_t bl_wav[] asm("../res/BL.wav");

static int16_t *get_sliding_block(uint32_t ch)
{
    uint32_t ch_size = AUDIO_PROCESS_BLOCK_SIZE * NUM_WINDOWS;
    return sliding_blocks + ch * ch_size;

    //    N_CHANNELS * AUDIO_PROCESS_BLOCK_SIZE * CMPLX_NUM * NUM_WINDOWS * sizeof(int16_t)
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

// static void convole_channel(int16_t outputuint32_t channel_idx)
// {
//     int16_t vol_i16 = m_volume_factor * 32767;

//     // dsps_mulc_s16(in_samples, in_samples, TOTAL_CH_SAMPLES, vol_i16, 1, 1);
//     // dsps_mul

//     for (int i = 0; i < PACKET_SAMPLES; i++)
//     {
//         int16_t v1 = in_samples[i * IN_CHANNELS];
//         int16_t v2 = in_samples[i * IN_CHANNELS + 1];

//         out_samples[i * OUT_CHANNELS] = v1;
//         out_samples[i * OUT_CHANNELS + 1] = v2;
//     }
// }

static void inverse_fft_sc16(int16_t *data, uint32_t n)
{
    // int16_t norm_i16 = 32767 / n;
    int16_t norm_i16 = n;
    // dsps_mulc_s16_ansi(data, data, n, norm_i16, 1, 1);
    // dspmul

    for (int i = 1; i < n * 2; i += 2)
    {
        data[i] = -data[i];
    }

    dsps_fft2r_sc16_ae32(data, n);
    dsps_bit_rev_sc16_ansi(data, n);

    for (int i = 1; i < n * 2; i += 2)
    {
        data[i] = -data[i];
    }

    // uint32_t t0 = esp_cpu_get_cycle_count();
    for (int i = 0; i < n * 2; i++)
    {
        data[i] *= n;
    }
    // uint32_t t1 = esp_cpu_get_cycle_count();
    // printf("mul: %lu\n", t1 - t0);
}

static void process_channel(int16_t *data, uint32_t n)
{
    // for (int i = 0; i <AUDIO_PROCESS_BLOCK_SIZE; i++)
    // {
    //     scratch[i * 2] = real_samples[i];
    // }

    // dsps_fft2r_sc16(scratch,AUDIO_PROCESS_BLOCK_SIZE);
    // inverse_fft_sc16(scratch,AUDIO_PROCESS_BLOCK_SIZE);

    // for (int i = 0; i <AUDIO_PROCESS_BLOCK_SIZE; i++)
    // {
    //     real_samples[i]
    //     scratch[i * 2] = real_samples[i];
    // }

    // int16_t *curr_block_ch0 = sliding_blocks[0] +AUDIO_PROCESS_BLOCK_SIZE;

    __attribute__((aligned(16))) static float scratch[AUDIO_PROCESS_BLOCK_SIZE * NUM_WINDOWS * 2] = {0.0f};

    for (int i = 0; i < n * 2; i++)
    {
        scratch[i] = (float)data[i];
    }

    // dsps_fft2r_fc32(scratch, n);
    // dsps_bit_rev_fc32(scratch, n);
    dsps_fft4r_fc32(scratch, n);
    // dsps_bit_rev_fc32(scratch, n);
    dsps_bit_rev4r_fc32(scratch, n);
    // dsps_bitre

    // int16_t norm_i16 = 32767 / n;

    // dsps_mulc_s16_ansi(data, data, n, norm_i16, 1, 1);
    // dspmul

    float norm = 1.0f / n;
    // dsps_mulc_f32_ansi(scratch, scratch, n * 2, norm, 1, 1);
    for (int i = 0; i < n * 2; i++)
    {
        scratch[i] *= norm;
    }

    for (int i = 1; i < n * 2; i += 2)
    {
        scratch[i] = -scratch[i];
    }

    // dsps_fft2r_fc32(scratch, n);
    // dsps_bit_rev_fc32(scratch, n);
    dsps_fft4r_fc32(scratch, n);
    // dsps_bit_rev_fc32(scratch, n);
    dsps_bit_rev4r_fc32(scratch, n);

    for (int i = 1; i < n * 2; i += 2)
    {
        scratch[i] = -scratch[i];
    }

    // uint32_t t0 = esp_cpu_get_cycle_count();
    for (int i = 0; i < n * 2; i += 2)
    {
        if (scratch[i] > 32700)
        {
            printf("clip %f", scratch[i]);
        }
        data[i] = (int16_t)scratch[i];
    }
    // uint32_t t1 = esp_cpu_get_cycle_count();
    // printf("mul: %lu\n", t1 - t0);
}

// static volatile bool working[2] = {false};
// static atomic_bool working[2] = {0};
static EventGroupHandle_t working;
static float *conv_scratch1;
static float *conv_scratch2;
// static volatile int16_t *conv_fl_left_out;
// static volatile int16_t *conv_fl_right_out;
// static volatile int16_t *conv_fr_left_out;
// static volatile int16_t *conv_fr_right_out;

static uint32_t tidx_arr[2] = {0, 1};
static uint32_t tidx_core[2] = {0, 1};

static void conv_worker(void *args)
{
    uint32_t tidx = ((uint32_t *)args)[0];

    float i16_max = 32767.0f;
    float i16_norm = 1.0f / i16_max;
    float conv_norm = 1.0f / 2.0f * i16_max;

    while (1)
    {
        xEventGroupWaitBits(working, 1 << tidx, pdTRUE, pdTRUE, portMAX_DELAY);

        int16_t *src = get_sliding_block(tidx);
        float *scratch;
        // int16_t *out = out_samples[tidx];
        block_convoler_t *conv1;
        block_convoler_t *conv2;

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

        // if (tidx == 1)
        // {
            for (int i = 0; i < AUDIO_PROCESS_BLOCK_SIZE * 2; i++)
            {
                scratch[i << 1] = src[i];
                scratch[(i << 1) + 1] = 0.0f;
            }
            dsps_mulc_f32_ansi(scratch, scratch, AUDIO_PROCESS_BLOCK_SIZE * 2, i16_norm, 2, 2);
            block_convolver_process(conv1, scratch);
            for (int i = 0; i < AUDIO_PROCESS_BLOCK_SIZE; i++)
            {
                out_samples[i * OUT_CHANNELS + tidx] += scratch[i] * conv_norm;
            }

            for (int i = 0; i < AUDIO_PROCESS_BLOCK_SIZE * 2; i++)
            {
                scratch[i << 1] = src[i];
                scratch[(i << 1) + 1] = 0.0f;
            }
            dsps_mulc_f32_ansi(scratch, scratch, AUDIO_PROCESS_BLOCK_SIZE * 2, i16_norm, 2, 2);
            block_convolver_process(conv2, scratch);
            for (int i = 0; i < AUDIO_PROCESS_BLOCK_SIZE; i++)
            {
                out_samples[i * OUT_CHANNELS + tidx] += scratch[i] * conv_norm;
            }
        // }

        xEventGroupSetBits(working, 1 << (tidx + 2));
    }
}

static void do_process()
{
    int16_t vol_i16 = m_volume_factor * 32767;

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
            curr_block[i] = in_samples[i * 8 + ch];
        }
    }

    // Adjust volume
    for (int ch = 0; ch < SLIDING_CHANNELS; ch++)
    {
        int16_t *curr_block = get_sliding_block(ch) + AUDIO_PROCESS_BLOCK_SIZE;
        dsps_mulc_s16(curr_block, curr_block, AUDIO_PROCESS_BLOCK_SIZE, vol_i16, 1, 1);
    }

    memset(out_samples, 0, OUT_CHANNELS * AUDIO_PROCESS_BLOCK_SIZE * sizeof(out_samples[0]));

    // TODO: Split it up onto two cores: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/pthread.html

    // working
    // atomic_fetch_add(&working_start, 1);
    // while (atomic_load(&working_start) < 2)
    // {
    //     // printf("pc5 %lu %i %i\n", tidx, working[0], working[1]);
    //     continue;
    // }

    xEventGroupSetBits(working, (1 << 0) | (1 << 1));
    xEventGroupWaitBits(working, (1 << 2) | (1 << 3), pdTRUE, pdTRUE, portMAX_DELAY);

    // assert(dsps_add_s16_ansi(out_samples[0], out_samples[1], out_samples[0], AUDIO_PROCESS_BLOCK_SIZE, 1, 1, 1, 0) == ESP_OK);

    // while (x)
    // {
    //     // printf("%i %i\n", atomic_load(&working[0]), atomic_load(&working[1]));
    //     continue;
    // }
    // printf("2\n");

    // Output
    // int16_t *curr_block_ch0 = get_sliding_block(0) + AUDIO_PROCESS_BLOCK_SIZE * CMPLX_NUM;
    // int16_t *curr_block_ch1 = get_sliding_block(1) + AUDIO_PROCESS_BLOCK_SIZE;

    // dsps_fft2r_sc16_ae32(curr_block_ch0,AUDIO_PROCESS_BLOCK_SIZE);
    // dsps_bit_rev_sc16_ansi(curr_block_ch0,AUDIO_PROCESS_BLOCK_SIZE);
    // inverse_fft_sc16(curr_block_ch0,AUDIO_PROCESS_BLOCK_SIZE);

    // process_channel(sliding_blocks[0], AUDIO_PROCESS_BLOCK_SIZE * NUM_WINDOWS);
    // process_channel(sliding_blocks[1], AUDIO_PROCESS_BLOCK_SIZE * NUM_WINDOWS);
    // process_channel(sliding_blocks[0], AUDIO_PROCESS_BLOCK_SIZE * NUM_WINDOWS);
    // process_channel(sliding_blocks[1], AUDIO_PROCESS_BLOCK_SIZE * NUM_WINDOWS);

    // static float v1[1024];
    // static float v2[1024];
    // // static float v3[1024];
    // // uint32_t t0 = esp_cpu_get_cycle_count();
    // for (int i = 0; i < 56; i++)
    // {
    //     dsps_mul_f32(v1, v2, v2, 1024, 1, 1, 1);
    // }
    // // uint32_t t1 = esp_cpu_get_cycle_count();
    // // printf("TT: %lu\n", t1 - t0);

    // for (int i = 0; i < AUDIO_PROCESS_BLOCK_SIZE; i++)
    // {
    //     int16_t v1 = conv_fl_left_out[i];
    //     int16_t v2 = curr_block_ch1[i];

    //     out_samples[i * OUT_CHANNELS] = v1;
    //     out_samples[i * OUT_CHANNELS + 1] = v2;
    // }
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

        // uint32_t hs = esp_get_free_heap_size();
        // printf("%lu\n", hs);
        // while (1)
        // {
        //     // printf("%d\n", esp_psram_get_size());
        //     printf("%d\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        //     printf("%d\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
        //     printf("%d\n", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
        //     printf("\n");
        // }

        // int16_t test[] = {-1, 0, 2, 0, 3, 0, 0, 0};
        // dsps_fft2r_sc16(test, 4);
        // for (int i = 0; i < 8; i++) {
        //     printf("%d, ", test[i]);
        // }
        // printf("\n");

        // assert(dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE) == ESP_OK);

        // -1.000001, -0.000000, 1.999997, 0.000000, 2.999999, -0.000000, -0.000031, 0.000001, -5.600000, -0.000000, 3.200000, -0.000000, 8.299999, -0.000000, 92.000031, -0.000001,
        // float test2[] = {-1.0f, 0.0f, 2.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, -5.6f, 0.0f, 3.2f, 0.0f, 8.3f, 0.0f, 92.0f, 0.0f,};
        // assert(dsps_fft2r_fc32(test2, 8) == ESP_OK);
        // assert(dsps_bit_rev_fc32(test2, 8) == ESP_OK);

        // for (int i = 0; i < 16; i++)
        // {
        //     printf("%f, ", test2[i]);
        // }
        // printf("\n");

        // for (int i = 0; i < 16; i++)
        // {
        //     test2[i] /= 8.0f;
        // }
        // for (int i = 1; i < 16; i += 2)
        // {
        //     test2[i] = -test2[i];
        // }
        // assert(dsps_fft2r_fc32(test2, 8) == ESP_OK);
        // dsps_bit_rev_fc32(test2, 8);
        // // assert(dsps_bit_rev4r_direct_fc32_ansi(test2, 8) == ESP_OK);
        // for (int i = 1; i < 16; i += 2)
        // {
        //     test2[i] = -test2[i];
        // }

        // for (int i = 0; i < 16; i++)
        // {
        //     printf("%f, ", test2[i]);
        // }
        // printf("\n\n");

        // 101.899994, 0.000000, 68.805298, 71.202354, -17.899996, 86.800003, -59.605293, 60.602360, -92.500000, 0.000000, -59.605301, -60.602352, -17.900003, -86.800003, 68.805290, -71.202362,
        // 11.650001, 27.325590, 13.649999, 5.201323, 9.349999, -5.625589, 16.299999, -26.901323, -11.562500, 0.000000, -7.450663, -7.575294, -2.237500, -10.850000, 8.600661, -8.900295,

        // assert(dsps_fft2r_init_sc16(NULL, CONFIG_DSP_MAX_FFT_SIZE) == ESP_OK);

        // int16_t test2[] = {-3560, 0, 7000, 0, 16000, 0, 0, 0};
        // dsps_fft2r_sc16_ae32(test2, 4);
        // dsps_bit_rev_sc16_ansi(test2, 4);

        // for (int i = 0; i < 8; i++)
        // {
        //     printf("%d, ", test2[i]);
        // }
        // printf("\n");

        // for (int i = 1; i < 8; i += 2)
        // {
        //     test2[i] = -test2[i];
        // }
        // dsps_fft2r_sc16_aes3(test2, 4);
        // dsps_bit_rev_sc16_ansi(test2, 4);
        // for (int i = 1; i < 8; i += 2)
        // {
        //     test2[i] = -test2[i];
        // }
        // for (int i = 0; i < 8; i++)
        // {
        //     test2[i] *= 4;
        // }

        // for (int i = 0; i < 8; i++)
        // {
        //     printf("%d, ", test2[i]);
        // }
        // printf("\n\n");
    }

    vTaskDelete(NULL);
}

void init_audio_transformer(RingbufHandle_t in_buf, RingbufHandle_t out_buf)
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
    parse_wav(___res_FL_wav_start, &fl_data);
    wav_data_t fr_data;
    parse_wav(___res_FR_wav_start, &fr_data);

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

    xTaskCreate(transformer_task, "transformer_task", 2048, NULL, 3, NULL);
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
    m_volume_factor = volume_factor;
}