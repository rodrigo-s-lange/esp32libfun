idf_component_register(
    SRCS "esp_pca9685.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp32libfun_i2c
)
