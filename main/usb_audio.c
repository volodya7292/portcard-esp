#include "usb_audio.h"
#include <stdio.h>
#include "tusb.h"
#include <math.h>
#include "audio_transformer.h"

#define AUDIO_SAMPLE_RATE CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE
#define USB_AUDIO_SAMPLE_RATE AUDIO_SAMPLE_RATE
// Corrects for distorions at high volumes
#define VOLUME_CORRECTION 1.0

// enum
// {
//     VOLUME_CTRL_0_DB = 0,
//     VOLUME_CTRL_10_DB = 2560,
//     VOLUME_CTRL_20_DB = 5120,
//     VOLUME_CTRL_30_DB = 7680,
//     VOLUME_CTRL_40_DB = 10240,
//     VOLUME_CTRL_50_DB = 12800,
//     VOLUME_CTRL_60_DB = 15360,
//     VOLUME_CTRL_70_DB = 17920,
//     VOLUME_CTRL_80_DB = 20480,
//     VOLUME_CTRL_90_DB = 23040,
//     VOLUME_CTRL_100_DB = 25600,
//     VOLUME_CTRL_SILENCE = 0x8000,
// };

const int16_t USB_VOL_MIN = -15360; // -60db
const int16_t USB_VOL_MAX = 0;
const int16_t USB_VOL_RES = 256;

static RingbufHandle_t m_out_buf = NULL;

static uint8_t spk_buf[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ];
static int8_t usb_mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];    // +1 for master channel 0
static int16_t usb_volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1]; // +1 for master channel 0

// Helper for clock get requests
static bool tud_audio_clock_get_request(uint8_t rhport, audio_control_request_t const *request)
{
    TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);

    // Example supports only single frequency, same value will be used for current value and range
    if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
    {
        if (request->bRequest == AUDIO_CS_REQ_CUR)
        {
            audio_control_cur_4_t curf = {tu_htole32(USB_AUDIO_SAMPLE_RATE)};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
        }
        else if (request->bRequest == AUDIO_CS_REQ_RANGE)
        {
            audio_control_range_4_n_t(1) rangef =
                {
                    .wNumSubRanges = tu_htole16(1),
                    .subrange[0] = {tu_htole32(USB_AUDIO_SAMPLE_RATE), tu_htole32(USB_AUDIO_SAMPLE_RATE), 0}};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &rangef, sizeof(rangef));
        }
    }
    else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID &&
             request->bRequest == AUDIO_CS_REQ_CUR)
    {
        audio_control_cur_1_t cur_valid = {.bCur = 1};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_valid, sizeof(cur_valid));
    }
    TU_LOG1("Clock get request not supported, entity = %u, selector = %u, request = %u\r\n",
            request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
}

// Helper for clock set requests
static bool tud_audio_clock_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf)
{
    (void)rhport;

    TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);
    TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

    if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
    {
        TU_VERIFY(request->wLength == sizeof(audio_control_cur_4_t));
        return true;
    }
    else
    {
        TU_LOG1("Clock set request not supported, entity = %u, selector = %u, request = %u\r\n",
                request->bEntityID, request->bControlSelector, request->bRequest);
        return true;
    }
}

// Helper for feature unit get requests
static bool tud_audio_feature_unit_get_request(uint8_t rhport, audio_control_request_t const *request)
{
    TU_ASSERT(request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT);

    if (request->bControlSelector == AUDIO_FU_CTRL_MUTE && request->bRequest == AUDIO_CS_REQ_CUR)
    {
        audio_control_cur_1_t mute1 = {.bCur = usb_mute[request->bChannelNumber]};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &mute1, sizeof(mute1));
    }
    else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME)
    {
        if (request->bRequest == AUDIO_CS_REQ_RANGE)
        {
            audio_control_range_2_n_t(1) range_vol = {
                .wNumSubRanges = tu_htole16(1),
                .subrange[0] = {.bMin = tu_htole16(USB_VOL_MIN), tu_htole16(USB_VOL_MAX), tu_htole16(USB_VOL_RES)}};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &range_vol, sizeof(range_vol));
        }
        else if (request->bRequest == AUDIO_CS_REQ_CUR)
        {
            audio_control_cur_2_t cur_vol = {.bCur = tu_htole16(usb_volume[request->bChannelNumber])};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_vol, sizeof(cur_vol));
        }
    }

    TU_LOG1("Feature unit get request not supported, entity = %u, selector = %u, request = %u\r\n",
            request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
}

static void on_volume_change()
{
    float volume_db = usb_volume[0] / USB_VOL_RES;

    // Apply volume correction
    volume_db = ((USB_VOL_MIN / USB_VOL_RES) - volume_db) * VOLUME_CORRECTION;
    volume_db = (USB_VOL_MIN / USB_VOL_RES) - volume_db;

    float factor = pow(10.0f, volume_db * 0.05); // *0.05 = /20
    if (usb_mute[0])
        factor = 0.0;

    audio_transformer_set_volume(factor);
}

// Helper for feature unit set requests
static bool tud_audio_feature_unit_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf)
{
    (void)rhport;

    TU_ASSERT(request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT);
    TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

    if (request->bControlSelector == AUDIO_FU_CTRL_MUTE)
    {
        TU_VERIFY(request->wLength == sizeof(audio_control_cur_1_t));

        usb_mute[request->bChannelNumber] = ((audio_control_cur_1_t *)buf)->bCur;
        on_volume_change();

        return true;
    }
    else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME)
    {
        TU_VERIFY(request->wLength == sizeof(audio_control_cur_2_t));

        usb_volume[request->bChannelNumber] = tu_le16toh(((audio_control_cur_2_t const *)buf)->bCur);
        on_volume_change();

        return true;
    }
    else
    {
        TU_LOG1("Feature unit set request not supported, entity = %u, selector = %u, request = %u\r\n",
                request->bEntityID, request->bControlSelector, request->bRequest);
        return true;
    }
}

bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;

    // Page 91 in UAC2 specification
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t ep = TU_U16_LOW(p_request->wIndex);

    (void)channelNum;
    (void)ctrlSel;
    (void)ep;

    //	return tud_control_xfer(rhport, p_request, &tmp, 1);

    return false; // Yet not implemented
}

// Invoked when audio class specific get request received for an interface
bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;

    // Page 91 in UAC2 specification
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t itf = TU_U16_LOW(p_request->wIndex);

    (void)channelNum;
    (void)ctrlSel;
    (void)itf;

    return false; // Yet not implemented
}

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;

    if (request->bEntityID == UAC2_ENTITY_CLOCK)
        return tud_audio_clock_get_request(rhport, request);
    if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT)
        return tud_audio_feature_unit_get_request(rhport, request);

    TU_LOG1("Get request not handled, entity = %d, selector = %d, request = %d\r\n",
            request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf)
{
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;

    if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT)
        return tud_audio_feature_unit_set_request(rhport, request, buf);
    if (request->bEntityID == UAC2_ENTITY_CLOCK)
        return tud_audio_clock_set_request(rhport, request, buf);

    TU_LOG1("Set request not handled, entity = %d, selector = %d, request = %d\r\n",
            request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
}

bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting)
{
    if (m_out_buf == NULL)
    {
        return true;
    }

    uint32_t usb_spk_data_size = tud_audio_read(spk_buf, n_bytes_received);

    // Free up space if needed
    // align down to sizeof(int16_t)*2 for stereo consistency
    uint32_t buf_avail = (xRingbufferGetCurFreeSize(m_out_buf) >> 2) << 2; 
    if (buf_avail < usb_spk_data_size) {
        size_t _sz;
        xRingbufferReceiveUpTo(m_out_buf, &_sz, 0, ((usb_spk_data_size - buf_avail) << 1) >> 1);
    }

    xRingbufferSend(m_out_buf, spk_buf, usb_spk_data_size, 0);

    return true;
}

void init_usb_audio(RingbufHandle_t out_buf)
{
    m_out_buf = out_buf;
}