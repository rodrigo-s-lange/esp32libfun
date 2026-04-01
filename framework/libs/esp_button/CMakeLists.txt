idf_component_register(
    SRCS "esp_button.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp32libfun_gpio esp_timer freertos
)
