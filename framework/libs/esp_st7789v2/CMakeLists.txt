idf_component_register(
    SRCS "esp_st7789v2.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp32libfun_spi
    PRIV_REQUIRES esp32libfun_gpio esp32libfun_delay
)
