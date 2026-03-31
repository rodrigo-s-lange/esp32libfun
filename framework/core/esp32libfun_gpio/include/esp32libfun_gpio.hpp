#pragma once

#include "esp_err.h"

namespace esp32libfun {

constexpr int INPUT                  = 0;
constexpr int INPUT_PULLUP           = 1;
constexpr int INPUT_PULLDOWN         = 2;
constexpr int OUTPUT                 = 3;
constexpr int INPUT_OUTPUT           = 4;
constexpr int INPUT_OUTPUT_OPENDRAIN = 5;
constexpr int OUTPUT_OPENDRAIN       = 6;

class Gpio {
public:
    static constexpr bool HIGH = true;
    static constexpr bool LOW = false;

    esp_err_t cfg(int pin, int direction) const;
    esp_err_t write(int pin, bool level) const;
    esp_err_t high(int pin) const;
    esp_err_t low(int pin) const;
    // Reads the electrical level currently seen on the pin.
    // If the pin was configured as OUTPUT-only and you need the last logical value written by the library,
    // prefer state(pin) or configure the pin as INPUT_OUTPUT.
    [[nodiscard]] bool read(int pin) const;
    // Returns the last logical level written by the library when tracked for an output-capable pin.
    // Falls back to read(pin) when no shadow state is available.
    [[nodiscard]] bool state(int pin) const;
    esp_err_t toggle(int pin) const;

private:
    static bool isValidPin(int pin);
    static bool usesOutputShadow(int direction);
    static esp_err_t applyConfig(int pin, int direction);
};

extern Gpio gpio;

} // namespace esp32libfun

using esp32libfun::gpio;
using esp32libfun::INPUT;
using esp32libfun::INPUT_PULLUP;
using esp32libfun::INPUT_PULLDOWN;
using esp32libfun::OUTPUT;
using esp32libfun::INPUT_OUTPUT;
using esp32libfun::INPUT_OUTPUT_OPENDRAIN;
using esp32libfun::OUTPUT_OPENDRAIN;
