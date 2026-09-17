# ESP32 IoT Dashboard — ESP-IDF

Hệ thống giám sát và điều khiển thiết bị sử dụng ESP32, ESP-IDF và giao diện Web Dashboard.

Hệ thống hỗ trợ:

* Giám sát nhiệt độ
* Giám sát độ ẩm không khí
* Giám sát độ ẩm đất
* Giám sát pH
* Điều khiển LED
* Điều khiển FAN
* Điều khiển PUMP
* Chế độ AUTO / MANUAL
* Điều chỉnh ngưỡng MIN / MAX từ Web
* HTTP POST để điều khiển
* SSE để cập nhật dữ liệu realtime
* Event Group để đồng bộ các sự kiện
* Debounce nút nhấn
* Task Watchdog để phát hiện task bị treo
* Stack monitoring để phát hiện nguy cơ thiếu stack

---

# 1. Kiến trúc hệ thống

```text
                         ┌──────────────────────┐
                         │      Web Browser     │
                         │   ESP32 Dashboard    │
                         └──────────┬───────────┘
                                    │
                         HTTP POST / SSE
                                    │
                                    ▼
                         ┌──────────────────────┐
                         │     HTTP Server      │
                         │                      │
                         │ /switch1 ... /4     │
                         │ /slider1 ... /3     │
                         │ /dht11 /soil /ph    │
                         │ /sse                 │
                         └──────────┬───────────┘
                                    │
                                    ▼
                         ┌──────────────────────┐
                         │   Application Layer  │
                         │                      │
                         │ app_set_switch_state │
                         │ app_http_callback    │
                         │ handleButtons        │
                         │ autoControl           │
                         └──────────┬───────────┘
                                    │
                          Application State
                                    │
                ┌───────────────────┼───────────────────┐
                ▼                   ▼                   ▼
              LED                  FAN                 PUMP
```

Application state là nguồn dữ liệu chính.

Button, Web và AUTO đều đi qua application layer thay vì điều khiển GPIO độc lập.

```text
BUTTON ──────┐
             │
WEB ─────────┼──> Application State ──> GPIO
             │
AUTO ────────┘
```

---

# 2. Phần cứng

## GPIO

| Chức năng   |   GPIO |
| ----------- | -----: |
| LED         |  GPIO2 |
| FAN         |  GPIO5 |
| PUMP        | GPIO18 |
| Button MODE | GPIO13 |
| Button PUMP | GPIO27 |
| Button FAN  | GPIO14 |
| Button LED  | GPIO12 |
| Soil ADC    | GPIO35 |
| DHT         | GPIO33 |
| pH ADC      | GPIO32 |

---

# 3. Trạng thái ứng dụng

Các trạng thái chính:

```c
static bool modeAuto = false;
static bool ledState = false;
static bool fanState = false;
static bool pumpState = false;
```

Ý nghĩa:

```text
modeAuto = false
    → MANUAL

modeAuto = true
    → AUTO
```

Trong MANUAL:

```text
WEB / BUTTON
     ↓
LED / FAN / PUMP
```

Trong AUTO:

```text
Sensor
   ↓
autoControl()
   ↓
LED / FAN / PUMP
```

Các nút điều khiển FAN, PUMP và LED bị khóa khi AUTO.

---

# 4. Nguồn thay đổi trạng thái

Sử dụng:

```c
typedef enum
{
    STATE_SOURCE_BUTTON,
    STATE_SOURCE_WEB,
    STATE_SOURCE_AUTO
} state_source_t;
```

Có ba nguồn:

| Source | Ý nghĩa                 |
| ------ | ----------------------- |
| BUTTON | Điều khiển bằng nút     |
| WEB    | Điều khiển từ Dashboard |
| AUTO   | Điều khiển tự động      |

Hàm trung tâm:

```text
app_set_switch_state()
```

Tất cả các nguồn điều khiển nên đi qua hàm này.

---

# 5. Chế độ AUTO / MANUAL

Trong AUTO:

```text
SW1 = AUTO
```

Web và button không được phép điều khiển trực tiếp:

```text
SW2 → LED
SW3 → FAN
SW4 → PUMP
```

Application layer kiểm tra:

```text
AUTO
 │
 ├── WEB output command → reject
 └── BUTTON output command → reject
```

Nhưng AUTO controller được phép thay đổi:

```text
AUTO controller
      ↓
app_set_switch_state()
      ↓
FAN / PUMP
```

Điều này giúp tránh việc Web hoặc button ghi đè lên chế độ tự động.

---

# 6. HTTP API

## Switch

```text
POST /switch1
POST /switch2
POST /switch3
POST /switch4
```

Mapping:

| Endpoint   | ID | Chức năng     |
| ---------- | -: | ------------- |
| `/switch1` |  1 | AUTO / MANUAL |
| `/switch2` |  2 | LED           |
| `/switch3` |  3 | FAN           |
| `/switch4` |  4 | PUMP          |

Body:

```text
0
```

hoặc:

```text
1
```

---

# 7. Slider API

```text
POST /slider1
POST /slider2
POST /slider3
```

Mapping:

| Endpoint   | ID | Sensor      |
| ---------- | -: | ----------- |
| `/slider1` |  1 | Temperature |
| `/slider2` |  2 | Humidity    |
| `/slider3` |  3 | Soil        |

Body:

```text
MIN:MAX
```

Ví dụ:

```text
30:40
```

nghĩa là:

```text
MIN = 30
MAX = 40
```

Các biến:

```text
Temperature:
newTempMin
newTempMax

Humidity:
newHumiMin
newHumiMax

Soil:
newSoilMin
newSoilMax
```

Luôn đảm bảo:

```text
MIN < MAX
```

---

# 8. Sensor API

Các endpoint:

```text
GET /dht11
GET /soil
GET /ph
```

Dữ liệu sensor được lưu trong application state.

Không đọc sensor trực tiếp trong HTTP callback.

Kiến trúc:

```text
Sensor task
    ↓
read_sensors()
    ↓
Application state
    ↓
HTTP / SSE
```

Điều này giúp HTTP callback xử lý nhanh và tránh bị block.

---

# 9. SSE

Browser kết nối:

```text
GET /sse
```

ESP32 sử dụng Server-Sent Events để gửi dữ liệu từ ESP32 → Browser.

Ví dụ switch:

```json
{
    "type": "sw",
    "id": 3,
    "value": 1
}
```

Sensor:

```json
{
    "type": "sensor",
    "temperature": 28.50,
    "humidity": 72.00,
    "soil": 45.00,
    "ph": 6.80
}
```

State:

```json
{
    "type": "state",
    "mode": 0,
    "pump": 0,
    "fan": 0,
    "led": 0
}
```

Slider:

```json
{
    "type": "slider",
    "id": 1,
    "min": 30,
    "max": 40
}
```

---

# 10. Đồng bộ khi Browser kết nối

Khi SSE client kết nối:

```text
Browser
   ↓
/sse
   ↓
app_sse_connected()
   ↓
syncAll()
   ↓
syncSensors()
```

Do đó Web Dashboard luôn nhận trạng thái hiện tại.

Không phụ thuộc vào việc trước đó có bỏ lỡ SSE message hay không.

---

# 11. Application Timer

Hệ thống sử dụng một `esp_timer`.

Timer không thực hiện công việc nặng.

Timer chỉ tạo event.

Chu kỳ:

```text
BUTTON = 20 ms
SENSOR = 2 s
AUTO   = 2 s
```

Quan hệ:

```text
20 ms × 100 = 2000 ms
```

---

# 12. Event Group

Sử dụng FreeRTOS Event Group:

```c
#define APP_EVENT_BUTTON   BIT0
#define APP_EVENT_SENSOR   BIT1
#define APP_EVENT_AUTO     BIT2
```

Timer:

```text
20 ms
 ↓
APP_EVENT_BUTTON

100 lần
 ↓
APP_EVENT_SENSOR
APP_EVENT_AUTO
```

---

# 13. Tại sao sử dụng Event Group?

Không sử dụng kiểu polling:

```text
while(1)
{
    check();
    delay(10);
}
```

Thay vào đó:

```text
app_task
    ↓
xEventGroupWaitBits()
    ↓
BLOCK
    ↓
event xuất hiện
    ↓
xử lý
    ↓
BLOCK tiếp
```

Ưu điểm:

* Giảm polling
* Giảm CPU usage
* Không phải kiểm tra flag liên tục
* Task có thể block khi không có việc
* Kiến trúc rõ ràng hơn
* Dễ mở rộng thêm event

---

# 14. Application Task

Application task xử lý:

```text
BUTTON
SENSOR
AUTO
```

Luồng:

```text
Event Group
     ↓
app_process()
     │
     ├── BUTTON
     │     ↓
     │ handleButtons()
     │
     ├── SENSOR
     │     ↓
     │ read_sensors()
     │     ↓
     │ syncSensors()
     │
     └── AUTO
           ↓
        autoControl()
```

---



# 15. AUTO Control

AUTO không điều khiển theo kiểu bật/tắt tại một ngưỡng duy nhất.

Sử dụng hysteresis.

## FAN

Bật khi:

```text
temperature > newTempMax
OR
humidity > newHumiMax
```

Tắt khi:

```text
temperature < newTempMin
AND
humidity < newHumiMin
```

Nếu nằm giữa MIN và MAX:

```text
giữ nguyên trạng thái
```

---

# 16. FAN hysteresis

Ví dụ:

```text
MIN = 30
MAX = 40
```

```text
Temperature

> 40
 │
 └── FAN ON

30 → 40
 │
 └── giữ trạng thái

< 30
 │
 └── FAN OFF
```

Không nên dùng:

```text
temperature > 30 → ON
temperature <= 30 → OFF
```

vì relay có thể đóng/ngắt liên tục khi nhiệt độ dao động quanh ngưỡng.

---

# 17. PUMP hysteresis

Bật:

```text
soilValue < newSoilMin
```

Tắt:

```text
soilValue > newSoilMax
```

Khoảng:

```text
MIN → MAX
```

là vùng giữ trạng thái.

Điều này giúp:

* Giảm số lần đóng/ngắt relay
* Giảm nhiễu
* Tăng tuổi thọ relay
* Điều khiển ổn định hơn

---

# 18. Sensor reading

Hàm:

```text
read_sensors()
```

đọc:

```text
DHT
Soil
pH
```

Giá trị được lưu vào application state:

```text
temperature
humidity
soilValue
phValue
```

Biến:

```text
sensorReady
```

được sử dụng để xác định sensor đã có dữ liệu hợp lệ.

AUTO chỉ hoạt động khi:

```text
modeAuto == true
AND
sensorReady == true


# 21. Tần suất sensor

Không đọc sensor liên tục.

Hiện tại:

```text
Sensor = 2 giây/lần
```

Điều này phù hợp với hệ thống giám sát môi trường vì nhiệt độ và độ ẩm không thay đổi quá nhanh.

DHT11 đặc biệt không cần đọc với tần suất cao.

