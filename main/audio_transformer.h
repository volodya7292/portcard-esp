#include "freertos/ringbuf.h"

#define AUDIO_PROCESS_BLOCK_SIZE 2048
#define AUDIO_PROCESS_IN_CHANNELS 8
#define AUDIO_PROCESS_OUT_CHANNELS 2
#define AUDIO_PROCESS_BPS 2 // bytes per single sample

void init_audio_transformer(RingbufHandle_t in_buf, RingbufHandle_t out_buf, const uint8_t* fl_wav_start, const uint8_t* fr_wav_start);
void audio_transformer_set_volume(float volume_factor);