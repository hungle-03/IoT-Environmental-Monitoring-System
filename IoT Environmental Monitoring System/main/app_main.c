/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_task_wdt.h"

#include <stdlib.h>
#include <string.h>
#include "output_gpio.h"

#include "input_gpio.h"
#include "http_sever_main.h"
#include "wifi_manager.h"

#include "adc.h"
#include "dht11.h"
#include "soil_sensor.h"
#include "ph_sensor.h"


static const char *TAG = "MAIN";

#define WIFI_SSID     "1"
#define WIFI_PASSWORD "11111111"

/* ============================================================
 * APP EVENTS
 * ============================================================ */

#define APP_EVENT_BUTTON   BIT0
#define APP_EVENT_SENSOR   BIT1
#define APP_EVENT_AUTO     BIT2

static EventGroupHandle_t app_event_group = NULL;

/* Timer handle */
static esp_timer_handle_t app_timer = NULL;

// ===== PIN =====
#define LED_PIN  GPIO_NUM_2
#define FAN_PIN  GPIO_NUM_5
#define PUMP_PIN GPIO_NUM_18

#define BTN_MODE GPIO_NUM_13
#define BTN_PUMP GPIO_NUM_27
#define BTN_FAN  GPIO_NUM_14
#define BTN_LED  GPIO_NUM_12

#define SOIL_PIN GPIO_NUM_35
#define DHT_PIN  GPIO_NUM_33
#define PH_SENSOR_PIN GPIO_NUM_32

struct dht11_read dht;
float temperature = 0;
float humidity = 0;


float newTempMin = 30, newTempMax = 40;
float newHumiMin = 60, newHumiMax = 80;
int   newSoilMin = 40, newSoilMax = 50;


struct soil_sensor_read soil;
float soilValue = 0;


struct ph_sensor_read ph;
float phValue = 0;

bool sensorReady = false;


static bool modeAuto = false;
static bool ledState = false;
static bool fanState = false;
static bool pumpState = false;

static volatile bool button_tick = false;
static volatile bool sensor_tick = false;
static volatile bool auto_tick = false;

typedef enum
{
    STATE_SOURCE_BUTTON,
    STATE_SOURCE_WEB,
    STATE_SOURCE_AUTO
} state_source_t;


// ===== BUTTON =====
#define DEBOUNCE_DELAY 50
struct Button {
  uint8_t  pin;
  bool     state;
  bool     lastReading;
  unsigned long lastDebounce;
};


struct Button btnMode = {BTN_MODE, 1, 1, 0};
struct Button btnPump = {BTN_PUMP, 1, 1, 0};
struct Button btnFan  = {BTN_FAN,  1, 1, 0};
struct Button btnLed  = {BTN_LED,  1, 1, 0};

// ===== BUTTON HANDLE =====
bool handleSingleButton(struct Button *btn) {
  bool reading = input_gpio_get_level(btn->pin);
   int64_t now = esp_timer_get_time() / 1000;
  if (reading != btn->lastReading) btn->lastDebounce = now;

  if ((now - btn->lastDebounce) > DEBOUNCE_DELAY) {
    if (reading != btn->state) {
      btn->state = reading;
      if (btn->state == 0) {
        btn->lastReading = reading;
        return true;
      }
    }
  }
  btn->lastReading = reading;
  return false;
}

esp_err_t app_set_switch_state(
    uint8_t id,
    uint8_t value,
    state_source_t source
)
{
    char sse_data[128];

    value = value ? 1 : 0;

    switch (id)
    {
        case 1:     // SW1 - AUTO / MANUAL
            modeAuto = value;
            break;

        case 2:     // SW2 - LED
            if (modeAuto && source != STATE_SOURCE_AUTO)
                return ESP_ERR_INVALID_STATE;

            ledState = value;
            output_gpio_set_level(LED_PIN, ledState);
            break;

        case 3:     // SW3 - FAN
            if (modeAuto && source != STATE_SOURCE_AUTO)
                return ESP_ERR_INVALID_STATE;

            fanState = value;
            output_gpio_set_level(FAN_PIN, fanState);
            break;

        case 4:     // SW4 - PUMP
            if (modeAuto && source != STATE_SOURCE_AUTO)
                return ESP_ERR_INVALID_STATE;

            pumpState = value;
            output_gpio_set_level(PUMP_PIN, pumpState);
            break;

        default:
            return ESP_ERR_INVALID_ARG;
    }

    snprintf(
        sse_data,
        sizeof(sse_data),
        "{\"type\":\"sw\",\"id\":%u,\"value\":%u}",
        id,
        value
    );

    http_sse_send(sse_data);

    ESP_LOGI(
        TAG,
        "SW%u = %u, source = %s",
        id,
        value,
        source == STATE_SOURCE_BUTTON ? "BUTTON" :
        source == STATE_SOURCE_WEB    ? "WEB" :
                                        "AUTO"
    );

    return ESP_OK;
}


void syncAll(void)
{
    char sse_data[256];

    snprintf(
        sse_data,
        sizeof(sse_data),
        "{\"type\":\"state\","
        "\"mode\":%u,"
        "\"pump\":%u,"
        "\"fan\":%u,"
        "\"led\":%u}",
        modeAuto,
        pumpState,
        fanState,
        ledState
    );

    http_sse_send(sse_data);

    ESP_LOGI(TAG, "All states synchronized");
}

void handleButtons(void)
{
    if (handleSingleButton(&btnMode))
    {
        app_set_switch_state(1,!modeAuto, STATE_SOURCE_BUTTON);
    }

    if (handleSingleButton(&btnLed) && !modeAuto)
    {
        app_set_switch_state( 2, !ledState, STATE_SOURCE_BUTTON);
    }

    if (handleSingleButton(&btnFan) && !modeAuto)
    {
        app_set_switch_state( 3,!fanState, STATE_SOURCE_BUTTON);
    }

    if (handleSingleButton(&btnPump) && !modeAuto)
    {
        app_set_switch_state( 4,!pumpState,STATE_SOURCE_BUTTON);
    }
}

void autoControl(void)
{
    if (!modeAuto)
        return;

    if (!sensorReady)
        return;

    bool newFan = fanState;
    bool newPump = pumpState;

    // FAN
    if (temperature > newTempMax || humidity > newHumiMax)
    {
        newFan = true;
    }
    else if (temperature < newTempMin && humidity < newHumiMin)
    {
        newFan = false;
    }

    // PUMP
    if (soilValue < newSoilMin)
    {
        newPump = true;
    }
    else if (soilValue > newSoilMax)
    {
        newPump = false;
    }

    if (newFan != fanState)
    {
        app_set_switch_state( 3,newFan, STATE_SOURCE_AUTO);
    }

    if (newPump != pumpState)
    {
        app_set_switch_state( 4,newPump, STATE_SOURCE_AUTO);
    }
}

void syncSensors(void)
{
    char sse_data[256];

    snprintf(
        sse_data,
        sizeof(sse_data),
        "{\"type\":\"sensor\","
        "\"temperature\":%.2f,"
        "\"humidity\":%.2f,"
        "\"soil\":%.2f,"
        "\"ph\":%.2f}",
        temperature,
        humidity,
        soilValue,
        phValue
    );

    http_sse_send(sse_data);

    ESP_LOGI(
        TAG,
        "Sensor: T=%.2f H=%.2f Soil=%.2f pH=%.2f",
        temperature,
        humidity,
        soilValue,
        phValue
    );
}



void read_sensors(void)
{
    bool valid = false;

    /* DHT11 */
    dht = DHT11_Read();

    if (dht.status == DHT11_OK)
    {
        temperature = dht.temperature;
        humidity = dht.humidity;
        valid = true;
    }

    /* Soil */
    soil = SOIL_SENSOR_Read();

    if (soil.status == 0)
    {
        soilValue = soil.soil_moisture;
        valid = true;
    }

    /* pH */
    ph = PH_SENSOR_Read();

    if (ph.status == 0)
    {
        phValue = ph.ph;
        valid = true;
    }

    /*
     * Chỉ cần ít nhất một cảm biến đọc thành công
     * thì hệ thống được xem là đã có dữ liệu.
     */
    if (valid)
    {
        sensorReady = true;
    }
}


void app_sse_connected(void)
{
    syncAll();
    syncSensors();
}

esp_err_t app_http_callback(
    const char *data,
    size_t len,
    const httpd_context_t *ctx,
    char *response,
    size_t response_size
)
{
    if (ctx == NULL || response == NULL || response_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    /* --------------------------------------------------------
     * Mặc định response
     * -------------------------------------------------------- */

    response[0] = '\0';

    /* ========================================================
     * 1. SWITCH
     * ======================================================== */

    if (ctx->type == HTTPD_TYPE_SW)
    {
        if (data == NULL || len == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }

        /*
         * Web gửi:
         *
         *     "0" -> OFF
         *     "1" -> ON
         */

        uint8_t value;

        if (data[0] == '0')
        {
            value = 0;
        }
        else if (data[0] == '1')
        {
            value = 1;
        }
        else
        {
            snprintf(
                response,
                response_size,
                "{\"status\":\"error\",\"message\":\"invalid switch value\"}"
            );

            return ESP_ERR_INVALID_ARG;
        }


        /*
         * Chuyển yêu cầu về application state.
          * Nếu modeAuto = true thì chỉ cho phép SW1 (AUTO/MANUAL)
         */
        esp_err_t err = app_set_switch_state( ctx->id, value, STATE_SOURCE_WEB);

        if (err != ESP_OK)
        {
            snprintf(
                response,
                response_size,
                "{\"status\":\"error\",\"id\":%u,\"value\":%u}",
                ctx->id,
                value
            );

            return err;
        }

        /*
         * HTTP response trả về cho request hiện tại.
         */
        snprintf(
            response,
            response_size,
            "{\"status\":\"ok\","
            "\"type\":\"sw\","
            "\"id\":%u,"
            "\"value\":%u}",
            ctx->id,
            value
        );

        return ESP_OK;
    }


    /* ========================================================
     * 2. SLIDER
     * ======================================================== */


    if (ctx->type == HTTPD_TYPE_SLIDER)
    {
        if (data == NULL || len == 0)
        {
            return ESP_ERR_INVALID_ARG;
        }

        /*
        * Web gửi:
        *
        * /slider1 -> "30:40"
        * /slider2 -> "60:80"
        * /slider3 -> "40:50"
        *
        * Ý nghĩa:
        *
        * slider1 = temperature min:max
        * slider2 = humidity    min:max
        * slider3 = soil        min:max
        */

        char buffer[32];

        if (len >= sizeof(buffer))
        {
            snprintf(
                response,
                response_size,
                "{\"status\":\"error\","
                "\"message\":\"slider data too long\"}"
            );

            return ESP_ERR_INVALID_SIZE;
        }

        memcpy(buffer, data, len);
        buffer[len] = '\0';

        /*
        * Tách:
        *
        * "30:40"
        *   ↓
        * min = 30
        * max = 40
        */
        char *separator = strchr(buffer, ':');

        if (separator == NULL)
        {
            snprintf(
                response,
                response_size,
                "{\"status\":\"error\","
                "\"message\":\"expected min:max\"}"
            );

            return ESP_ERR_INVALID_ARG;
        }

        *separator = '\0';

        char *min_str = buffer;
        char *max_str = separator + 1;

        char *end_min;
        char *end_max;

        long min_value = strtol(min_str, &end_min, 10);
        long max_value = strtol(max_str, &end_max, 10);

        /*
        * Kiểm tra cả hai giá trị phải là số.
        */
        if (end_min == min_str ||
            *end_min != '\0' ||
            end_max == max_str ||
            *end_max != '\0')
        {
            snprintf(
                response,
                response_size,
                "{\"status\":\"error\","
                "\"message\":\"invalid min/max value\"}"
            );

            return ESP_ERR_INVALID_ARG;
        }


        /*
        * Giới hạn theo range của slider Web.
        *
        * Hiện tại giả sử Web dùng 0 -> 100.
        */
        if (min_value < 0)
            min_value = 0;
        if (min_value > 100)
            min_value = 100;

        if (max_value < 0)
            max_value = 0;

        if (max_value > 100)
            max_value = 100;
        /*
        * Min phải nhỏ hơn Max.
        */

        if (min_value >= max_value)
        {
            snprintf(
                response,
                response_size,
                "{\"status\":\"error\","
                "\"message\":\"min must be smaller than max\"}"
            );
            return ESP_ERR_INVALID_ARG;
        }
        /*
        * Cập nhật Application State.
        */
        switch (ctx->id)
        {
            case 1:
                /* Temperature */
                newTempMin = min_value;
                newTempMax = max_value;
                break;

            case 2:
                /* Humidity */
                newHumiMin = min_value;
                newHumiMax = max_value;
                break;

            case 3:
                /* Soil */
                newSoilMin = min_value;
                newSoilMax = max_value;
                break;

            default:
                snprintf(
                    response,
                    response_size,
                    "{\"status\":\"error\","
                    "\"message\":\"invalid slider id\"}"
                );

                return ESP_ERR_INVALID_ARG;
        }

        /*
        * Gửi trạng thái mới về Web thông qua SSE.
        */

        char sse_data[128];

        snprintf(
            sse_data,
            sizeof(sse_data),
            "{\"type\":\"slider\","
            "\"id\":%u,"
            "\"min\":%ld,"
            "\"max\":%ld}",
            ctx->id,
            min_value,
            max_value
        );

        http_sse_send(sse_data);


        /*
        * HTTP response.
        */

        snprintf(
            response,
            response_size,
            "{\"status\":\"ok\","
            "\"type\":\"slider\","
            "\"id\":%u,"
            "\"min\":%ld,"
            "\"max\":%ld}",
            ctx->id,
            min_value,
            max_value
        );

        ESP_LOGI(
            TAG,
            "Slider%u: min=%ld max=%ld",
            ctx->id,
            min_value,
            max_value
        );

        return ESP_OK;
    }


    /* ========================================================
     * 3. SENSOR
     * ======================================================== */

    if (ctx->type == HTTPD_TYPE_DATA_SENSORS)
    {
        /*
         * GET sensor:
         *
         * data = NULL
         * len  = 0  
         * Dữ liệu đã được read_sensors()
         * cập nhật vào:
         * temperature
         * humidity
         * soilValue
         * phValue
         */

        switch (ctx->id)
        {
            /* ------------------------------------------------
             * DHT11
             * ------------------------------------------------ */

            case 1:

                snprintf(
                    response,
                    response_size,
                    "{\"status\":\"ok\","
                    "\"type\":\"sensor\","
                    "\"id\":1,"
                    "\"temperature\":%.2f,"
                    "\"humidity\":%.2f}",
                    temperature,
                    humidity
                );

                break;

            /* ------------------------------------------------
             * SOIL
             * ------------------------------------------------ */

            case 2:

                snprintf(
                    response,
                    response_size,
                    "{\"status\":\"ok\","
                    "\"type\":\"sensor\","
                    "\"id\":2,"
                    "\"soil\":%.2f}",
                    soilValue
                );
                break;

            /* ------------------------------------------------
             * pH
             * ------------------------------------------------ */
            case 3:

                snprintf(
                    response,
                    response_size,
                    "{\"status\":\"ok\","
                    "\"type\":\"sensor\","
                    "\"id\":3,"
                    "\"ph\":%.2f}",
                    phValue
                );

                break;

            default:

                snprintf(
                    response,
                    response_size,
                    "{\"status\":\"error\","
                    "\"message\":\"invalid sensor id\"}"
                );

                return ESP_ERR_INVALID_ARG;
        }

        return ESP_OK;
    }


    /* ========================================================
     * 4. UNKNOWN TYPE
     * ======================================================== */

    snprintf(
        response,
        response_size,
        "{\"status\":\"error\",\"message\":\"unknown HTTP type\"}"
    );

    return ESP_ERR_INVALID_ARG;
}


static void app_timer_callback(void *arg)
{
    static uint32_t counter = 0;

    if (app_event_group == NULL)
        return;

    /* Button: 100 ms */
    xEventGroupSetBits(
        app_event_group,
        APP_EVENT_BUTTON
    );

    counter++;

    /* Sensor + Auto: mỗi 2 giây */
    if (counter >= 20)
    {
        counter = 0;

        xEventGroupSetBits(
            app_event_group,
            APP_EVENT_SENSOR | APP_EVENT_AUTO
        );
    }
}


/* ============================================================
 * TIMER INIT
 * ============================================================ */

static void app_timer_init(void)
{
    const esp_timer_create_args_t timer_args =
    {
        .callback = &app_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "app_tick_timer"
    };

    ESP_ERROR_CHECK(
        esp_timer_create(
            &timer_args,
            &app_timer
        )
    );

    /*
     * 100000 us = 100 ms
     */
    ESP_ERROR_CHECK(
        esp_timer_start_periodic(
            app_timer,
            100000
        )
    );

    ESP_LOGI(TAG, "Application timer started");
}

/* ============================================================
 * APP PROCESS
 *
 * Task sẽ block tại xEventGroupWaitBits()
 * khi không có event.
 * ============================================================ */

static void app_process(void)
{
    EventBits_t events;

    events = xEventGroupWaitBits(
        app_event_group,

        APP_EVENT_BUTTON |
        APP_EVENT_SENSOR |
        APP_EVENT_AUTO,

        pdTRUE,         /* clear bits sau khi lấy */
        pdFALSE,        /* chỉ cần 1 event */
        pdMS_TO_TICKS(1000)   /* không có event -> ngủ */
    );


    /* --------------------------------------------------------
     * BUTTON
     * -------------------------------------------------------- */

    if (events & APP_EVENT_BUTTON)
    {
        handleButtons();
    }


    /* --------------------------------------------------------
     * SENSOR
     * -------------------------------------------------------- */

    if (events & APP_EVENT_SENSOR)
    {
        read_sensors();

        if (sensorReady)
        {
            syncSensors();
        }
    }


    /* --------------------------------------------------------
     * AUTO CONTROL
     * -------------------------------------------------------- */

    if (events & APP_EVENT_AUTO)
    {
        autoControl();
    }
}

/* ============================================================
 * APP TASK
 * ============================================================ */

static void app_task(void *arg)
{
    ESP_LOGI(TAG, "Application task started");

    while (1)
    {
        app_process();
    }
}


static void app_watchdog_init(void)
{
    esp_task_wdt_config_t twdt_config =
    {
        .timeout_ms = 5000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };

    ESP_ERROR_CHECK(
        esp_task_wdt_init(&twdt_config)
    );
}

static void app_task(void *arg)
{
    ESP_LOGI(TAG, "Application task started");

    ESP_ERROR_CHECK(
        esp_task_wdt_add(NULL)
    );

    while (1)
    {
        app_process();

        esp_task_wdt_reset();
    }
}


void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        /* If you only want to open more logs in the wifi module, you need to make the max level greater than the default level,
         * and call esp_log_level_set() before esp_wifi_init() to improve the log level of the wifi module. */
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    app_event_group = xEventGroupCreate();

    if (app_event_group == NULL)
    {
        ESP_LOGE(TAG, "Failed to create Event Group");

        /*
         * Không thể tiếp tục nếu không có Event Group.
         */
        abort();
    }

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta(WIFI_SSID, WIFI_PASSWORD);
    input_gpio_create(BTN_MODE, 2);
    input_gpio_create(BTN_PUMP, 2);
    input_gpio_create(BTN_FAN, 2);
    input_gpio_create(BTN_LED, 2);

    output_gpio_create(LED_PIN);
    output_gpio_create(FAN_PIN);
    output_gpio_create(PUMP_PIN);


    DHT11_Init(DHT_PIN);
    PH_SENSOR_Init(PH_SENSOR_PIN);
    SOIL_SENSOR_Init(SOIL_PIN);

    http_set_callback(app_http_callback);

    http_set_sse_connect_callback(app_sse_connected);

    read_sensors();

    app_timer_init();
    /* --------------------------------------------------------
     * 6. Tạo Application Task
     * -------------------------------------------------------- */

    BaseType_t ret = xTaskCreate(
        app_task,
        "app_task",
        4096,
        NULL,
        5,
        NULL
    );

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create app_task");
        abort();
    }
    ESP_LOGI(TAG, "Application started");
}
