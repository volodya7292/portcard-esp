#include "i2s_audio.h"
#include "driver/i2s_std.h"
#include <math.h>
#include <memory.h>

#define I2S_BCLK_IO1 GPIO_NUM_1
#define I2S_WS_IO1 GPIO_NUM_2
#define I2S_DOUT_IO1 GPIO_NUM_4
#define I2S_BUFF_SIZE 1024

static uint32_t m_output_freq = 0;
static RingbufHandle_t m_in_rb = NULL;
static i2s_chan_handle_t tx_chan = NULL;

static void i2s_init_std_simplex()
{
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(m_output_freq),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_IO1,
            .ws = I2S_WS_IO1,
            .dout = I2S_DOUT_IO1,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));
}

static void i2s_write_task(void *args)
{
    uint8_t *w_buf = (uint8_t *)calloc(1, I2S_BUFF_SIZE);
    assert(w_buf); // Check if w_buf allocation success

    // TODO
    // /* (Optional) Preload the data before enabling the TX channel, so that the valid data can be transmitted immediately */
    // while (w_bytes == I2S_BUFF_SIZE) {
    //     /* Here we load the target buffer repeatedly, until all the DMA buffers are preloaded */
    //     ESP_ERROR_CHECK(i2s_channel_preload_data(tx_chan, w_buf, I2S_BUFF_SIZE, &w_bytes));
    // }

    /* Enable the TX channel */
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));

    while (1)
    {
        size_t received_size = 0;
        // void* bytes = xRingbufferReceive(m_in_rb, &received_size, portMAX_DELAY);
        void *bytes = xRingbufferReceiveUpTo(m_in_rb, &received_size, portMAX_DELAY, I2S_BUFF_SIZE);
        memcpy(w_buf, bytes, received_size);
        vRingbufferReturnItem(m_in_rb, bytes);

        if (i2s_channel_write(tx_chan, w_buf, received_size, NULL, portMAX_DELAY) != ESP_OK)
        {
            // printf("Write Task: i2s write %d bytes\n", w_bytes);
            printf("Write Task: i2s write failed\n");
        }
    }

    free(w_buf);
    vTaskDelete(NULL);
}

void init_i2s_audio(RingbufHandle_t in_buf, uint32_t output_freq)
{
    m_in_rb = in_buf;
    m_output_freq = output_freq;

    i2s_init_std_simplex();
    xTaskCreate(i2s_write_task, "i2s_write_task", 1024, NULL, 3, NULL);
}
