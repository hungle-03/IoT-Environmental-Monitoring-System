idf_component_register(SRCS "wifi_manager.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES
                            esp_wifi
                            esp_eth
                            nvs_flash
                       )
