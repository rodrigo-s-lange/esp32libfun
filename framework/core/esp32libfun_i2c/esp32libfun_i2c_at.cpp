#include "esp32libfun_i2c.hpp"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifndef ESP32LIBFUN_HAS_AT
#define ESP32LIBFUN_HAS_AT 0
#endif

#if ESP32LIBFUN_HAS_AT
#include "../esp32libfun_at/include/esp32libfun_at.hpp"
#endif

namespace esp32libfun {

namespace {

bool s_i2c_at_enabled = false;

#if ESP32LIBFUN_HAS_AT

constexpr size_t kAtDataMax = 128;

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

    if ((base == 16 && !isxdigit(static_cast<unsigned char>(*text))) ||
        (base == 10 && !isdigit(static_cast<unsigned char>(*text)))) {
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

bool parseBool(const char *text, bool *value_out)
{
    if (text == nullptr || value_out == nullptr) {
        return false;
    }

    text = skipSeparators(text);
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

bool isHexDigit(char ch)
{
    return isxdigit(static_cast<unsigned char>(ch)) != 0;
}

unsigned hexDigitValue(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return static_cast<unsigned>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return static_cast<unsigned>(ch - 'a' + 10);
    }
    return static_cast<unsigned>(ch - 'A' + 10);
}

bool parseField(const char *text, char *out, size_t out_len, const char **next_out = nullptr, bool allow_empty = false)
{
    if (text == nullptr || out == nullptr || out_len == 0) {
        return false;
    }

    text = skipSeparators(text);
    if (!allow_empty && *text == '\0') {
        return false;
    }

    size_t index = 0;
    if (*text == '"') {
        ++text;
        while (*text != '\0' && *text != '"') {
            if (index + 1 >= out_len) {
                return false;
            }
            out[index++] = *text++;
        }
        if (*text != '"') {
            return false;
        }
        ++text;
    } else {
        const char *start = text;
        while (*text != '\0' && *text != ',') {
            ++text;
        }

        const char *end = text;
        while (end > start && isspace(static_cast<unsigned char>(end[-1]))) {
            --end;
        }

        const size_t len = static_cast<size_t>(end - start);
        if (len == 0 && !allow_empty) {
            return false;
        }
        if (len >= out_len) {
            return false;
        }

        memcpy(out, start, len);
        index = len;
    }

    out[index] = '\0';
    if (next_out != nullptr) {
        *next_out = text;
    }
    return true;
}

bool parseHexBytes(const char *text, uint8_t *out, size_t out_capacity, size_t *out_len, const char **next_out = nullptr)
{
    if (text == nullptr || out == nullptr || out_len == nullptr) {
        return false;
    }

    char field[(kAtDataMax * 2) + 1] = {};
    if (!parseField(text, field, sizeof(field), next_out)) {
        return false;
    }

    size_t nibble_count = 0;
    for (size_t i = 0; field[i] != '\0'; ++i) {
        const char ch = field[i];
        if (isspace(static_cast<unsigned char>(ch)) || ch == ':' || ch == '-') {
            continue;
        }
        if (!isHexDigit(ch)) {
            return false;
        }
        ++nibble_count;
    }

    if (nibble_count == 0 || (nibble_count & 1U) != 0) {
        return false;
    }

    const size_t byte_count = nibble_count / 2;
    if (byte_count > out_capacity) {
        return false;
    }

    bool high_nibble = true;
    uint8_t value = 0;
    size_t out_index = 0;
    for (size_t i = 0; field[i] != '\0'; ++i) {
        const char ch = field[i];
        if (isspace(static_cast<unsigned char>(ch)) || ch == ':' || ch == '-') {
            continue;
        }

        const uint8_t digit = static_cast<uint8_t>(hexDigitValue(ch));
        if (high_nibble) {
            value = static_cast<uint8_t>(digit << 4);
            high_nibble = false;
        } else {
            value = static_cast<uint8_t>(value | digit);
            out[out_index++] = value;
            high_nibble = true;
        }
    }

    *out_len = out_index;
    return true;
}

void writeHexResponse(const char *label, const uint8_t *data, size_t len)
{
    char line[(kAtDataMax * 2) + 32] = {};
    int written = snprintf(line, sizeof(line), "%s=", label);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(line)) {
        at.writeError("response too long");
        return;
    }

    size_t offset = static_cast<size_t>(written);
    for (size_t i = 0; i < len; ++i) {
        const int chunk = snprintf(line + offset, sizeof(line) - offset, "%02X", data[i]);
        if (chunk < 0 || static_cast<size_t>(chunk) >= (sizeof(line) - offset)) {
            at.writeError("response too long");
            return;
        }
        offset += static_cast<size_t>(chunk);
    }

    at.writeLine("%s", line);
}

bool parsePortOptional(const char *args, int *port_out)
{
    if (port_out == nullptr) {
        return false;
    }

    *port_out = 0;
    const char *text = skipSeparators(args);
    if (text == nullptr || *text == '\0') {
        return true;
    }

    return parseInt(text, port_out);
}

void writeI2cCommandError(const char *message)
{
    at.writeError("%s", message);
}

void writeI2cRuntimeError(const char *operation, esp_err_t err)
{
    at.writeError("%s: %s", operation, esp_err_to_name(err));
}

void atI2cBusSet(const char *args)
{
    int sda = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &sda, &cursor) || !consumeComma(&cursor)) {
        writeI2cCommandError("use <sda>,<scl>[,<speed>[,<port>[,<pullup>]]]");
        return;
    }

    int scl = -1;
    if (!parseInt(cursor, &scl, &cursor)) {
        writeI2cCommandError("use <sda>,<scl>[,<speed>[,<port>[,<pullup>]]]");
        return;
    }

    uint32_t speed_hz = I2C_STANDARD;
    if (consumeComma(&cursor)) {
        if (!parseUnsigned(cursor, &speed_hz, &cursor)) {
            writeI2cCommandError("invalid speed");
            return;
        }
    }

    int port = 0;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port, &cursor)) {
            writeI2cCommandError("invalid port");
            return;
        }
    }

    bool pullup = true;
    if (consumeComma(&cursor)) {
        if (!parseBool(cursor, &pullup)) {
            writeI2cCommandError("pullup must be 0/1");
            return;
        }
    }

    const esp_err_t err = i2c.begin(sda, scl, speed_hz, port, pullup);
    if (err != ESP_OK) {
        writeI2cRuntimeError("i2c.begin", err);
        return;
    }

    at.writeLine("I2CBUS%d=%d", port, i2c.ready(port) ? 1 : 0);
    at.writeLine("OK");
}

void atI2cBusGet(const char *args)
{
    int port = 0;
    if (!parsePortOptional(args, &port)) {
        writeI2cCommandError("use AT+I2CBUS?<port>");
        return;
    }

    at.writeLine("I2CBUS%d=%d", port, i2c.ready(port) ? 1 : 0);
    at.writeLine("OK");
}

void atI2cBusOff(const char *args)
{
    int port = 0;
    if (!parsePortOptional(args, &port)) {
        writeI2cCommandError("use <port>");
        return;
    }

    const esp_err_t err = i2c.end(port);
    if (err != ESP_OK) {
        writeI2cRuntimeError("i2c.end", err);
        return;
    }

    at.writeLine("I2CBUS%d=0", port);
    at.writeLine("OK");
}

void atI2cScan(const char *args)
{
    const char *text = skipSeparators(args);
    if (text == nullptr || *text == '\0' || strncmp(text, "SCAN", 4) != 0) {
        writeI2cCommandError("use AT+I2C=SCAN[,<port>]");
        return;
    }

    text += 4;
    int port = 0;
    if (*text != '\0') {
        if (*text != ',') {
            writeI2cCommandError("use AT+I2C=SCAN[,<port>]");
            return;
        }
        if (!parseInt(text + 1, &port)) {
            writeI2cCommandError("invalid port");
            return;
        }
    }

    if (!i2c.ready(port)) {
        writeI2cCommandError("bus not ready");
        return;
    }

    unsigned found = 0;
    for (uint16_t address = 0x08; address <= 0x77; ++address) {
        if (i2c.probe(address, port, 25) == ESP_OK) {
            at.writeLine("I2C=0x%02X", address);
            ++found;
        }
    }

    at.writeLine("I2CFOUND=%u", found);
    at.writeLine("OK");
}

void atI2cScanAlias(const char *args)
{
    int port = 0;
    if (!parsePortOptional(args, &port)) {
        writeI2cCommandError("use AT+I2CSCAN[=<port>]");
        return;
    }

    char scan_args[16] = {};
    if (port == 0) {
        strcpy(scan_args, "SCAN");
    } else {
        snprintf(scan_args, sizeof(scan_args), "SCAN,%d", port);
    }
    atI2cScan(scan_args);
}

void atI2cProbe(const char *args)
{
    uint32_t address = 0;
    const char *cursor = nullptr;
    if (!parseUnsigned(args, &address, &cursor)) {
        writeI2cCommandError("use <addr>[,<port>]");
        return;
    }

    int port = 0;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeI2cCommandError("invalid port");
            return;
        }
    }

    const esp_err_t err = i2c.probe(static_cast<uint16_t>(address), port, 25);
    if (err == ESP_OK) {
        at.writeLine("I2CPROBE=1 ADDR=0x%02X", static_cast<unsigned>(address));
        at.writeLine("OK");
        return;
    }

    if (err == ESP_ERR_INVALID_STATE || err == ESP_ERR_INVALID_ARG) {
        writeI2cRuntimeError("i2c.probe", err);
        return;
    }

    at.writeLine("I2CPROBE=0 ADDR=0x%02X", static_cast<unsigned>(address));
    at.writeLine("OK");
}

void atI2cAdd(const char *args)
{
    uint32_t address = 0;
    const char *cursor = nullptr;
    if (!parseUnsigned(args, &address, &cursor)) {
        writeI2cCommandError("use <addr>[,<port>[,<speed>[,<addr_bits>]]]");
        return;
    }

    int port = 0;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port, &cursor)) {
            writeI2cCommandError("invalid port");
            return;
        }
    }

    uint32_t speed_hz = 0;
    if (consumeComma(&cursor)) {
        if (!parseUnsigned(cursor, &speed_hz, &cursor)) {
            writeI2cCommandError("invalid speed");
            return;
        }
    }

    int addr_bits = I2C_ADDR_7BIT;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &addr_bits)) {
            writeI2cCommandError("invalid addr_bits");
            return;
        }
    }

    const esp_err_t err = i2c.add(static_cast<uint16_t>(address), port, speed_hz, addr_bits);
    if (err != ESP_OK) {
        writeI2cRuntimeError("i2c.add", err);
        return;
    }

    at.writeLine("I2CDEV=1 ADDR=0x%02X PORT=%d", static_cast<unsigned>(address), port);
    at.writeLine("OK");
}

void atI2cDevGet(const char *args)
{
    uint32_t address = 0;
    const char *cursor = nullptr;
    if (!parseUnsigned(args, &address, &cursor)) {
        writeI2cCommandError("use AT+I2CDEV?<addr>[,<port>]");
        return;
    }

    int port = 0;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeI2cCommandError("invalid port");
            return;
        }
    }

    at.writeLine("I2CDEV=ADDR=0x%02X PORT=%d READY=%d",
                 static_cast<unsigned>(address),
                 port,
                 i2c.has(static_cast<uint16_t>(address), port) ? 1 : 0);
    at.writeLine("OK");
}

void atI2cRemove(const char *args)
{
    uint32_t address = 0;
    const char *cursor = nullptr;
    if (!parseUnsigned(args, &address, &cursor)) {
        writeI2cCommandError("use <addr>[,<port>]");
        return;
    }

    int port = 0;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeI2cCommandError("invalid port");
            return;
        }
    }

    const esp_err_t err = i2c.remove(static_cast<uint16_t>(address), port);
    if (err != ESP_OK) {
        writeI2cRuntimeError("i2c.remove", err);
        return;
    }

    at.writeLine("I2CDEV=0 ADDR=0x%02X PORT=%d", static_cast<unsigned>(address), port);
    at.writeLine("OK");
}

void atI2cReset(const char *args)
{
    int port = 0;
    if (!parsePortOptional(args, &port)) {
        writeI2cCommandError("use <port>");
        return;
    }

    const esp_err_t err = i2c.reset(port);
    if (err != ESP_OK) {
        writeI2cRuntimeError("i2c.reset", err);
        return;
    }

    at.writeLine("I2CRESET=%d", port);
    at.writeLine("OK");
}

void atI2cWrite(const char *args)
{
    uint32_t address = 0;
    const char *cursor = nullptr;
    if (!parseUnsigned(args, &address, &cursor) || !consumeComma(&cursor)) {
        writeI2cCommandError("use <addr>,<hex>[,<port>]");
        return;
    }

    uint8_t tx_data[kAtDataMax] = {};
    size_t tx_len = 0;
    if (!parseHexBytes(cursor, tx_data, sizeof(tx_data), &tx_len, &cursor)) {
        writeI2cCommandError("invalid hex payload");
        return;
    }

    int port = 0;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeI2cCommandError("invalid port");
            return;
        }
    }

    const esp_err_t err = i2c.write(static_cast<uint16_t>(address), tx_data, tx_len, port);
    if (err != ESP_OK) {
        writeI2cRuntimeError("i2c.write", err);
        return;
    }

    at.writeLine("I2CWRITE=%u", static_cast<unsigned>(tx_len));
    at.writeLine("OK");
}

void atI2cRead(const char *args)
{
    uint32_t address = 0;
    const char *cursor = nullptr;
    if (!parseUnsigned(args, &address, &cursor) || !consumeComma(&cursor)) {
        writeI2cCommandError("use <addr>,<len>[,<port>]");
        return;
    }

    uint32_t read_len = 0;
    if (!parseUnsigned(cursor, &read_len, &cursor) || read_len == 0 || read_len > kAtDataMax) {
        writeI2cCommandError("len must be 1..128");
        return;
    }

    int port = 0;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeI2cCommandError("invalid port");
            return;
        }
    }

    uint8_t rx_data[kAtDataMax] = {};
    const esp_err_t err = i2c.read(static_cast<uint16_t>(address), rx_data, read_len, port);
    if (err != ESP_OK) {
        writeI2cRuntimeError("i2c.read", err);
        return;
    }

    writeHexResponse("I2CRX", rx_data, read_len);
    at.writeLine("OK");
}

void atI2cWriteRead(const char *args)
{
    uint32_t address = 0;
    const char *cursor = nullptr;
    if (!parseUnsigned(args, &address, &cursor) || !consumeComma(&cursor)) {
        writeI2cCommandError("use <addr>,<hex>,<len>[,<port>]");
        return;
    }

    uint8_t tx_data[kAtDataMax] = {};
    size_t tx_len = 0;
    if (!parseHexBytes(cursor, tx_data, sizeof(tx_data), &tx_len, &cursor) || !consumeComma(&cursor)) {
        writeI2cCommandError("invalid hex payload");
        return;
    }

    uint32_t read_len = 0;
    if (!parseUnsigned(cursor, &read_len, &cursor) || read_len == 0 || read_len > kAtDataMax) {
        writeI2cCommandError("len must be 1..128");
        return;
    }

    int port = 0;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeI2cCommandError("invalid port");
            return;
        }
    }

    uint8_t rx_data[kAtDataMax] = {};
    const esp_err_t err = i2c.writeRead(static_cast<uint16_t>(address), tx_data, tx_len, rx_data, read_len, port);
    if (err != ESP_OK) {
        writeI2cRuntimeError("i2c.writeRead", err);
        return;
    }

    writeHexResponse("I2CRX", rx_data, read_len);
    at.writeLine("OK");
}

void atI2cRegWrite(const char *args)
{
    uint32_t address = 0;
    const char *cursor = nullptr;
    if (!parseUnsigned(args, &address, &cursor) || !consumeComma(&cursor)) {
        writeI2cCommandError("use <addr>,<reg>,<hex>[,<port>]");
        return;
    }

    uint32_t reg = 0;
    if (!parseUnsigned(cursor, &reg, &cursor) || reg > 0xFFU || !consumeComma(&cursor)) {
        writeI2cCommandError("invalid reg");
        return;
    }

    uint8_t tx_data[kAtDataMax] = {};
    size_t tx_len = 0;
    if (!parseHexBytes(cursor, tx_data, sizeof(tx_data), &tx_len, &cursor)) {
        writeI2cCommandError("invalid hex payload");
        return;
    }

    int port = 0;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeI2cCommandError("invalid port");
            return;
        }
    }

    const esp_err_t err = i2c.regWrite(static_cast<uint16_t>(address), static_cast<uint8_t>(reg), tx_data, tx_len, port);
    if (err != ESP_OK) {
        writeI2cRuntimeError("i2c.regWrite", err);
        return;
    }

    at.writeLine("I2CREGWRITE=0x%02X,%u", static_cast<unsigned>(reg), static_cast<unsigned>(tx_len));
    at.writeLine("OK");
}

void atI2cRegRead(const char *args)
{
    uint32_t address = 0;
    const char *cursor = nullptr;
    if (!parseUnsigned(args, &address, &cursor) || !consumeComma(&cursor)) {
        writeI2cCommandError("use <addr>,<reg>,<len>[,<port>]");
        return;
    }

    uint32_t reg = 0;
    if (!parseUnsigned(cursor, &reg, &cursor) || reg > 0xFFU || !consumeComma(&cursor)) {
        writeI2cCommandError("invalid reg");
        return;
    }

    uint32_t read_len = 0;
    if (!parseUnsigned(cursor, &read_len, &cursor) || read_len == 0 || read_len > kAtDataMax) {
        writeI2cCommandError("len must be 1..128");
        return;
    }

    int port = 0;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeI2cCommandError("invalid port");
            return;
        }
    }

    uint8_t rx_data[kAtDataMax] = {};
    const esp_err_t err = i2c.regRead(static_cast<uint16_t>(address), static_cast<uint8_t>(reg), rx_data, read_len, port);
    if (err != ESP_OK) {
        writeI2cRuntimeError("i2c.regRead", err);
        return;
    }

    writeHexResponse("I2CREGRX", rx_data, read_len);
    at.writeLine("OK");
}

esp_err_t unregisterCommand(const char *command)
{
    const esp_err_t err = at.unregisterCmd(command);
    return (err == ESP_OK || err == ESP_ERR_NOT_FOUND) ? ESP_OK : err;
}

#endif

} // namespace

esp_err_t I2c::at(bool enable) const
{
#if !ESP32LIBFUN_HAS_AT
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!::at.isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        if (s_i2c_at_enabled) {
            return ESP_OK;
        }

        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CBUS", atI2cBusSet, "I2C bus init: <sda>,<scl>[,<speed>[,<port>[,<pullup>]]]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CBUS?", atI2cBusGet, "I2C bus state: AT+I2CBUS?<port>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CBUSOFF", atI2cBusOff, "I2C bus end: <port>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2C", atI2cScan, "I2C scan: AT+I2C=SCAN[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CSCAN", atI2cScanAlias, "I2C scan alias: AT+I2CSCAN[=<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CPROBE", atI2cProbe, "I2C probe: <addr>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CADD", atI2cAdd, "I2C add device: <addr>[,<port>[,<speed>[,<addr_bits>]]]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CDEV?", atI2cDevGet, "I2C device state: AT+I2CDEV?<addr>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CREMOVE", atI2cRemove, "I2C remove device: <addr>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CRESET", atI2cReset, "I2C reset bus: <port>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CWRITE", atI2cWrite, "I2C write bytes: <addr>,<hex>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CREAD", atI2cRead, "I2C read bytes: <addr>,<len>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CWRITEREAD", atI2cWriteRead, "I2C write-read: <addr>,<hex>,<len>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CREGWRITE", atI2cRegWrite, "I2C register write: <addr>,<reg>,<hex>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+I2CREGREAD", atI2cRegRead, "I2C register read: <addr>,<reg>,<len>[,<port>]"));
        s_i2c_at_enabled = true;
        return ESP_OK;
    }

    if (!s_i2c_at_enabled) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(unregisterCommand("AT+I2CBUS"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CBUS?"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CBUSOFF"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2C"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CSCAN"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CPROBE"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CADD"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CDEV?"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CREMOVE"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CRESET"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CWRITE"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CREAD"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CWRITEREAD"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CREGWRITE"));
    ESP_ERROR_CHECK(unregisterCommand("AT+I2CREGREAD"));
    s_i2c_at_enabled = false;
    return ESP_OK;
#endif
}

bool I2c::atEnabled(void) const
{
    return s_i2c_at_enabled;
}

} // namespace esp32libfun
