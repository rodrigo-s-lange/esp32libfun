#pragma once

#include "sdkconfig.h"

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

#if CONFIG_ESP32LIBFUN_MCPWM
#include "../../esp32libfun_mcpwm/include/esp32libfun_mcpwm.hpp"
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

// Bring public API symbols into the global scope so users of this header
// do not need to qualify with esp32libfun:: or use 'using namespace'.
#if CONFIG_ESP32LIBFUN_SERIAL
using esp32libfun::serial;
#endif
#if CONFIG_ESP32LIBFUN_AT
using esp32libfun::at;
#endif
#if CONFIG_ESP32LIBFUN_DELAY
using esp32libfun::delay;
#endif
#if CONFIG_ESP32LIBFUN_GPIO
using esp32libfun::gpio;
#endif
#if CONFIG_ESP32LIBFUN_LEDC
using esp32libfun::ledc;
#endif
#if CONFIG_ESP32LIBFUN_I2C
using esp32libfun::i2c;
#endif
#if CONFIG_ESP32LIBFUN_PCNT
using esp32libfun::pcnt;
#endif
#if CONFIG_ESP32LIBFUN_SPI
using esp32libfun::spi;
#endif
#if CONFIG_ESP32LIBFUN_MCPWM
using esp32libfun::mcpwm;
#endif
