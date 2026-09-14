idf_component_register(SRCS "main.cpp" "${COMPOSER_ROOT}/adapter_esp_pixels/test/test_adapter.cpp"
    INCLUDE_DIRS "."
    REQUIRES adapter_esp_pixels esp32libfun_serial log)
