#include "esp32libfun_serial.hpp"

#include <string.h>

#ifndef ESP32LIBFUN_HAS_AT
#define ESP32LIBFUN_HAS_AT 0
#endif

#if ESP32LIBFUN_HAS_AT
#include "../esp32libfun_at/include/esp32libfun_at.hpp"
#endif

namespace {

bool s_serial_at_enabled = false;

#if ESP32LIBFUN_HAS_AT

const char *skipSerialAtSeparators(const char *text)
{
    while (text != nullptr && (*text == '=' || *text == ' ' || *text == '\t')) {
        ++text;
    }
    return text;
}

bool parseSerialAtBool(const char *text, bool *value_out)
{
    if (text == nullptr || value_out == nullptr) {
        return false;
    }

    text = skipSerialAtSeparators(text);
    if (strcmp(text, "1") == 0 || strcmp(text, "ON") == 0 || strcmp(text, "TRUE") == 0) {
        *value_out = true;
        return true;
    }

    if (strcmp(text, "0") == 0 || strcmp(text, "OFF") == 0 || strcmp(text, "FALSE") == 0) {
        *value_out = false;
        return true;
    }

    return false;
}

void atSerialPrint(const char *args)
{
    const char *text = skipSerialAtSeparators(args);
    serial.print("%s", text);
    serial.print("\r\n");
    at.writeLine(G "OK");
}

void atSerialPrintln(const char *args)
{
    const char *text = skipSerialAtSeparators(args);
    serial.println("%s", text);
    at.writeLine(G "OK");
}

void atSerialEchoSet(const char *args)
{
    bool enabled = false;
    if (!parseSerialAtBool(args, &enabled)) {
        at.writeError("use 0/1 or OFF/ON");
        return;
    }

    serial.setEcho(enabled);
    at.writeLine("SERIALECHO=%d", serial.echoEnabled() ? 1 : 0);
    at.writeLine(G "OK");
}

void atSerialEchoGet(const char *args)
{
    (void)args;
    at.writeLine("SERIALECHO=%d", serial.echoEnabled() ? 1 : 0);
    at.writeLine(G "OK");
}

void atSerialBackendGet(const char *args)
{
    (void)args;
    at.writeLine("SERIALBACKEND=%s", serial.backend());
    at.writeLine(G "OK");
}

esp_err_t unregisterSerialAtCommand(const char *command)
{
    const esp_err_t err = at.unregisterCmd(command);
    return (err == ESP_OK || err == ESP_ERR_NOT_FOUND) ? ESP_OK : err;
}

#endif

} // namespace

namespace esp32libfun {

esp_err_t Serial::at(bool enable)
{
#if !ESP32LIBFUN_HAS_AT
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!::at.isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        if (s_serial_at_enabled) {
            return ESP_OK;
        }

        ESP_ERROR_CHECK(::at.registerCmd("AT+SERIALPRINT", atSerialPrint, "Print one line through serial"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SERIALPRINTLN", atSerialPrintln, "Print one line with println"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SERIALECHO", atSerialEchoSet, "Serial line echo: 0|1"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SERIALECHO?", atSerialEchoGet, "Read serial echo state"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SERIALBACKEND?", atSerialBackendGet, "Read serial backend name"));
        s_serial_at_enabled = true;
        return ESP_OK;
    }

    if (!s_serial_at_enabled) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(unregisterSerialAtCommand("AT+SERIALPRINT"));
    ESP_ERROR_CHECK(unregisterSerialAtCommand("AT+SERIALPRINTLN"));
    ESP_ERROR_CHECK(unregisterSerialAtCommand("AT+SERIALECHO"));
    ESP_ERROR_CHECK(unregisterSerialAtCommand("AT+SERIALECHO?"));
    ESP_ERROR_CHECK(unregisterSerialAtCommand("AT+SERIALBACKEND?"));
    s_serial_at_enabled = false;
    return ESP_OK;
#endif
}

bool Serial::atEnabled(void) const
{
    return s_serial_at_enabled;
}

} // namespace esp32libfun
