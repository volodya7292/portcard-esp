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
    esp_err_t ret;

    for (int i = 0; i < PACKETS_IN_FLIGHT; i++)
    {
        trans[i].length = MAX_PACKET_SIZE * 8;
        trans[i].rx_buffer = recvbuf[i];
        ret = spi_slave_queue_trans(SPI_INSTANCE, &trans[i], portMAX_DELAY);
        check_err(ret);
    }

    uint32_t curr_packet_idx = 0;
    uint32_t t = 0;

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

        ret = spi_slave_queue_trans(SPI_INSTANCE, curr_trans, portMAX_DELAY);
        check_err(ret);

        curr_packet_idx = (curr_packet_idx + 1) % PACKETS_IN_FLIGHT;
        t += 1;
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
    };

    // Initialize SPI slave interface
    esp_err_t ret = spi_slave_initialize(SPI_INSTANCE, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    check_err(ret);

    (void)xTaskCreate(spi_receiver_task, "spi_receiver", 4096, NULL, 3, NULL);
}
