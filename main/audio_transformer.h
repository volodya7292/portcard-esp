#include "freertos/ringbuf.h"

void init_audio_transformer(RingbufHandle_t in_buf, RingbufHandle_t out_buf);
void audio_transformer_set_volume(float volume_factor);