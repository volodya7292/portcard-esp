#include "spi_receiver.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include <math.h>

#define SPI_INSTANCE SPI3_HOST
#define SPI_PIN_RX 10
#define SPI_PIN_CS 8
#define SPI_PIN_SCK 9
#define SPI_PIN_TX 7

#define MAX_PACKET_SIZE 1024
#define PACKETS_IN_FLIGHT 3

#define IN_CHANNELS 8
#define IN_SINGLE_SAMPLE_SIZE 2
#define IN_FULL_SAMPLE_SIZE (IN_CHANNELS * IN_SINGLE_SAMPLE_SIZE)

static RingbufHandle_t m_out_rb = NULL;

static float sin_table[8000];

inline static float sin_tabled(float v)
{
    const uint32_t sin_table_len = sizeof(sin_table) / sizeof(sin_table[0]);

    float vm = fmod(v, 1.0f);
    uint32_t idx = vm * sin_table_len;
    return sin_table[idx];
}

static void check_err(esp_err_t err)
{
    if (err == ESP_OK)
    {
        return;
    }
    while (1)
    {
        printf("ERROR: %d\n", err);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void spi_receiver_task(void *args)
{
    WORD_ALIGNED_ATTR static uint8_t recvbuf[PACKETS_IN_FLIGHT][MAX_PACKET_SIZE] = {0};
    spi_slave_transaction_t trans[PACKETS_IN_FLIGHT] = {0};
    // WORD_ALIGNED_ATTR uint8_t recvbuf0[16] = {0};
    // WORD_ALIGNED_ATTR uint8_t recvbuf1[16] = {0};
    // WORD_ALIGNED_ATTR uint8_t recvbuf2[16] = {0};

    esp_err_t ret;

    for (int i = 0; i < PACKETS_IN_FLIGHT; i++)
    {
        trans[i].length = MAX_PACKET_SIZE * 8;
        trans[i].rx_buffer = recvbuf[i];
        ret = spi_slave_queue_trans(SPI_INSTANCE, &trans[i], portMAX_DELAY);
        check_err(ret);
    }

    // trans[0].length = sizeof(recvbuf0) * 8;
    // trans[0].rx_buffer = recvbuf0;

    // trans[1].length = sizeof(recvbuf1) * 8;
    // trans[1].rx_buffer = recvbuf1;

    // trans[2].length = sizeof(recvbuf2) * 8;
    // trans[2].rx_buffer = recvbuf2;
    // spi_slave_transaction_t trans = {
    //     .length = sizeof(recvbuf) * 8,
    //     .rx_buffer = recvbuf,
    // };
    // esp_err_t ret = spi_slave_queue_trans(SPI_INSTANCE, &trans[0], portMAX_DELAY);
    // check_err(ret);
    // ret = spi_slave_queue_trans(SPI_INSTANCE, &trans[1], portMAX_DELAY);
    // check_err(ret);
    // ret = spi_slave_queue_trans(SPI_INSTANCE, &trans[2], portMAX_DELAY);
    // check_err(ret);

    uint32_t curr_packet_idx = 0;
    uint32_t t = 0;
    double time = 0.0;

    while (1)
    {
        spi_slave_transaction_t *curr_trans = &trans[curr_packet_idx];

        ret = spi_slave_get_trans_result(SPI_INSTANCE, &curr_trans, portMAX_DELAY);
        check_err(ret);

        if (curr_trans->trans_len > 0)
        {
            // Prevents channel order corruption
            uint32_t trans_bytesize = curr_trans->trans_len / 8;
            uint32_t corruption = trans_bytesize % IN_FULL_SAMPLE_SIZE;
            uint32_t proper_len = trans_bytesize - corruption;

            if (corruption > 0) {
                printf("corrupted pkt: %lu %lu\n", trans_bytesize, corruption);
            }

            uint8_t *curr_buf = (uint8_t *)curr_trans->rx_buffer;
            xRingbufferSend(m_out_rb, curr_buf, proper_len, 0);

            if (t % 1000 == 0)
            {
                printf("recv %lu %d\n", proper_len, curr_buf[5]);
            }
        }

        // TODO: ->length can be multiple of of ->trans_len ???

        // if (curr_trans->trans_len == curr_trans->length)
        // {
        //     // int16_t in_samples[PACKET_SIZE / 2];
        //     // double t_step = 1.0 / 48000.0;
        //     // double freq = 900.0f;
        //     // for (int i = 0; i < (PACKET_SIZE/2/8); i += 1)
        //     // {
        //     //     float v = sin_tabled(time * freq);
        //     //     int16_t vu = v * 4000.0;

        //     //     in_samples[i * 8] = vu;
        //     //     in_samples[i * 8 + 1] = vu;

        //     //     time += t_step;
        //     // }
        //     // xRingbufferSend(m_out_rb, in_samples, PACKET_SIZE, 0);

        //     uint8_t *curr_buf = (uint8_t *)curr_trans->rx_buffer;
        //     xRingbufferSend(m_out_rb, curr_buf, MAX_PACKET_SIZE, 0);
        //     if (t % 1000 == 0)
        //     {
        //         printf("recv\n");
        //     }
        // } else if (curr_trans->trans_len > 0) {
        //     printf("corrupted pkt: %d\n", curr_trans->trans_len);
        // }

        ret = spi_slave_queue_trans(SPI_INSTANCE, curr_trans, portMAX_DELAY);
        check_err(ret);

        curr_packet_idx = (curr_packet_idx + 1) % PACKETS_IN_FLIGHT;
        t += 1;

        // spi_slave_transaction_t *curr_trans = &trans[curr_idx];

        // ret = spi_slave_get_trans_result(SPI_INSTANCE, &curr_trans, portMAX_DELAY);
        // check_err(ret);

        // ret = spi_slave_queue_trans(SPI_INSTANCE, curr_trans, portMAX_DELAY);
        // check_err(ret);

        // if (curr_trans->trans_len > 0)
        // {
        //     if (curr_trans->trans_len != 128)
        //     {
        //         printf("Received %d != 128\n", curr_trans->trans_len);
        //     }
        //     else
        //     {
        //         for (int i = 0; i < sizeof(recvbuf0); i++)
        //         {
        //             uint8_t v = ((uint8_t *)curr_trans->rx_buffer)[i];

        //             if (v != i)
        //             {
        //                 printf("shit %d != %d\n", v, i);
        //                 break;
        //             }
        //         }

        //         printf("Received 128\n");
        //     }
        // }
    }

    vTaskDelete(NULL);
}

void init_spi_receiver(RingbufHandle_t out_rb)
{
    m_out_rb = out_rb;

    // Configuration for the SPI bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = SPI_PIN_RX,
        .miso_io_num = SPI_PIN_TX,
        .sclk_io_num = SPI_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    // Configuration for the SPI slave interface
    spi_slave_interface_config_t slvcfg = {
        .mode = 0,
        .spics_io_num = SPI_PIN_CS,
        .queue_size = PACKETS_IN_FLIGHT,
        .flags = 0,
        // .post_setup_cb = my_post_setup_cb,
        // .post_trans_cb = my_post_trans_cb
    };

    // // Configuration for the handshake line
    // gpio_config_t io_conf = {
    //     .intr_type = GPIO_INTR_DISABLE,
    //     .mode = GPIO_MODE_OUTPUT,
    //     .pin_bit_mask = BIT64(GPIO_HANDSHAKE),
    // };
    // // Configure handshake line as output
    // gpio_config(&io_conf);

    // Enable pull-ups on SPI lines so we don't detect rogue pulses when no master is connected.
    // gpio_set_pull_mode(SPI_PIN_RX, GPIO_PULLUP_ONLY);
    // gpio_set_pull_mode(SPI_PIN_SCK, GPIO_PULLUP_ONLY);
    // gpio_set_pull_mode(SPI_PIN_CS, GPIO_PULLUP_ONLY);

    // Initialize SPI slave interface
    esp_err_t ret = spi_slave_initialize(SPI_INSTANCE, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    check_err(ret);

    // while (1)
    // {
    //     printf("Code: %d\n", ret);
    //     vTaskDelay(1000 / portTICK_PERIOD_MS);
    // }

    // WORD_ALIGNED_ATTR char sendbuf[129] = "";
    // WORD_ALIGNED_ATTR char recvbuf[129] = "";
    // memset(recvbuf, 0, 33);
    // spi_slave_transaction_t t;
    // memset(&t, 0, sizeof(t));

    uint32_t sin_table_len = sizeof(sin_table) / sizeof(sin_table[0]);
    for (uint32_t i = 0; i < sin_table_len; i++)
    {
        sin_table[i] = sin((float)i * M_PI * 2 / sin_table_len);
    }

    (void)xTaskCreate(spi_receiver_task, "spi_receiver", 4096, NULL, 3, NULL);
}
