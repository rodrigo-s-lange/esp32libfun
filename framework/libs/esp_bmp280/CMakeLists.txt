idf_component_register(
    SRCS "esp_bmp280.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos
    PRIV_REQUIRES esp32libfun_i2c
)
