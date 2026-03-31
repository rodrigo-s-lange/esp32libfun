set(srcs "esp32libfun.cpp")
set(requires)
set(priv_requires)
set(include_dirs "include")

if(CONFIG_ESP32LIBFUN_SERIAL)
    list(APPEND requires esp32libfun_serial)
    list(APPEND priv_requires esp32libfun_serial)
endif()

if(CONFIG_ESP32LIBFUN_AT)
    list(APPEND requires esp32libfun_at)
    list(APPEND priv_requires esp32libfun_at)
endif()

if(CONFIG_ESP32LIBFUN_DELAY)
    list(APPEND requires esp32libfun_delay)
    list(APPEND priv_requires esp32libfun_delay)
endif()

if(CONFIG_ESP32LIBFUN_GPIO)
    list(APPEND requires esp32libfun_gpio)
    list(APPEND requires esp_driver_gpio)
    list(APPEND priv_requires esp32libfun_gpio)
endif()

if(CONFIG_ESP32LIBFUN_BUTTON)
    list(APPEND requires esp32libfun_button)
    list(APPEND priv_requires esp32libfun_button)
endif()

if(CONFIG_ESP32LIBFUN_I2C)
    list(APPEND requires esp32libfun_i2c)
    list(APPEND priv_requires esp32libfun_i2c)
endif()

if(CONFIG_ESP32LIBFUN_RUNTIME)
    list(APPEND requires esp32libfun_runtime)
    list(APPEND priv_requires esp32libfun_runtime)
endif()

if(CONFIG_ESP32LIBFUN_WIFI_STA)
    list(APPEND requires esp32libfun_wifi_sta)
    list(APPEND priv_requires esp32libfun_wifi_sta)
endif()

if(CONFIG_ESP32LIBFUN_WEBSERVER)
    list(APPEND requires esp32libfun_webserver)
    list(APPEND requires esp_http_server)
    list(APPEND priv_requires esp32libfun_webserver)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${include_dirs}
    PRIV_REQUIRES ${priv_requires}
    REQUIRES ${requires}
)
