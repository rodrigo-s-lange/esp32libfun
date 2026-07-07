#include "esp32libfun_gpio.hpp"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifndef ESP32LIBFUN_HAS_AT
#define ESP32LIBFUN_HAS_AT 0
#endif

#if ESP32LIBFUN_HAS_AT
#include "../esp32libfun_at/include/esp32libfun_at.hpp"
#include "../esp32libfun_serial/include/esp32libfun_serial.hpp"
#endif

namespace esp32libfun {

namespace {

bool s_gpio_at_enabled = false;

#if ESP32LIBFUN_HAS_AT

const char *skipSeparators(const char *text)
{
    while (text != nullptr && (*text == '=' || *text == ' ' || *text == '\t')) {
        ++text;
    }
    return text;
}

bool parseInt(const char *text, int *value_out, const char **next_out = nullptr)
{
    if (text == nullptr || value_out == nullptr) {
        return false;
    }

    text = skipSeparators(text);
    if (*text == '\0') {
        return false;
    }

    bool negative = false;
    if (*text == '-') {
        negative = true;
        ++text;
    }

    if (!isdigit(static_cast<unsigned char>(*text))) {
        return false;
    }

    int value = 0;
    while (isdigit(static_cast<unsigned char>(*text))) {
        value = (value * 10) + (*text - '0');
        ++text;
    }

    *value_out = negative ? -value : value;
    if (next_out != nullptr) {
        *next_out = text;
    }
    return true;
}

bool parseLevelToken(const char *text, bool *level_out)
{
    if (text == nullptr || level_out == nullptr) {
        return false;
    }

    text = skipSeparators(text);
    if (strcmp(text, "1") == 0 || strcmp(text, "ON") == 0 || strcmp(text, "HIGH") == 0) {
        *level_out = true;
        return true;
    }

    if (strcmp(text, "0") == 0 || strcmp(text, "OFF") == 0 || strcmp(text, "LOW") == 0) {
        *level_out = false;
        return true;
    }

    return false;
}

bool parseModeToken(const char *text, int *mode_out)
{
    if (text == nullptr || mode_out == nullptr) {
        return false;
    }

    text = skipSeparators(text);

    if (strcmp(text, "INPUT") == 0) {
        *mode_out = INPUT;
        return true;
    }
    if (strcmp(text, "INPUT_PULLUP") == 0) {
        *mode_out = INPUT_PULLUP;
        return true;
    }
    if (strcmp(text, "INPUT_PULLDOWN") == 0) {
        *mode_out = INPUT_PULLDOWN;
        return true;
    }
    if (strcmp(text, "OUTPUT") == 0) {
        *mode_out = OUTPUT;
        return true;
    }
    if (strcmp(text, "INPUT_OUTPUT") == 0) {
        *mode_out = INPUT_OUTPUT;
        return true;
    }
    if (strcmp(text, "INPUT_OUTPUT_OPENDRAIN") == 0) {
        *mode_out = INPUT_OUTPUT_OPENDRAIN;
        return true;
    }
    if (strcmp(text, "OUTPUT_OPENDRAIN") == 0) {
        *mode_out = OUTPUT_OPENDRAIN;
        return true;
    }

    return parseInt(text, mode_out);
}

void atGpioWrite(const char *args)
{
    int pin = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &pin, &cursor) || cursor == nullptr || *cursor != ',') {
        at.writeError("use <pin>,<0|1>");
        return;
    }

    bool level = false;
    if (!parseLevelToken(cursor + 1, &level)) {
        at.writeError("use <pin>,<0|1>");
        return;
    }

    ESP_ERROR_CHECK(gpio.write(pin, level));
    at.writeLine(G "OK");
}

void atGpioRead(const char *args)
{
    int pin = -1;
    if (!parseInt(args, &pin)) {
        at.writeError("use AT+GPIO?<pin>");
        return;
    }

    at.writeLine("GPIO%d=%d", pin, gpio.state(pin) ? 1 : 0);
    at.writeLine(G "OK");
}

void atGpioToggle(const char *args)
{
    int pin = -1;
    if (!parseInt(args, &pin)) {
        at.writeError("use <pin>");
        return;
    }

    ESP_ERROR_CHECK(gpio.toggle(pin));
    at.writeLine("GPIO%d=%d", pin, gpio.state(pin) ? 1 : 0);
    at.writeLine(G "OK");
}

void atGpioCfg(const char *args)
{
    int pin = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &pin, &cursor) || cursor == nullptr || *cursor != ',') {
        at.writeError("use <pin>,<mode>");
        return;
    }

    int mode = 0;
    if (!parseModeToken(cursor + 1, &mode)) {
        at.writeError("use INPUT, INPUT_PULLUP, INPUT_PULLDOWN, OUTPUT, INPUT_OUTPUT, INPUT_OUTPUT_OPENDRAIN, OUTPUT_OPENDRAIN");
        return;
    }

    ESP_ERROR_CHECK(gpio.cfg(pin, mode));
    at.writeLine(G "OK");
}

esp_err_t unregisterCommand(const char *command)
{
    const esp_err_t err = at.unregisterCmd(command);
    return (err == ESP_OK || err == ESP_ERR_NOT_FOUND) ? ESP_OK : err;
}

#endif

} // namespace

esp_err_t Gpio::at(bool enable) const
{
#if !ESP32LIBFUN_HAS_AT
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!::at.isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        if (s_gpio_at_enabled) {
            return ESP_OK;
        }

        ESP_ERROR_CHECK(::at.registerCmd("AT+GPIO", atGpioWrite, "GPIO write: <pin>,<0|1>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+GPIO?", atGpioRead, "GPIO state: AT+GPIO?<pin>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+GPIOTOGGLE", atGpioToggle, "GPIO toggle: <pin>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+GPIOCFG", atGpioCfg, "GPIO config: <pin>,<mode>"));
        s_gpio_at_enabled = true;
        return ESP_OK;
    }

    if (!s_gpio_at_enabled) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(unregisterCommand("AT+GPIO"));
    ESP_ERROR_CHECK(unregisterCommand("AT+GPIO?"));
    ESP_ERROR_CHECK(unregisterCommand("AT+GPIOTOGGLE"));
    ESP_ERROR_CHECK(unregisterCommand("AT+GPIOCFG"));
    s_gpio_at_enabled = false;
    return ESP_OK;
#endif
}

bool Gpio::atEnabled(void) const
{
    return s_gpio_at_enabled;
}

} // namespace esp32libfun
