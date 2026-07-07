set(srcs "esp32libfun_w5500.cpp" "esp32libfun_w5500_at.cpp")
set(requires esp_eth esp_event esp_netif esp_driver_spi esp_driver_gpio lwip)

if(CONFIG_ESP32LIBFUN_AT)
    list(APPEND requires esp32libfun_at)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES ${requires}
    PRIV_REQUIRES log
)

if(CONFIG_ESP32LIBFUN_AT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP32LIBFUN_HAS_AT=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP32LIBFUN_HAS_AT=0)
endif()
