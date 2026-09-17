#include "dht11.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"


static gpio_num_t dht11_gpio;


/* =========================
 * GPIO
 * ========================= */

static void DHT11_SetOutput(void)
{
    gpio_set_direction(
        dht11_gpio,
        GPIO_MODE_OUTPUT
    );
}


static void DHT11_SetInput(void)
{
    gpio_set_direction(
        dht11_gpio,
        GPIO_MODE_INPUT
    );
}


/* =========================
 * Wait GPIO level
 * ========================= */

static int DHT11_WaitLevel(
    int level,
    int timeout_us
)
{
    while (gpio_get_level(dht11_gpio) == level)
    {
        if (timeout_us-- <= 0)
            return 0;

        esp_rom_delay_us(1);
    }

    return 1;
}


/* =========================
 * INIT
 * ========================= */

void DHT11_Init(gpio_num_t gpio_num)
{
    dht11_gpio = gpio_num;

    gpio_set_pull_mode(
        dht11_gpio,
        GPIO_PULLUP_ONLY
    );

    DHT11_SetInput();
}


/* =========================
 * READ
 * ========================= */

struct dht11_read DHT11_Read(void)
{
    struct dht11_read result =
    {
        .status = DHT11_OK,
        .temperature = 0,
        .humidity = 0
    };

    uint8_t data[5] = {0};


    /* =========================
     * Start signal
     * ========================= */

    DHT11_SetOutput();

    gpio_set_level(
        dht11_gpio,
        0
    );

    /*
     * DHT11 cần LOW ít nhất 18 ms
     */
    esp_rom_delay_us(20000);

    gpio_set_level(
        dht11_gpio,
        1
    );

    esp_rom_delay_us(30);

    DHT11_SetInput();


    /* =========================
     * DHT11 response
     * ========================= */

    /*
     * DHT11 kéo LOW khoảng 80 us
     */
    if (!DHT11_WaitLevel(1, 100))
    {
        result.status = DHT11_TIMEOUT_ERROR;
        return result;
    }


    /*
     * DHT11 kéo HIGH khoảng 80 us
     */
    if (!DHT11_WaitLevel(0, 100))
    {
        result.status = DHT11_TIMEOUT_ERROR;
        return result;
    }


    /*
     * Kết thúc response HIGH
     */
    if (!DHT11_WaitLevel(1, 100))
    {
        result.status = DHT11_TIMEOUT_ERROR;
        return result;
    }


    /* =========================
     * Read 40 bits
     * ========================= */

    for (int i = 0; i < 40; i++)
    {
        /*
         * Chờ DHT11 bắt đầu bit
         * bằng mức HIGH
         */
        if (!DHT11_WaitLevel(0, 100))
        {
            result.status = DHT11_TIMEOUT_ERROR;
            return result;
        }


        /*
         * Đợi khoảng giữa bit 0 và bit 1
         *
         * Bit 0: HIGH khoảng 26 us
         * Bit 1: HIGH khoảng 70 us
         */
        esp_rom_delay_us(40);


        if (gpio_get_level(dht11_gpio))
        {
            data[i / 8] |=
                (1 << (7 - (i % 8)));
        }


        /*
         * Chờ HIGH kết thúc
         */
        if (!DHT11_WaitLevel(1, 100))
        {
            result.status = DHT11_TIMEOUT_ERROR;
            return result;
        }
    }


    /* =========================
     * Checksum
     * ========================= */

    uint8_t checksum =
        data[0] +
        data[1] +
        data[2] +
        data[3];

    if (checksum != data[4])
    {
        result.status = DHT11_CRC_ERROR;
        return result;
    }


    /* =========================
     * Convert data
     * ========================= */

    result.humidity =
        data[0];

    result.temperature =
        data[2];


    return result;
}