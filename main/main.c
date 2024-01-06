#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// #include "tinyusb.h"
// #include "tusb_cdc_acm.h"
// #include "tusb_console.h"
// #include "tusb.h"
#include "usb.h"
#include "usb_audio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "math.h"
#include "i2s_audio.h"
#include "audio_transformer.h"
#include "driver/spi_common.h"
#include "spi_receiver.h"
// #include "esp_console.h"

#define IO_AUDIO_FREQ 48000
#define BLINK_GPIO GPIO_NUM_21

// double-buffered ring buffer sizes
#define AUDIO_PROCESS_IN_RB_SIZE (AUDIO_PROCESS_BLOCK_SIZE * AUDIO_PROCESS_IN_CHANNELS * AUDIO_PROCESS_BPS * 2)
#define AUDIO_PROCESS_OUT_RB_SIZE (AUDIO_PROCESS_BLOCK_SIZE * AUDIO_PROCESS_OUT_CHANNELS * AUDIO_PROCESS_BPS * 2)

static led_strip_handle_t led_strip;

static void configure_led(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
    led_strip_clear(led_strip);
}

void on_data_receive()
{
    led_strip_set_pixel(led_strip, 0, 16, 16, 16);
    // led_strip_refresh(led_strip);
}

void on_controls_change(float volume_factor)
{
    audio_transformer_set_volume(volume_factor);
}

void app_main()
{
    // /* Setting TinyUSB up */
    // const tinyusb_config_t tusb_cfg = {
    //     .device_descriptor = NULL,
    //     .string_descriptor = NULL,
    //     .external_phy = false, // In the most cases you need to use a `false` value
    //     .configuration_descriptor = NULL,
    // };
    // ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    // tinyusb_config_cdcacm_t acm_cfg = {0}; // the configuration uses default values
    // ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));
    // esp_tusb_init_console(TINYUSB_CDC_ACM_0);

    configure_led();

    RingbufHandle_t rb_in2trans = xRingbufferCreate(AUDIO_PROCESS_IN_RB_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (rb_in2trans == NULL)
    {
        printf("Failed to create ringbuf!\n");
    }

    RingbufHandle_t rb_trans2out = xRingbufferCreate(AUDIO_PROCESS_OUT_RB_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (rb_trans2out == NULL)
    {
        printf("Failed to create ringbuf!\n");
    }

    init_i2s_audio(rb_trans2out, IO_AUDIO_FREQ);

    init_usb_audio(rb_in2trans);
    init_usb();

    // TODO: use dsps_fft2r_sc16 for FFT

    // init_spi_receiver(rb_in2trans, on_controls_change);
    init_audio_transformer(rb_in2trans, rb_trans2out);

    led_strip_set_pixel(led_strip, 0, 0, 16, 0);
    led_strip_refresh(led_strip);

    // if (dsps_fft4r_init_fc32(NULL, 2048) != ESP_OK)
    // {
    //     return;
    // }

    // uint32_t t0 = esp_cpu_get_cycle_count();
    // if (dsps_fft4r_fc32(test_data, 2048) != ESP_OK)
    // {
    //     return;
    // }
    // uint32_t t1 = esp_cpu_get_cycle_count();

    // if (t1 - t0 < 160000)
    // {
    //     led_strip_set_pixel(led_strip, 0, 16, 0, 0);
    //     led_strip_refresh(led_strip);
    // } else {
    //     led_strip_set_pixel(led_strip, 0, 16, 0, 16);
    //     led_strip_refresh(led_strip);
    // }

    while (1)
    {
        // led_strip_set_pixel(led_strip, 0, 0, 16, 0);
        led_strip_refresh(led_strip);
        printf("init\n");
        fprintf(stdout, "example: print -> stdout\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
