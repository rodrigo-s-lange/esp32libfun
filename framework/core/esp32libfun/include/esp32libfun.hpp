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

#if CONFIG_ESP32LIBFUN_LEDC
#include "../../esp32libfun_ledc/include/esp32libfun_ledc.hpp"
#endif

#if CONFIG_ESP32LIBFUN_I2C
#include "../../esp32libfun_i2c/include/esp32libfun_i2c.hpp"
#endif

#if CONFIG_ESP32LIBFUN_PCNT
#include "../../esp32libfun_pcnt/include/esp32libfun_pcnt.hpp"
#endif

#if CONFIG_ESP32LIBFUN_SPI
#include "../../esp32libfun_spi/include/esp32libfun_spi.hpp"
#endif

#if CONFIG_ESP32LIBFUN_ADC
#include "../../esp32libfun_adc/include/esp32libfun_adc.hpp"
#endif

#if CONFIG_ESP32LIBFUN_GPTIMER
#include "../../esp32libfun_gptimer/include/esp32libfun_gptimer.hpp"
#endif

#if CONFIG_ESP32LIBFUN_RMT
#include "../../esp32libfun_rmt/include/esp32libfun_rmt.hpp"
#endif

#if CONFIG_ESP32LIBFUN_TWAI
#include "../../esp32libfun_twai/include/esp32libfun_twai.hpp"
#endif

#if CONFIG_ESP32LIBFUN_W5500
#include "../../esp32libfun_w5500/include/esp32libfun_w5500.hpp"
#endif

#if CONFIG_ESP32LIBFUN_LAN8720
#include "../../esp32libfun_lan8720/include/esp32libfun_lan8720.hpp"
#endif

#if CONFIG_ESP32LIBFUN_MCPWM
#include "../../esp32libfun_mcpwm/include/esp32libfun_mcpwm.hpp"
#endif

#if CONFIG_ESP32LIBFUN_WIFI_STA
#include "../../esp32libfun_wifi_sta/include/esp32libfun_wifi_sta.hpp"
#endif

#if CONFIG_ESP32LIBFUN_WEBSERVER
#include "../../esp32libfun_webserver/include/esp32libfun_webserver.hpp"
#endif

#define ESP32LIBFUN_VERSION "v0.1.2"
#define ESP32LIBFUN_VERSION_MAJOR 0
#define ESP32LIBFUN_VERSION_MINOR 1
#define ESP32LIBFUN_VERSION_PATCH 2
#define ESP32LIBFUN_IDF_BASELINE "v6.0.0"

/// Returns the framework major version as text.
///
/// @return Major version number as a static string.
const char *esp32libfun_major(void);
/// Returns the framework minor version as text.
///
/// @return Minor version number as a static string.
const char *esp32libfun_minor(void);
/// Returns the framework patch version as text.
///
/// @return Patch version number as a static string.
const char *esp32libfun_patch(void);
/// Returns the full framework version string.
///
/// @return Version string, e.g. `"v0.1.0"`.
const char *esp32libfun_version(void);

/// Initializes enabled framework convenience services.
///
/// Currently starts the serial console (when enabled) and the AT console
/// (when enabled). Other core modules are initialized on demand by calling
/// their own `begin()`/`init()`.
void esp32libfun_init(void);
