#include "freertos/ringbuf.h"

typedef void(*func_controls_change)(float volume_factor);

void init_spi_receiver(RingbufHandle_t out_rb, func_controls_change on_controls_change);
