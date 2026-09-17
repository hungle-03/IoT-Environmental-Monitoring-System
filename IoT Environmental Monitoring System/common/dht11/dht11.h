#ifndef DHT_H
#define DHT_H

#include "esp_err.h"
#include "hal/gpio_types.h"

typedef enum {
    DHT11_CRC_ERROR = -2,
    DHT11_TIMEOUT_ERROR = -1,
    DHT11_OK = 0
} dht11_status_t;


struct dht11_read {
    int status;
    int temperature;
    int humidity;
};

void DHT11_Init(gpio_num_t gpio_num);
struct dht11_read DHT11_Read(void);

#endif



/*DHT11_Init(GPIO_NUM_33);

struct dht11_read dht;

dht = DHT11_Read();

if (dht.status == DHT11_OK)
{
    printf("Temperature: %d C\n", dht.temperature);
    printf("Humidity: %d %%\n", dht.humidity);
}*/