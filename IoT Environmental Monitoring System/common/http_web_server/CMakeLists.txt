idf_component_register(
    SRCS "http_sever_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_http_server
        esp_wifi
        esp_eth
        nvs_flash
)