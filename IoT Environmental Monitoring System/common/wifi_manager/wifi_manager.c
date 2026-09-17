#include "wifi_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"


/* ============================================================
 * WIFI CONFIG
 * ============================================================ */



#define WIFI_MAX_RETRY  5


/* ============================================================
 * EVENT GROUP
 * ============================================================ */

static EventGroupHandle_t s_wifi_event_group;


/* ============================================================
 * EVENT BITS
 * ============================================================ */

#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1


/* ============================================================
 * TAG
 * ============================================================ */

static const char *TAG = "WIFI";


/* ============================================================
 * RETRY
 * ============================================================ */

static int s_retry_num = 0;


/* ============================================================
 * EVENT HANDLER
 * ============================================================ */

static void event_handler(void *arg,
                          esp_event_base_t event_base,
                          int32_t event_id,
                          void *event_data)
{
    /*
     * WiFi started
     */
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }


    /*
     * WiFi disconnected
     */
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < WIFI_MAX_RETRY)
        {
            esp_wifi_connect();

            s_retry_num++;

            ESP_LOGI(
                TAG,
                "retry to connect to the AP"
            );
        }
        else
        {
            xEventGroupSetBits(
                s_wifi_event_group,
                WIFI_FAIL_BIT
            );

            ESP_LOGE(
                TAG,
                "connect to the AP fail"
            );
        }
    }


    /*
     * Got IP
     */
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;


        ESP_LOGI(
            TAG,
            "got ip:" IPSTR,
            IP2STR(&event->ip_info.ip)
        );


        s_retry_num = 0;


        xEventGroupSetBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}


/* ============================================================
 * WIFI INIT STA
 * ============================================================ */

void wifi_init_sta(const char *ssid, const char *password)
{
    /*
     * Create Event Group
     */
    s_wifi_event_group =
        xEventGroupCreate();


    /*
     * Initialize TCP/IP stack
     */
    ESP_ERROR_CHECK(
        esp_netif_init()
    );


    /*
     * Create default event loop
     */
    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );


    /*
     * Create default WiFi STA
     */
    esp_netif_create_default_wifi_sta();
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    esp_netif_ip_info_t ip_info = {
        .ip.addr      = ESP_IP4TOADDR(192, 168, 1, 50),
        .gw.addr      = ESP_IP4TOADDR(192, 168, 1, 1),
        .netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0),
    };

    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(sta_netif));

    ESP_ERROR_CHECK(
        esp_netif_set_ip_info(sta_netif, &ip_info)
    );

    /*
     * Initialize WiFi driver
     */
    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();


    ESP_ERROR_CHECK(
        esp_wifi_init(&cfg)
    );


    /*
     * Register WiFi event
     */
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &event_handler,
            NULL,
            NULL
        )
    );


    /*
     * Register IP event
     */
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &event_handler,
            NULL,
            NULL
        )
    );


    /*
     * WiFi configuration
     */
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode =
                WIFI_AUTH_WPA2_PSK,

            .sae_pwe_h2e =
                WPA3_SAE_PWE_BOTH,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    /*
     * Set WiFi mode
     */
    ESP_ERROR_CHECK(
        esp_wifi_set_mode(
            WIFI_MODE_STA
        )
    );


    /*
     * Set WiFi configuration
     */
    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );


    /*
     * Start WiFi
     */
    ESP_ERROR_CHECK(
        esp_wifi_start()
    );


    ESP_LOGI(
        TAG,
        "wifi_init_sta finished."
    );


    /*
     * Wait for connection
     */
    EventBits_t bits =
        xEventGroupWaitBits(
            s_wifi_event_group,

            WIFI_CONNECTED_BIT |
            WIFI_FAIL_BIT,

            pdFALSE,
            pdFALSE,

            portMAX_DELAY
        );


    /*
     * Check result
     */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(
            TAG,
            "connected to AP"
        );
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGE(
            TAG,
            "Failed to connect to AP"
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "UNEXPECTED EVENT"
        );
    }
}