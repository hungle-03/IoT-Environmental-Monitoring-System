idf_component_register(
    SRCS
        "app_main.c"

    PRIV_REQUIRES
        esp_timer
        http_web_server
        wifi_manager
        output_iot
        input_iot
        esp_wifi
        nvs_flash
        adc
        dht11
        soil_sensor
        ph_sensor
    INCLUDE_DIRS "."
)