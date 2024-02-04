#include "i2s_audio.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include <math.h>
#include <memory.h>
#include "freertos/task.h"
#include "common.h"

#define I2S_BCLK_IO1 GPIO_NUM_35
#define I2S_WS_IO1 GPIO_NUM_33
#define I2S_DOUT_IO1 GPIO_NUM_34
#define I2S_UNMUTE_PIN GPIO_NUM_18
#define I2S_FULL_SAMPLE_SIZE (sizeof(int16_t) * 2) // 16bit * two channels
#define I2S_DMA_FRAMES 4096 // should be twice the size of input block size to avoid sound crackling
#define RECV_BUF_SIZE (I2S_DMA_FRAMES * I2S_FULL_SAMPLE_SIZE)

static uint32_t m_output_freq = 0;
static RingbufHandle_t m_in_rb = NULL;
static i2s_chan_handle_t tx_chan = NULL;

static void i2s_init_std_simplex()
{
    i2s_chan_config_t tx_chan_cfg = {
        .id = I2S_NUM_AUTO,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 3,
        .dma_frame_num = I2S_DMA_FRAMES,
        .auto_clear = true,
        .intr_priority = 0,
    };

    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = m_output_freq,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
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
    int16_t *w_buf = (int16_t *)malloc(RECV_BUF_SIZE);
    assert(w_buf);

    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));

    bool do_prefetch = true;

    while (1)
    {
        size_t received_size = 0;
        size_t max_recv_size = RECV_BUF_SIZE / 4;
        if (do_prefetch) {
            max_recv_size = RECV_BUF_SIZE;
            do_prefetch = false;
        }

        received_size = max_recv_size;
        bool succ = receive_full_buffer(m_in_rb, max_recv_size, 100 / portTICK_PERIOD_MS, w_buf);
        if (!succ) {
            do_prefetch = true;
            continue;
        }

        assert(i2s_channel_write(tx_chan, w_buf, received_size, NULL, portMAX_DELAY) == ESP_OK);
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
