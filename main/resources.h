#ifndef _RESOURCES_H_
#define _RESOURCES_H_

// extern const uint8_t ___res_BL_wav_start[] asm("_binary_BL_wav_start");
// extern const uint8_t ___res_BL_wav_end[] asm("_binary_BL_wav_end");

// extern const uint8_t ___res_BR_wav_start[] asm("_binary_BR_wav_start");
// extern const uint8_t ___res_BR_wav_end[] asm("_binary_BR_wav_end");

// extern const uint8_t ___res_SL_wav_start[] asm("_binary_SL_wav_start");
// extern const uint8_t ___res_SL_wav_end[] asm("_binary_SL_wav_end");

// extern const uint8_t ___res_SR_wav_start[] asm("_binary_SR_wav_start");
// extern const uint8_t ___res_SR_wav_end[] asm("_binary_SR_wav_end");

extern const uint8_t ___res_FL_wav_start[] asm("_binary_FL_wav_start");
extern const uint8_t ___res_FL_wav_end[] asm("_binary_FL_wav_end");

extern const uint8_t ___res_FR_wav_start[] asm("_binary_FR_wav_start");
extern const uint8_t ___res_FR_wav_end[] asm("_binary_FR_wav_end");

// extern const uint8_t ___res_FC_wav_start[] asm("_binary_FC_wav_start");
// extern const uint8_t ___res_FC_wav_end[] asm("_binary_FC_wav_end");

// extern const uint8_t ___res_LFE_wav_start[] asm("_binary_LFE_wav_start");
// extern const uint8_t ___res_LFE_wav_end[] asm("_binary_LFE_wav_end");

#define GET_RES_SIZE(res) (res##_end - res##_start)

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

#endif