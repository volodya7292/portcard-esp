#ifndef _WAV_H_
#define _WAV_H_

typedef struct
{
    uint16_t num_channels;
    uint32_t channel_size;
    uint32_t num_samples;
    int16_t const *data;
} wav_data_t;

static void parse_wav(void const *ptr, wav_data_t *data)
{
    ptr += 22;
    uint16_t num_channels = *(uint16_t *)ptr;

    ptr += 18;
    uint32_t subchunk2_size = *(uint32_t *)ptr;
    uint32_t channel_size = subchunk2_size / num_channels;

    ptr += 4;

    data->num_channels = num_channels;
    data->channel_size = channel_size;
    data->num_samples = channel_size / sizeof(int16_t);
    data->data = ptr;
}

#endif // _WAV_H_
