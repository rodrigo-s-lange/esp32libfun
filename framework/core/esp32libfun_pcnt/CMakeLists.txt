idf_component_register(SRCS "esp32libfun_pcnt.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_gpio esp_driver_pcnt)
