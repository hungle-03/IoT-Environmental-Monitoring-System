#include "http_sever_main.h"
/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/


#include <esp_log.h>
#include <sys/param.h>
#include "esp_netif.h"
#include <esp_http_server.h>
#include "esp_event.h"
#include <esp_wifi.h>
#include <esp_system.h>
#include "esp_eth.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/time.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"



#ifndef CONFIG_EXAMPLE_ENABLE_SSE_HANDLER
#define CONFIG_EXAMPLE_ENABLE_SSE_HANDLER 1
#endif

#define EXAMPLE_HTTP_QUERY_KEY_MAX_LEN  (64)

static httpd_handle_t server = NULL;

static httpd_callback_t http_callback = NULL;

static http_sse_connect_callback_t sse_connect_callback = NULL;

static const char *TAG = "http_server";

#define HTTP_RESPONSE        128
#define SSE_MESSAGE_SIZE     256
#define SSE_QUEUE_LENGTH     8

typedef struct
{
    char data[SSE_MESSAGE_SIZE];

} sse_message_t;

static QueueHandle_t sse_queue = NULL;

static SemaphoreHandle_t sse_client_mutex = NULL;

static uint8_t sse_client_connected = 0;



#define HTTP_RESPONSE 128
/* ============================================================
 * HTTP CONTEXT
 * ============================================================ */

/*
 * Mỗi URI sẽ có một context riêng.
 *
 * Ví dụ:
 *      SW1     -> { HTTPD_TYPE_SW,            1 }
 *      SW2     -> { HTTPD_TYPE_SW,            2 }
 *      SLIDER1 -> { HTTPD_TYPE_SLIDER,        1 }
 *      DHT11   -> { HTTPD_TYPE_DATA_SENSORS,  1 }
 *
 * Context phải tồn tại lâu dài nên dùng static const.
 */

    static const httpd_context_t sw1_context =
    {
        .type = HTTPD_TYPE_SW,
        .id   = 1
    };

    static const httpd_context_t sw2_context =
    {
        .type = HTTPD_TYPE_SW,
        .id   = 2
    };

    static const httpd_context_t sw3_context =
    {
        .type = HTTPD_TYPE_SW,
        .id   = 3
    };

    static const httpd_context_t sw4_context =
    {
        .type = HTTPD_TYPE_SW,
        .id   = 4
    };

    static const httpd_context_t slider1_context = // set min/max temperature
    {
        .type = HTTPD_TYPE_SLIDER,
        .id   = 1
    };

    static const httpd_context_t slider2_context = // set min/max humidity
    {
        .type = HTTPD_TYPE_SLIDER,
        .id   = 2
    };

    static const httpd_context_t slider3_context = // set min/max soil
    {
        .type = HTTPD_TYPE_SLIDER,
        .id   = 3
    };

    static const httpd_context_t dht11_context =
    {
        .type = HTTPD_TYPE_DATA_SENSORS,
        .id   = 1
    };

    static const httpd_context_t soil_context =
    {
        .type = HTTPD_TYPE_DATA_SENSORS,
        .id   = 2
    };

    static const httpd_context_t ph_context =
    {
        .type = HTTPD_TYPE_DATA_SENSORS,
        .id   = 3
    };

// extern const uint8_t anh_jpg_start[] asm("_binary_anh_jpg_start");
// extern const uint8_t anh_jpg_end[]   asm("_binary_anh_jpg_end");


extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

#if CONFIG_EXAMPLE_BASIC_AUTH

typedef struct {
    char    *username;
    char    *password;
} basic_auth_info_t;

#define HTTPD_401      "401 UNAUTHORIZED"           /*!< HTTP Response 401 */

static char *http_auth_basic(const char *username, const char *password)
{
    size_t out;
    char *user_info = NULL;
    char *digest = NULL;
    size_t n = 0;
    int rc = asprintf(&user_info, "%s:%s", username, password);
    if (rc < 0) {
        ESP_LOGE(TAG, "asprintf() returned: %d", rc);
        return NULL;
    }

    if (!user_info) {
        ESP_LOGE(TAG, "No enough memory for user information");
        return NULL;
    }
    esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info, strlen(user_info));

    /* 6: The length of the "Basic " string
     * n: Number of bytes for a base64 encode format
     * 1: Number of bytes for a reserved which be used to fill zero
    */
    digest = calloc(1, 6 + n + 1);
    if (digest) {
        strcpy(digest, "Basic ");
        esp_crypto_base64_encode((unsigned char *)digest + 6, n, &out, (const unsigned char *)user_info, strlen(user_info));
    }
    free(user_info);
    return digest;
}

/* An HTTP GET handler */
static esp_err_t basic_auth_get_handler(httpd_req_t *req)
{
    char *buf = NULL;
    size_t buf_len = 0;
    basic_auth_info_t *basic_auth_info = req->user_ctx;

    buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
    if (buf_len > 1) {
        buf = calloc(1, buf_len);
        if (!buf) {
            ESP_LOGE(TAG, "No enough memory for basic authorization");
            return ESP_ERR_NO_MEM;
        }

        if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Authorization: %s", buf);
        } else {
            ESP_LOGE(TAG, "No auth value received");
        }

        char *auth_credentials = http_auth_basic(basic_auth_info->username, basic_auth_info->password);
        if (!auth_credentials) {
            ESP_LOGE(TAG, "No enough memory for basic authorization credentials");
            free(buf);
            return ESP_ERR_NO_MEM;
        }

        if (strncmp(auth_credentials, buf, buf_len)) {
            ESP_LOGE(TAG, "Not authenticated");
            httpd_resp_set_status(req, HTTPD_401);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
            httpd_resp_send(req, NULL, 0);
        } else {
            ESP_LOGI(TAG, "Authenticated!");
            char *basic_auth_resp = NULL;
            httpd_resp_set_status(req, HTTPD_200);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            int rc = asprintf(&basic_auth_resp, "{\"authenticated\": true,\"user\": \"%s\"}", basic_auth_info->username);
            if (rc < 0) {
                ESP_LOGE(TAG, "asprintf() returned: %d", rc);
                free(auth_credentials);
                return ESP_FAIL;
            }
            if (!basic_auth_resp) {
                ESP_LOGE(TAG, "No enough memory for basic authorization response");
                free(auth_credentials);
                free(buf);
                return ESP_ERR_NO_MEM;
            }
            httpd_resp_send(req, basic_auth_resp, strlen(basic_auth_resp));
            free(basic_auth_resp);
        }
        free(auth_credentials);
        free(buf);
    } else {
        ESP_LOGE(TAG, "No auth header received");
        httpd_resp_set_status(req, HTTPD_401);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Connection", "keep-alive");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
        httpd_resp_send(req, NULL, 0);
    }

    return ESP_OK;
}

static httpd_uri_t basic_auth = {
    .uri       = "/basic_auth",
    .method    = HTTP_GET,
    .handler   = basic_auth_get_handler,
};

static void httpd_register_basic_auth(httpd_handle_t server)
{
    basic_auth_info_t *basic_auth_info = calloc(1, sizeof(basic_auth_info_t));
    if (basic_auth_info) {
        basic_auth_info->username = CONFIG_EXAMPLE_BASIC_AUTH_USERNAME;
        basic_auth_info->password = CONFIG_EXAMPLE_BASIC_AUTH_PASSWORD;

        basic_auth.user_ctx = basic_auth_info;
        httpd_register_uri_handler(server, &basic_auth);
    }
}
#endif

/* An HTTP GET handler */
static esp_err_t http_get_handler(httpd_req_t *req)
{

    // const char* resp_str = (const char*) "Hello World!";
    // httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

    // httpd_resp_set_type(req, "image/jpeg");
    // httpd_resp_send(req, (const char *)anh_jpg_start, anh_jpg_end - anh_jpg_start);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

static const httpd_uri_t get_data = {
    .uri       = "/get_data",
    .method    = HTTP_GET,
    .handler   = http_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};

/* ============================================================
 * COMMON HTTP POST HANDLER
 * ============================================================ */

/*
 * Đây là handler chung cho:
 *
 *      /switch1
 *      /switch2
 *      /slider1
 *      ...
 *
 * Handler không cần biết cụ thể URI nào.
 *
 * Nó lấy thông tin thiết bị từ:
 *
 *      req->user_ctx
 *
 */

static esp_err_t http_post_handler(httpd_req_t *req)
{
    const httpd_context_t *ctx =(const httpd_context_t *)req->user_ctx;

    if (ctx == NULL)
    {
        ESP_LOGE(TAG, "HTTP context is NULL");
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"HTTP context is NULL");
        return ESP_FAIL;
    }

    if (req->content_len <= 0 ||
        req->content_len >= SSE_MESSAGE_SIZE)
    {
        ESP_LOGE(TAG, "Invalid POST body length: %d",req->content_len);

        httpd_resp_send_err( req,HTTPD_400_BAD_REQUEST,"Invalid request body");

        return ESP_FAIL;
    }

    char body[SSE_MESSAGE_SIZE];
    size_t received = 0;

    while (received < req->content_len)
    {
        int ret = httpd_req_recv( req, body + received, req->content_len - received);

        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            continue;
        }

        if (ret <= 0)
        {
            ESP_LOGE(TAG, "httpd_req_recv() failed");
            return ESP_FAIL;
        }

        received += ret;
    }

    body[received] = '\0';

    ESP_LOGI(TAG,"POST type=%d id=%d data=%s",ctx->type,ctx->id,body);

    char response[HTTP_RESPONSE];
    response[0] = '\0';

    if (http_callback != NULL)
    {
        esp_err_t ret = http_callback(body, received,ctx,response,sizeof(response));

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Application callback failed: %s",esp_err_to_name(ret));

            httpd_resp_send_err( req,HTTPD_500_INTERNAL_SERVER_ERROR, "Application callback failed");

            return ESP_FAIL;
        }
    }

    if (response[0] == '\0')
    {
        snprintf(response,sizeof(response), "{\"status\":\"ok\"}");
    }

    httpd_resp_set_type(req, "application/json");

    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN );
}

/* ============================================================
 * POST URI
 * ============================================================ */

    /*
    * Switch 1
    */
    static const httpd_uri_t switch1_uri =
    {
        .uri      = "/switch1",
        .method   = HTTP_POST,
        .handler  = http_post_handler,
        .user_ctx = (void *)&sw1_context
    };

    /*
    * Switch 2
    */
    static const httpd_uri_t switch2_uri =
    {
        .uri      = "/switch2",
        .method   = HTTP_POST,
        .handler  = http_post_handler,
        .user_ctx = (void *)&sw2_context
    };

    /*
    * Switch 3
    */
    static const httpd_uri_t switch3_uri =
    {
        .uri      = "/switch3",
        .method   = HTTP_POST,
        .handler  = http_post_handler,
        .user_ctx = (void *)&sw3_context
    };

    /*
    * Switch 4
    */
    static const httpd_uri_t switch4_uri =
    {
        .uri      = "/switch4",
        .method   = HTTP_POST,
        .handler  = http_post_handler,
        .user_ctx = (void *)&sw4_context
    };

    /*
    * slider 1
    */
    static const httpd_uri_t slider1_uri =
    {
        .uri      = "/slider1",
        .method   = HTTP_POST,
        .handler  = http_post_handler,
        .user_ctx = (void *)&slider1_context
    };

    /*
    * slider 2
    */
    static const httpd_uri_t slider2_uri =
    {
        .uri      = "/slider2",
        .method   = HTTP_POST,
        .handler  = http_post_handler,
        .user_ctx = (void *)&slider2_context
    };

        /*
    * slider 3
    */
    static const httpd_uri_t slider3_uri =
    {
        .uri      = "/slider3",
        .method   = HTTP_POST,
        .handler  = http_post_handler,
        .user_ctx = (void *)&slider3_context
    };

/* ============================================================
 * SENSOR GET HANDLER
 * ============================================================ */

static esp_err_t http_sensor_get_handler(httpd_req_t *req)
{
    const httpd_context_t *ctx =(const httpd_context_t *)req->user_ctx;
    if (ctx == NULL)
    {
        ESP_LOGE(TAG,"Sensor context is NULL");

        return ESP_FAIL;
    }

    ESP_LOGI(TAG,"GET sensor type=%d id=%d",ctx->type,ctx->id);

    char response[HTTP_RESPONSE];

    response[0] = '\0';

    /* ========================================================
     * CALLBACK
     * ======================================================== */

    if (http_callback != NULL)
    {
        esp_err_t ret = http_callback(NULL, 0,ctx,response,sizeof(response));

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG,"Sensor callback failed: %s", esp_err_to_name(ret) );

            httpd_resp_set_status(req,"500 Internal Server Error");

            return httpd_resp_send(req,"{\"status\":\"error\"}",HTTPD_RESP_USE_STRLEN);
        }
    }

    /* ========================================================
     * RESPONSE
     * ======================================================== */

    if (response[0] == '\0')
    {
        strcpy( response, "{\"status\":\"no_data\"}");
    }

    httpd_resp_set_type( req,"application/json");

    return httpd_resp_send( req,  response, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}
    /*
    * dht11
    */
    static const httpd_uri_t dht11_uri =
    {
        .uri      = "/dht11",
        .method   = HTTP_GET,
        .handler  = http_sensor_get_handler,
        .user_ctx = (void *)&dht11_context
    };

    /*
    * soil
    */
    static const httpd_uri_t soil_uri =
    {
        .uri      = "/soil",
        .method   = HTTP_GET,
        .handler  = http_sensor_get_handler,
        .user_ctx = (void *)&soil_context
    };

    /*
    * ph
    */
    static const httpd_uri_t ph_uri =
    {
        .uri      = "/ph",
        .method   = HTTP_GET,
        .handler  = http_sensor_get_handler,
        .user_ctx = (void *)&ph_context
    };


esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/hello", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/hello URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    } else if (strcmp("/echo", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
        /* Return ESP_FAIL to close underlying socket */
        return ESP_FAIL;
    }
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}


#if CONFIG_EXAMPLE_ENABLE_SSE_HANDLER

static esp_err_t sse_handler(httpd_req_t *req)
{
    if (sse_queue == NULL)
    {
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "SSE queue is not initialized"
        );

        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "X-Accel-Buffering", "no");

    /*
     * Chỉ cho phép một SSE client tại một thời điểm.
     */
    if (xSemaphoreTake(
            sse_client_mutex,
            pdMS_TO_TICKS(100)
        ) != pdTRUE)
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");

        httpd_resp_send(
            req,
            "SSE client is already connected",
            HTTPD_RESP_USE_STRLEN);

        return ESP_FAIL;
    }

    if (sse_client_connected)
    {
        xSemaphoreGive(sse_client_mutex);

        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");

        httpd_resp_send(
            req,
            "Only one SSE client is supported",
            HTTPD_RESP_USE_STRLEN
        );

        return ESP_FAIL;
    }

    sse_client_connected = 1;

    xSemaphoreGive(sse_client_mutex);
    
    if (sse_connect_callback != NULL)
    {
        sse_connect_callback();
    }

    /*
     * Gửi sự kiện kết nối ban đầu.
     */
    const char *connected_event =
        "event: connected\n"
        "data: {\"status\":\"connected\"}\n\n";

    if (httpd_resp_send_chunk(
            req,
            connected_event,
            strlen(connected_event)
        ) != ESP_OK)
    {
        goto sse_disconnect;
    }

    while (1)
    {
        sse_message_t message;

        if (xQueueReceive(
                sse_queue,
                &message,
                portMAX_DELAY
            ) != pdTRUE)
        {
            continue;
        }

        char event_buffer[SSE_MESSAGE_SIZE + 32];

        int len = snprintf(
            event_buffer,
            sizeof(event_buffer),
            "data: %s\n\n",
            message.data
        );

        if (len <= 0 || len >= sizeof(event_buffer))
        {
            ESP_LOGE(TAG, "SSE event is too long");
            continue;
        }

        esp_err_t ret = httpd_resp_send_chunk(
            req,
            event_buffer,
            len
        );

        if (ret != ESP_OK)
        {
            ESP_LOGW(
                TAG,
                "SSE client disconnected: %s",
                esp_err_to_name(ret)
            );

            break;
        }
    }

    sse_disconnect:

        xSemaphoreTake(
            sse_client_mutex,
            portMAX_DELAY
        );

        sse_client_connected = 0;

        xSemaphoreGive(sse_client_mutex);

        httpd_resp_send_chunk(req, NULL, 0);

        return ESP_OK;
    }

    static const httpd_uri_t sse_uri =
    {
        .uri      = "/sse",
        .method   = HTTP_GET,
        .handler  = sse_handler,
        .user_ctx = NULL
    };

#endif



void start_webserver(void)
{

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.lru_purge_enable = true;
#if CONFIG_EXAMPLE_ENABLE_SSE_HANDLER

    sse_queue = xQueueCreate(
        SSE_QUEUE_LENGTH,
        sizeof(sse_message_t)
    );

    if (sse_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create SSE queue");
        return;
    }

    sse_client_mutex = xSemaphoreCreateMutex();

    if (sse_client_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create SSE mutex");

        vQueueDelete(sse_queue);
        sse_queue = NULL;

        return;
    }

    sse_client_connected = 0;

#endif
    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &get_data);
        httpd_register_uri_handler(server, &switch1_uri);
        httpd_register_uri_handler(server, &switch2_uri);
        httpd_register_uri_handler(server, &switch3_uri);
        httpd_register_uri_handler(server, &switch4_uri);

        httpd_register_uri_handler(server, &slider1_uri);
        httpd_register_uri_handler(server, &slider2_uri);
        httpd_register_uri_handler(server, &slider3_uri);

        httpd_register_uri_handler(server, &dht11_uri);
        httpd_register_uri_handler(server, &soil_uri);
        httpd_register_uri_handler(server, &ph_uri);


#if CONFIG_EXAMPLE_ENABLE_SSE_HANDLER
        httpd_register_uri_handler(server, &sse_uri);
#endif

        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    } else {
        ESP_LOGI(TAG, "Error starting server!");
    }
}

void stop_webserver(void)
{
    if (server != NULL)
    {
        httpd_stop(server);
        server = NULL;
    }

#if CONFIG_EXAMPLE_ENABLE_SSE_HANDLER

    if (sse_queue != NULL)
    {
        vQueueDelete(sse_queue);
        sse_queue = NULL;
    }

    if (sse_client_mutex != NULL)
    {
        vSemaphoreDelete(sse_client_mutex);
        sse_client_mutex = NULL;
    }
    sse_client_connected = 0;

#endif
}

void http_set_callback(httpd_callback_t callback)
{
    http_callback = callback;
}


esp_err_t http_sse_send(const char *data)
{
#if CONFIG_EXAMPLE_ENABLE_SSE_HANDLER

    if (data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (sse_queue == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (strlen(data) >= SSE_MESSAGE_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!http_sse_is_connected())
    {
        /*
         * Không có Web client thì không cần xếp hàng.
         */
        return ESP_OK;
    }

    sse_message_t message;

    memset(&message, 0, sizeof(message));

    snprintf( message.data, sizeof(message.data), "%s", data);

    if (xQueueSend(  sse_queue, &message,  0) != pdTRUE)
    {
        ESP_LOGW(TAG, "SSE queue is full");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;

#else

    (void)data;
    return ESP_ERR_NOT_SUPPORTED;

#endif
}


uint8_t http_sse_is_connected(void)
{
#if CONFIG_EXAMPLE_ENABLE_SSE_HANDLER

    uint8_t connected;

    if (sse_client_mutex == NULL)
    {
        return 0;
    }

    xSemaphoreTake(
        sse_client_mutex, portMAX_DELAY);

    connected = sse_client_connected;

    xSemaphoreGive(sse_client_mutex);

    return connected;

#else

    return 0;

#endif
}



void http_set_sse_connect_callback(
    http_sse_connect_callback_t callback
)
{
    sse_connect_callback = callback;
}