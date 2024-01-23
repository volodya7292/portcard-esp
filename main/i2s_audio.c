#include "i2s_audio.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include <math.h>
#include <memory.h>
#include "freertos/task.h"

#define I2S_BCLK_IO1 GPIO_NUM_35
#define I2S_WS_IO1 GPIO_NUM_33
#define I2S_DOUT_IO1 GPIO_NUM_34
#define I2S_UNMUTE_PIN GPIO_NUM_18
#define I2S_FULL_SAMPLE_SIZE (sizeof(int16_t) * 2) // 16bit * two channels
#define I2S_DMA_FRAMES 960
#define I2S_BUFF_SIZE (I2S_DMA_FRAMES * I2S_FULL_SAMPLE_SIZE)

static uint32_t m_output_freq = 0;
static RingbufHandle_t m_in_rb = NULL;
static i2s_chan_handle_t tx_chan = NULL;

static void i2s_init_std_simplex()
{
    i2s_chan_config_t tx_chan_cfg = {
        .id = I2S_NUM_AUTO,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = I2S_DMA_FRAMES,
        .auto_clear = true,
        .intr_priority = 0,
    };

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
    int16_t *w_buf = (int16_t *)calloc(sizeof(int16_t), I2S_BUFF_SIZE);
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
        int16_t *bytes = xRingbufferReceiveUpTo(m_in_rb, &received_size, portMAX_DELAY, I2S_BUFF_SIZE);
        uint32_t received_samples = received_size / I2S_FULL_SAMPLE_SIZE; // a sample has two values for two channels

        uint32_t w_buf_avail = received_samples * I2S_FULL_SAMPLE_SIZE; // rounds byte count to two channels
        memcpy(w_buf, bytes, w_buf_avail);
        
        vRingbufferReturnItem(m_in_rb, bytes);

        if (i2s_channel_write(tx_chan, w_buf, w_buf_avail, NULL, portMAX_DELAY) != ESP_OK)
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
    xTaskCreate(i2s_write_task, "i2s_write_task", 1024, NULL, 2, NULL);

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(I2S_UNMUTE_PIN),
    };

    // Configure handshake line as output
    assert(gpio_config(&io_conf) == ESP_OK);

    // Unmute the DAC
    assert(gpio_set_level(I2S_UNMUTE_PIN, 1) == ESP_OK);
}
