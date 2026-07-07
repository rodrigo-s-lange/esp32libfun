#include "esp32libfun_ledc.hpp"

#include <ctype.h>
#include <stdlib.h>
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

bool s_ledc_at_enabled = false;

#if ESP32LIBFUN_HAS_AT

const char *skipSeparators(const char *text)
{
    while (text != nullptr && (*text == '=' || *text == ' ' || *text == '\t')) {
        ++text;
    }
    return text;
}

bool parseUnsigned(const char *text, uint32_t *value_out, const char **next_out = nullptr)
{
    if (text == nullptr || value_out == nullptr) {
        return false;
    }

    text = skipSeparators(text);
    if (*text == '\0') {
        return false;
    }

    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }

    if ((base == 10 && !isdigit(static_cast<unsigned char>(*text))) ||
        (base == 16 && !isxdigit(static_cast<unsigned char>(*text)))) {
        return false;
    }

    uint32_t value = 0;
    while (*text != '\0') {
        unsigned digit = 0;
        if (*text >= '0' && *text <= '9') {
            digit = static_cast<unsigned>(*text - '0');
        } else if (base == 16 && *text >= 'a' && *text <= 'f') {
            digit = static_cast<unsigned>(*text - 'a' + 10);
        } else if (base == 16 && *text >= 'A' && *text <= 'F') {
            digit = static_cast<unsigned>(*text - 'A' + 10);
        } else {
            break;
        }

        value = (value * static_cast<uint32_t>(base)) + digit;
        ++text;
    }

    *value_out = value;
    if (next_out != nullptr) {
        *next_out = text;
    }
    return true;
}

bool parseInt(const char *text, int *value_out, const char **next_out = nullptr)
{
    if (text == nullptr || value_out == nullptr) {
        return false;
    }

    text = skipSeparators(text);
    bool negative = false;
    if (*text == '-') {
        negative = true;
        ++text;
    }

    uint32_t value = 0;
    const char *next = nullptr;
    if (!parseUnsigned(text, &value, &next)) {
        return false;
    }

    *value_out = negative ? -static_cast<int>(value) : static_cast<int>(value);
    if (next_out != nullptr) {
        *next_out = next;
    }
    return true;
}

bool parseFloat(const char *text, float *value_out, const char **next_out = nullptr)
{
    if (text == nullptr || value_out == nullptr) {
        return false;
    }

    text = skipSeparators(text);
    if (*text == '\0') {
        return false;
    }

    char *end = nullptr;
    const float value = strtof(text, &end);
    if (end == text) {
        return false;
    }

    *value_out = value;
    if (next_out != nullptr) {
        *next_out = end;
    }
    return true;
}

bool consumeComma(const char **cursor)
{
    if (cursor == nullptr || *cursor == nullptr) {
        return false;
    }

    const char *text = skipSeparators(*cursor);
    if (*text != ',') {
        return false;
    }

    *cursor = text + 1;
    return true;
}

bool parseBoolToken(const char *text, bool *value_out, const char **next_out = nullptr)
{
    if (text == nullptr || value_out == nullptr) {
        return false;
    }

    text = skipSeparators(text);
    const char *cursor = text;
    while (*cursor != '\0' && *cursor != ',' && !isspace(static_cast<unsigned char>(*cursor))) {
        ++cursor;
    }

    const size_t len = static_cast<size_t>(cursor - text);
    if ((len == 1 && text[0] == '1') ||
        (len == 2 && strncmp(text, "ON", 2) == 0) ||
        (len == 4 && strncmp(text, "TRUE", 4) == 0)) {
        *value_out = true;
    } else if ((len == 1 && text[0] == '0') ||
               (len == 3 && strncmp(text, "OFF", 3) == 0) ||
               (len == 5 && strncmp(text, "FALSE", 5) == 0)) {
        *value_out = false;
    } else {
        return false;
    }

    if (next_out != nullptr) {
        *next_out = cursor;
    }
    return true;
}

void writeEspError(const char *operation, esp_err_t err)
{
    at.writeError("%s failed: %s", operation, esp_err_to_name(err));
}

uint32_t percentToDuty(int pin, float percent)
{
    if (percent < 0.0f) {
        percent = 0.0f;
    }
    if (percent > 100.0f) {
        percent = 100.0f;
    }

    const uint32_t max_duty = ledc.maxDuty(pin);
    return static_cast<uint32_t>((percent * static_cast<float>(max_duty)) / 100.0f + 0.5f);
}

void atLedcPercent(const char *args)
{
    int pin = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &pin, &cursor) || !consumeComma(&cursor)) {
        at.writeError("use <pin>,<percent>");
        return;
    }

    float percent = 0.0f;
    if (!parseFloat(cursor, &percent)) {
        at.writeError("use <pin>,<percent>");
        return;
    }

    const esp_err_t err = ledc.percent(pin, percent);
    if (err != ESP_OK) {
        writeEspError("ledc.percent", err);
        return;
    }

    at.writeLine("LEDC%d=%.2f", pin, static_cast<double>(percent));
    at.writeLine(G "OK");
}

void atLedcGet(const char *args)
{
    int pin = -1;
    if (!parseInt(args, &pin)) {
        at.writeError("use AT+LEDC?<pin>");
        return;
    }

    if (!ledc.ready(pin)) {
        at.writeLine("LEDC%d=OFF", pin);
        at.writeLine(G "OK");
        return;
    }

    const uint32_t duty = ledc.duty(pin);
    const uint32_t max_duty = ledc.maxDuty(pin);
    const double percent = (max_duty > 0) ? ((static_cast<double>(duty) * 100.0) / static_cast<double>(max_duty)) : 0.0;

    at.writeLine("LEDC%d=ON,FREQ=%lu,RES=%u,DUTY=%lu,MAX=%lu,PERCENT=%.2f",
                 pin,
                 static_cast<unsigned long>(ledc.freq(pin)),
                 static_cast<unsigned>(ledc.resolution(pin)),
                 static_cast<unsigned long>(duty),
                 static_cast<unsigned long>(max_duty),
                 percent);
    at.writeLine(G "OK");
}

void atLedcCfg(const char *args)
{
    int pin = -1;
    uint32_t freq_hz = LEDC_PWM;
    uint32_t resolution_bits = Ledc::DEFAULT_RESOLUTION_BITS;
    int channel = -1;
    bool invert = false;

    const char *cursor = nullptr;
    if (!parseInt(args, &pin, &cursor) || !consumeComma(&cursor) || !parseUnsigned(cursor, &freq_hz, &cursor)) {
        at.writeError("use <pin>,<freq>[,<resolution>[,<channel>[,<invert>]]]");
        return;
    }

    if (consumeComma(&cursor)) {
        if (!parseUnsigned(cursor, &resolution_bits, &cursor)) {
            at.writeError("use <pin>,<freq>[,<resolution>[,<channel>[,<invert>]]]");
            return;
        }

        if (consumeComma(&cursor)) {
            if (!parseInt(cursor, &channel, &cursor)) {
                at.writeError("use <pin>,<freq>[,<resolution>[,<channel>[,<invert>]]]");
                return;
            }

            if (consumeComma(&cursor) && !parseBoolToken(cursor, &invert)) {
                at.writeError("use invert 0/1 or OFF/ON");
                return;
            }
        }
    }

    const esp_err_t err = ledc.begin(pin, freq_hz, static_cast<uint8_t>(resolution_bits), channel, invert);
    if (err != ESP_OK) {
        writeEspError("ledc.begin", err);
        return;
    }

    at.writeLine("LEDC%d=ON,FREQ=%lu,RES=%lu", pin, static_cast<unsigned long>(freq_hz), static_cast<unsigned long>(resolution_bits));
    at.writeLine(G "OK");
}

void atLedcDuty(const char *args)
{
    int pin = -1;
    uint32_t duty = 0;

    const char *cursor = nullptr;
    if (!parseInt(args, &pin, &cursor) || !consumeComma(&cursor) || !parseUnsigned(cursor, &duty)) {
        at.writeError("use <pin>,<raw_duty>");
        return;
    }

    const esp_err_t err = ledc.duty(pin, duty);
    if (err != ESP_OK) {
        writeEspError("ledc.duty", err);
        return;
    }

    at.writeLine("LEDC%d:DUTY=%lu", pin, static_cast<unsigned long>(duty));
    at.writeLine(G "OK");
}

void atLedcFreq(const char *args)
{
    int pin = -1;
    uint32_t freq_hz = 0;

    const char *cursor = nullptr;
    if (!parseInt(args, &pin, &cursor) || !consumeComma(&cursor) || !parseUnsigned(cursor, &freq_hz)) {
        at.writeError("use <pin>,<freq>");
        return;
    }

    const esp_err_t err = ledc.freq(pin, freq_hz);
    if (err != ESP_OK) {
        writeEspError("ledc.freq", err);
        return;
    }

    at.writeLine("LEDC%d:FREQ=%lu", pin, static_cast<unsigned long>(freq_hz));
    at.writeLine(G "OK");
}

void atLedcFade(const char *args)
{
    int pin = -1;
    float percent = 0.0f;
    uint32_t time_ms = 0;
    bool wait_done = false;

    const char *cursor = nullptr;
    if (!parseInt(args, &pin, &cursor) ||
        !consumeComma(&cursor) ||
        !parseFloat(cursor, &percent, &cursor) ||
        !consumeComma(&cursor) ||
        !parseUnsigned(cursor, &time_ms, &cursor)) {
        at.writeError("use <pin>,<percent>,<ms>[,<wait>]");
        return;
    }

    if (consumeComma(&cursor) && !parseBoolToken(cursor, &wait_done)) {
        at.writeError("use wait 0/1 or OFF/ON");
        return;
    }

    if (!ledc.ready(pin)) {
        at.writeError("LEDC pin is not configured");
        return;
    }

    const uint32_t target_duty = percentToDuty(pin, percent);
    const esp_err_t err = ledc.fade(pin, target_duty, time_ms, wait_done);
    if (err != ESP_OK) {
        writeEspError("ledc.fade", err);
        return;
    }

    at.writeLine("LEDC%d:FADE=%.2f,%lu", pin, static_cast<double>(percent), static_cast<unsigned long>(time_ms));
    at.writeLine(G "OK");
}

void atLedcOff(const char *args)
{
    int pin = -1;
    if (!parseInt(args, &pin)) {
        at.writeError("use <pin>");
        return;
    }

    const esp_err_t err = ledc.end(pin);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        writeEspError("ledc.end", err);
        return;
    }

    at.writeLine("LEDC%d=OFF", pin);
    at.writeLine(G "OK");
}

esp_err_t unregisterCommand(const char *command)
{
    const esp_err_t err = at.unregisterCmd(command);
    return (err == ESP_OK || err == ESP_ERR_NOT_FOUND) ? ESP_OK : err;
}

#endif

} // namespace

esp_err_t Ledc::at(bool enable) const
{
#if !ESP32LIBFUN_HAS_AT
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!::at.isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        if (s_ledc_at_enabled) {
            return ESP_OK;
        }

        ESP_ERROR_CHECK(::at.registerCmd("AT+LEDC", atLedcPercent, "LEDC duty percent: <pin>,<percent>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+LEDC?", atLedcGet, "LEDC state: AT+LEDC?<pin>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+LEDCCFG", atLedcCfg, "LEDC config: <pin>,<freq>[,<res>[,<channel>[,<invert>]]]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+LEDCDUTY", atLedcDuty, "LEDC raw duty: <pin>,<raw_duty>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+LEDCFREQ", atLedcFreq, "LEDC frequency: <pin>,<freq>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+LEDCFADE", atLedcFade, "LEDC fade: <pin>,<percent>,<ms>[,<wait>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+LEDCOFF", atLedcOff, "LEDC off: <pin>"));
        s_ledc_at_enabled = true;
        return ESP_OK;
    }

    if (!s_ledc_at_enabled) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(unregisterCommand("AT+LEDC"));
    ESP_ERROR_CHECK(unregisterCommand("AT+LEDC?"));
    ESP_ERROR_CHECK(unregisterCommand("AT+LEDCCFG"));
    ESP_ERROR_CHECK(unregisterCommand("AT+LEDCDUTY"));
    ESP_ERROR_CHECK(unregisterCommand("AT+LEDCFREQ"));
    ESP_ERROR_CHECK(unregisterCommand("AT+LEDCFADE"));
    ESP_ERROR_CHECK(unregisterCommand("AT+LEDCOFF"));
    s_ledc_at_enabled = false;
    return ESP_OK;
#endif
}

bool Ledc::atEnabled(void) const
{
    return s_ledc_at_enabled;
}

} // namespace esp32libfun
