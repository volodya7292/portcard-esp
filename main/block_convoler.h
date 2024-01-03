#include <inttypes.h>

typedef struct
{
    uint32_t samples_per_block;
    float **tf_blocks;
    float **sliding_signal_fft;
    uint32_t num_blocks;
    float *scratch1;
    // float *scratch2;
    // float *scratch3;
} block_convoler_t;

void block_convolver_init(block_convoler_t *convolver, float* scratch, uint32_t samples_per_block, int16_t const *ir, uint32_t ir_num_samples);
int16_t *block_convolver_process(block_convoler_t *convolver, int16_t const *signal_block);