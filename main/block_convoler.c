#include "block_convoler.h"
#include <stdlib.h>
#include <stdbool.h>
#include "esp_dsp.h"
#include "dsps_mem.h"
#include <math.h>
#include <memory.h>
#include "esp_heap_caps.h"

// A complex number has two values
#define CMPLX_I16_SIZE (sizeof(int16_t) * 2)
#define CMPLX_F32_SIZE (sizeof(float) * 2)

static bool is_pow_of_two(uint32_t v)
{
    return __builtin_popcount(v) == 1;
}

static void inverse_fft_f32(float *data, uint32_t n)
{
    float norm = 1.0f / n;
    assert(dsps_mulc_f32(data, data, n * 2, norm, 1, 1) == ESP_OK);

    for (int i = 1; i < n * 2; i += 2)
    {
        data[i] = -data[i];
    }

    assert(dsps_fft4r_fc32(data, n) == ESP_OK);
    assert(dsps_bit_rev4r_fc32(data, n) == ESP_OK);

    for (int i = 1; i < n * 2; i += 2)
    {
        data[i] = -data[i];
    }
}

/// @param scratch a 16-byte aligned buffer of `samples_per_block * 16` bytes
void block_convolver_init(block_convoler_t *convolver, float *scratch, uint32_t samples_per_block, int16_t const *ir, uint32_t ir_num_samples)
{
    assert(is_pow_of_two(samples_per_block));
    assert(dsps_fft4r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE) == ESP_OK);

    uint32_t num_blocks = ceil((double)ir_num_samples / (double)samples_per_block);
    // Twice the size for overlap-save method to work
    uint32_t actual_block_len = 2 * samples_per_block;

    float **tf_blocks = malloc(num_blocks * sizeof(void *));
    float **sliding_signal_fft = malloc(num_blocks * sizeof(void *));

    float i16_max = 32767;
    float norm_i16 = 1.0 / i16_max;

    for (int block_idx = 0; block_idx < num_blocks; block_idx++)
    {
        float *tf_block = heap_caps_malloc(actual_block_len * CMPLX_F32_SIZE, MALLOC_CAP_SPIRAM);
        // calloc zeros the allocated memory out
        float *signal_fft_block = heap_caps_calloc(actual_block_len, CMPLX_F32_SIZE, MALLOC_CAP_SPIRAM);

        uint32_t ir_offset = block_idx * samples_per_block;
        uint32_t copy_samples = samples_per_block;

        if (ir_offset + copy_samples > ir_num_samples)
        {
            copy_samples = ir_num_samples - ir_offset;
        }

        memset(scratch, 0, actual_block_len * CMPLX_F32_SIZE);
        for (int i = 0; i < copy_samples; i++)
        {
            scratch[i * 2] = (float)ir[ir_offset + i] * norm_i16;
        }

        assert(dsps_fft4r_fc32(scratch, actual_block_len) == ESP_OK);
        assert(dsps_bit_rev4r_fc32(scratch, actual_block_len) == ESP_OK);

        memcpy(tf_block, scratch, actual_block_len * CMPLX_F32_SIZE);

        tf_blocks[block_idx] = tf_block;
        sliding_signal_fft[block_idx] = signal_fft_block;
    }

    convolver->samples_per_block = samples_per_block;
    convolver->tf_blocks = tf_blocks;
    convolver->sliding_signal_fft = sliding_signal_fft;
    convolver->num_blocks = num_blocks;
}

/// @param signal contains `2 * samples_per_block` samples.
/// @return a pointer to a temporary buffer that contains the output `samples_per_block` samples.
void block_convolver_process(block_convoler_t *convolver, float *signal)
{
    float *scratch1 = signal;
    float **sliding_signal_fft = convolver->sliding_signal_fft;
    float **tf_blocks = convolver->tf_blocks;
    uint32_t num_blocks = convolver->num_blocks;

    uint32_t actual_block_len = 2 * convolver->samples_per_block;

    // FFT the next signal block
    assert(dsps_fft4r_fc32(scratch1, actual_block_len) == ESP_OK);
    assert(dsps_bit_rev4r_fc32(scratch1, actual_block_len) == ESP_OK);

    // Shift previous blocks to the left
    float *tmp = sliding_signal_fft[0];
    for (int i = 0; i < num_blocks - 1; i++)
    {
        sliding_signal_fft[i] = sliding_signal_fft[i + 1];
    }
    sliding_signal_fft[num_blocks - 1] = tmp;

    // Save the fft of the next block
    float *last_signal_fft = sliding_signal_fft[num_blocks - 1];
    memcpy(last_signal_fft, scratch1, actual_block_len * CMPLX_F32_SIZE);

    memset(scratch1, 0, actual_block_len * CMPLX_F32_SIZE);

    // Perform segmented-fft-convolution
    for (int bi = 0; bi < num_blocks; bi++)
    {
        float *s_fft_block = sliding_signal_fft[num_blocks - 1 - bi];
        float *tf_block = tf_blocks[bi];

        for (int i = 0; i < actual_block_len * 2; i += 2)
        {
            int i_re = i;
            int i_im = i + 1;

            float sfft_re = s_fft_block[i_re];
            float sfft_im = s_fft_block[i_im];
            float tf_re = tf_block[i_re];
            float tf_im = tf_block[i_im];

            float re = sfft_re * tf_re - sfft_im * tf_im;
            float im = sfft_re * tf_im + sfft_im * tf_re;
            scratch1[i_re] += re;
            scratch1[i_im] += im;
        }
    }

    // Apply inverse fft
    inverse_fft_f32(scratch1, actual_block_len);

    uint32_t copy_src_off = convolver->samples_per_block * 2;

    // Copy the result back
    for (int i = 0; i < convolver->samples_per_block; i++)
    {
        signal[i] = scratch1[copy_src_off + i * 2];
    }
}