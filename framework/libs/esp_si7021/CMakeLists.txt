idf_component_register(
    SRCS "esp_si7021.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos
    PRIV_REQUIRES esp32libfun_i2c
)
