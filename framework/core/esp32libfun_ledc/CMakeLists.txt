idf_component_register(SRCS "esp32libfun_ledc.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_gpio esp_driver_ledc)
