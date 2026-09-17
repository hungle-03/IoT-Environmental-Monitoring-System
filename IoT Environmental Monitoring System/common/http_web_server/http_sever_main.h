
#ifndef HTTP_SEVER_MAIN_H
#define HTTP_SEVER_MAIN_H

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"


/* ============================================================
 * HTTP OBJECT TYPE
 * ============================================================ */

typedef enum
{
    HTTPD_TYPE_SW = 0,
    HTTPD_TYPE_SLIDER,
    HTTPD_TYPE_DATA_SENSORS

} httpd_type_t;


/* ============================================================
 * HTTP CONTEXT
 * ============================================================ */

/*
 * type:
 *      SW
 *      SLIDER
 *      SENSOR
 *
 * id:
 *      SW      : 1 -> 4
 *      SLIDER  : 1 -> 2
 *      SENSOR  : 1 -> DHT11
 *                2 -> Soil
 *                3 -> pH
 */

typedef struct
{
    httpd_type_t type;
    uint8_t id;

} httpd_context_t;


/* ============================================================
 * HTTP CALLBACK
 * ============================================================ */

/*
 * data:
 *      POST data.
 *      Với GET sensor -> NULL
 *
 * len:
 *      Độ dài data.
 *
 * ctx:
 *      Xác định đối tượng HTTP.
 *
 * response:
 *      Buffer để application tạo dữ liệu trả về cho Web.
 *
 * response_size:
 *      Kích thước response buffer.
 */

typedef esp_err_t (*httpd_callback_t)(
    const char *data,
    size_t len,
    const httpd_context_t *ctx,
    char *response,
    size_t response_size
);


/* ============================================================
 * SSE CONNECT CALLBACK
 * ============================================================ */

/*
 * Được gọi khi một trình duyệt vừa kết nối SSE thành công.
 *
 * Callback này thuộc application layer.
 *
 * Ví dụ trong app_main.c:
 *
 * void app_sse_connected(void)
 * {
 *     syncAll();
 *     syncSensors();
 * }
 */

typedef void (*http_sse_connect_callback_t)(void);


/* ============================================================
 * HTTP SERVER API
 * ============================================================ */

void start_webserver(void);

void stop_webserver(void);


/* ============================================================
 * HTTP CALLBACK API
 * ============================================================ */

/*
 * Đăng ký callback xử lý HTTP request.
 */
void http_set_callback(httpd_callback_t callback);


/* ============================================================
 * SSE API
 * ============================================================ */

/*
 * Đăng ký callback khi Web vừa kết nối SSE.
 */
void http_set_sse_connect_callback(
    http_sse_connect_callback_t callback
);


/*
 * Gửi dữ liệu từ ESP32 -> Web thông qua SSE.
 *
 * data:
 *      Chuỗi JSON.
 *
 * Ví dụ:
 *      {"type":"sw","id":1,"value":1}
 */
esp_err_t http_sse_send(const char *data);


/*
 * Kiểm tra hiện tại có trình duyệt
 * kết nối SSE hay không.
 *
 * return:
 *      1 -> Có client SSE
 *      0 -> Không có client SSE
 */
uint8_t http_sse_is_connected(void);


#endif /* HTTP_SEVER_MAIN_H */


/* dùng trong app_main.c
http_set_sse_connect_callback(app_sse_connected); // đăng ký callback khi Web vừa kết nối SSE

và:

void app_sse_connected(void) // thực hiện callback khi Web vừa kết nối SSE
{
    syncAll();
    syncSensors();
} */