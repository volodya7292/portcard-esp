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
#include "nvs_flash.h"
#include "resources.h"
#include "common.h"
// #include "esp_console.h"

#define BLINK_GPIO GPIO_NUM_21

// double-buffered ring buffer sizes
#define AUDIO_PROCESS_IN_RB_SIZE (AUDIO_PROCESS_BLOCK_SIZE * AUDIO_PROCESS_IN_CHANNELS * AUDIO_PROCESS_BPS * 2)
#define AUDIO_PROCESS_OUT_RB_SIZE (AUDIO_PROCESS_BLOCK_SIZE * AUDIO_PROCESS_OUT_CHANNELS * AUDIO_PROCESS_BPS * 2)
#define IS_DEBUG false

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
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
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

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t nvs_handle;
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    ESP_ERROR_CHECK(err);

    int32_t restart_counter = 0;
    err = nvs_get_i32(nvs_handle, "restart_counter", &restart_counter);

    restart_counter++;
    err = nvs_set_i32(nvs_handle, "restart_counter", restart_counter);

    // ------------------------------------------------------------------------------------------

    configure_led();

    const uint8_t *fl_wav_start;
    const uint8_t *fr_wav_start;

    if (restart_counter % 2 == 0)
    {
        led_strip_set_pixel(led_strip, 0, 8, 8, 8);
        fl_wav_start = ___res_FL_MODE1_wav_start;
        fr_wav_start = ___res_FR_MODE1_wav_start;
    }
    else
    {
        led_strip_set_pixel(led_strip, 0, 0, 8, 0);
        fl_wav_start = ___res_FL_MODE2_wav_start;
        fr_wav_start = ___res_FR_MODE2_wav_start;
    }
    led_strip_refresh(led_strip);

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

    // init_spi_receiver(rb_in2trans, on_controls_change);
    init_audio_transformer(rb_in2trans, rb_trans2out, fl_wav_start, fr_wav_start);

#if IS_DEBUG
    led_strip_set_pixel(led_strip, 0, 0, 16, 0);
#endif

    while (1)
    {
        led_strip_refresh(led_strip);
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
}
