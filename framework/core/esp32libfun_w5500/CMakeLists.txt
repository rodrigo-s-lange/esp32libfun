idf_component_register(
    SRCS "esp32libfun_w5500.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_eth esp_event esp_netif esp_driver_spi esp_driver_gpio
    PRIV_REQUIRES log
)
