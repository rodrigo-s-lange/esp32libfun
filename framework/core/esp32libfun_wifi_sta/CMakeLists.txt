idf_component_register(SRCS "esp32libfun_wifi_sta.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_event esp_netif esp_wifi lwip nvs_flash)
