set(srcs "esp32libfun.cpp")
set(requires)
set(priv_requires)
set(include_dirs "include")

if(CONFIG_ESP32LIBFUN_SERIAL)
    list(APPEND requires esp32libfun_serial)
    list(APPEND priv_requires esp32libfun_serial)
    list(APPEND include_dirs "../esp32libfun_serial/include")
endif()

if(CONFIG_ESP32LIBFUN_AT)
    list(APPEND requires esp32libfun_at)
    list(APPEND priv_requires esp32libfun_at)
    list(APPEND include_dirs "../esp32libfun_at/include")
endif()

if(CONFIG_ESP32LIBFUN_DELAY)
    list(APPEND requires esp32libfun_delay)
    list(APPEND priv_requires esp32libfun_delay)
    list(APPEND include_dirs "../esp32libfun_delay/include")
endif()

if(CONFIG_ESP32LIBFUN_GPIO)
    list(APPEND requires esp32libfun_gpio)
    list(APPEND requires esp_driver_gpio)
    list(APPEND priv_requires esp32libfun_gpio)
    list(APPEND include_dirs "../esp32libfun_gpio/include")
endif()

if(CONFIG_ESP32LIBFUN_I2C)
    list(APPEND requires esp32libfun_i2c)
    list(APPEND priv_requires esp32libfun_i2c)
    list(APPEND include_dirs "../esp32libfun_i2c/include")
endif()

if(CONFIG_ESP32LIBFUN_RUNTIME)
    list(APPEND requires esp32libfun_runtime)
    list(APPEND priv_requires esp32libfun_runtime)
    list(APPEND include_dirs "../esp32libfun_runtime/include")
endif()

if(CONFIG_ESP32LIBFUN_WIFI_STA)
    list(APPEND requires esp32libfun_wifi_sta)
    list(APPEND priv_requires esp32libfun_wifi_sta)
    list(APPEND include_dirs "../esp32libfun_wifi_sta/include")
endif()

if(CONFIG_ESP32LIBFUN_WEBSERVER)
    list(APPEND requires esp32libfun_webserver)
    list(APPEND requires esp_http_server)
    list(APPEND priv_requires esp32libfun_webserver)
    list(APPEND include_dirs "../esp32libfun_webserver/include")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${include_dirs}
    PRIV_REQUIRES ${priv_requires}
    REQUIRES ${requires}
)
