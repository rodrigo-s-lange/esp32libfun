#pragma once

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// This aggregator uses explicit relative includes on purpose so the header stays
// self-contained and mirrors the core directory layout directly.
#if CONFIG_ESP32LIBFUN_SERIAL
#include "../../esp32libfun_serial/include/esp32libfun_serial.hpp"
#endif

#if CONFIG_ESP32LIBFUN_AT
#include "../../esp32libfun_at/include/esp32libfun_at.hpp"
#endif

#if CONFIG_ESP32LIBFUN_DELAY
#include "../../esp32libfun_delay/include/esp32libfun_delay.hpp"
#endif

#if CONFIG_ESP32LIBFUN_GPIO
#include "../../esp32libfun_gpio/include/esp32libfun_gpio.hpp"
#endif

#if CONFIG_ESP32LIBFUN_I2C
#include "../../esp32libfun_i2c/include/esp32libfun_i2c.hpp"
#endif

#if CONFIG_ESP32LIBFUN_WIFI_STA
#include "../../esp32libfun_wifi_sta/include/esp32libfun_wifi_sta.hpp"
#endif

#if CONFIG_ESP32LIBFUN_WEBSERVER
#include "../../esp32libfun_webserver/include/esp32libfun_webserver.hpp"
#endif

#define ESP32LIBFUN_VERSION "v0.0.0"
#define ESP32LIBFUN_VERSION_MAJOR 0
#define ESP32LIBFUN_VERSION_MINOR 0
#define ESP32LIBFUN_VERSION_PATCH 0
#define ESP32LIBFUN_IDF_BASELINE "v6.0.0"

const char *esp32libfun_major(void);
const char *esp32libfun_minor(void);
const char *esp32libfun_patch(void);
const char *esp32libfun_version(void);

void esp32libfun_init(void);
