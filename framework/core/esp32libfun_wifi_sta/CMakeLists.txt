set(srcs "esp32libfun_wifi_sta.cpp" "esp32libfun_wifi_sta_at.cpp")
set(requires esp_event esp_netif esp_wifi lwip nvs_flash)

if(CONFIG_ESP32LIBFUN_AT)
    list(APPEND requires esp32libfun_at)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES ${requires}
)

if(CONFIG_ESP32LIBFUN_AT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP32LIBFUN_HAS_AT=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP32LIBFUN_HAS_AT=0)
endif()
