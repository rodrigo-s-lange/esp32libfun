#include "esp32libfun_spi.hpp"

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

bool s_spi_at_enabled = false;

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

bool parseField(const char *text, char *out, size_t out_len, const char **next_out = nullptr)
{
    if (text == nullptr || out == nullptr || out_len == 0) {
        return false;
    }

    text = skipSeparators(text);
    if (*text == '\0') {
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
        if (len == 0 || len >= out_len) {
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

    *port_out = SPI_HOST_DEFAULT;
    const char *text = skipSeparators(args);
    if (text == nullptr || *text == '\0') {
        return true;
    }

    return parseInt(text, port_out);
}

void writeSpiCommandError(const char *message)
{
    at.writeError("%s", message);
}

void writeSpiRuntimeError(const char *operation, esp_err_t err)
{
    at.writeError("%s: %s", operation, esp_err_to_name(err));
}

void atSpiBusSet(const char *args)
{
    int sclk = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &sclk, &cursor) || !consumeComma(&cursor)) {
        writeSpiCommandError("use <sclk>,<mosi>[,<miso>[,<port>[,<max_transfer>]]]");
        return;
    }

    int mosi = -1;
    if (!parseInt(cursor, &mosi, &cursor)) {
        writeSpiCommandError("use <sclk>,<mosi>[,<miso>[,<port>[,<max_transfer>]]]");
        return;
    }

    int miso = -1;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &miso, &cursor)) {
            writeSpiCommandError("invalid miso");
            return;
        }
    }

    int port = SPI_HOST_DEFAULT;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port, &cursor)) {
            writeSpiCommandError("invalid port");
            return;
        }
    }

    uint32_t max_transfer = static_cast<uint32_t>(Spi::DEFAULT_MAX_TRANSFER);
    if (consumeComma(&cursor)) {
        if (!parseUnsigned(cursor, &max_transfer)) {
            writeSpiCommandError("invalid max_transfer");
            return;
        }
    }

    const esp_err_t err = spi.begin(sclk, mosi, miso, port, max_transfer);
    if (err != ESP_OK) {
        writeSpiRuntimeError("spi.begin", err);
        return;
    }

    at.writeLine("SPIBUS%d=%d", port, spi.ready(port) ? 1 : 0);
    at.writeLine("OK");
}

void atSpiBusGet(const char *args)
{
    int port = SPI_HOST_DEFAULT;
    if (!parsePortOptional(args, &port)) {
        writeSpiCommandError("use AT+SPIBUS?<port>");
        return;
    }

    at.writeLine("SPIBUS%d=%d", port, spi.ready(port) ? 1 : 0);
    at.writeLine("OK");
}

void atSpiBusOff(const char *args)
{
    int port = SPI_HOST_DEFAULT;
    if (!parsePortOptional(args, &port)) {
        writeSpiCommandError("use <port>");
        return;
    }

    const esp_err_t err = spi.end(port);
    if (err != ESP_OK) {
        writeSpiRuntimeError("spi.end", err);
        return;
    }

    at.writeLine("SPIBUS%d=0", port);
    at.writeLine("OK");
}

void atSpiAdd(const char *args)
{
    int cs = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &cs, &cursor) || !consumeComma(&cursor)) {
        writeSpiCommandError("use <cs>,<clock>[,<mode>[,<port>[,<queue_size>]]]");
        return;
    }

    uint32_t clock_hz = 0;
    if (!parseUnsigned(cursor, &clock_hz, &cursor)) {
        writeSpiCommandError("invalid clock");
        return;
    }

    int mode = SPI_MODE_0;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &mode, &cursor)) {
            writeSpiCommandError("invalid mode");
            return;
        }
    }

    int port = SPI_HOST_DEFAULT;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port, &cursor)) {
            writeSpiCommandError("invalid port");
            return;
        }
    }

    uint32_t queue_size = 1;
    if (consumeComma(&cursor)) {
        if (!parseUnsigned(cursor, &queue_size)) {
            writeSpiCommandError("invalid queue_size");
            return;
        }
    }

    const esp_err_t err = spi.add(cs, clock_hz, mode, port, queue_size);
    if (err != ESP_OK) {
        writeSpiRuntimeError("spi.add", err);
        return;
    }

    at.writeLine("SPIDEV=1 CS=%d PORT=%d", cs, port);
    at.writeLine("OK");
}

void atSpiDevGet(const char *args)
{
    int cs = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &cs, &cursor)) {
        writeSpiCommandError("use AT+SPIDEV?<cs>[,<port>]");
        return;
    }

    int port = SPI_HOST_DEFAULT;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeSpiCommandError("invalid port");
            return;
        }
    }

    at.writeLine("SPIDEV=CS=%d PORT=%d READY=%d", cs, port, spi.has(cs, port) ? 1 : 0);
    at.writeLine("OK");
}

void atSpiRemove(const char *args)
{
    int cs = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &cs, &cursor)) {
        writeSpiCommandError("use <cs>[,<port>]");
        return;
    }

    int port = SPI_HOST_DEFAULT;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeSpiCommandError("invalid port");
            return;
        }
    }

    const esp_err_t err = spi.remove(cs, port);
    if (err != ESP_OK) {
        writeSpiRuntimeError("spi.remove", err);
        return;
    }

    at.writeLine("SPIDEV=0 CS=%d PORT=%d", cs, port);
    at.writeLine("OK");
}

void atSpiCmd(const char *args)
{
    int cs = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &cs, &cursor) || !consumeComma(&cursor)) {
        writeSpiCommandError("use <cs>,<value>[,<port>]");
        return;
    }

    uint32_t value = 0;
    if (!parseUnsigned(cursor, &value, &cursor) || value > 0xFFU) {
        writeSpiCommandError("invalid value");
        return;
    }

    int port = SPI_HOST_DEFAULT;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeSpiCommandError("invalid port");
            return;
        }
    }

    const esp_err_t err = spi.cmd(cs, static_cast<uint8_t>(value), port);
    if (err != ESP_OK) {
        writeSpiRuntimeError("spi.cmd", err);
        return;
    }

    at.writeLine("SPICMD=0x%02X", static_cast<unsigned>(value));
    at.writeLine("OK");
}

void atSpiWrite(const char *args)
{
    int cs = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &cs, &cursor) || !consumeComma(&cursor)) {
        writeSpiCommandError("use <cs>,<hex>[,<port>]");
        return;
    }

    uint8_t tx_data[kAtDataMax] = {};
    size_t tx_len = 0;
    if (!parseHexBytes(cursor, tx_data, sizeof(tx_data), &tx_len, &cursor)) {
        writeSpiCommandError("invalid hex payload");
        return;
    }

    int port = SPI_HOST_DEFAULT;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeSpiCommandError("invalid port");
            return;
        }
    }

    const esp_err_t err = spi.write(cs, tx_data, tx_len, port);
    if (err != ESP_OK) {
        writeSpiRuntimeError("spi.write", err);
        return;
    }

    at.writeLine("SPIWRITE=%u", static_cast<unsigned>(tx_len));
    at.writeLine("OK");
}

void atSpiRead(const char *args)
{
    int cs = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &cs, &cursor) || !consumeComma(&cursor)) {
        writeSpiCommandError("use <cs>,<len>[,<port>]");
        return;
    }

    uint32_t read_len = 0;
    if (!parseUnsigned(cursor, &read_len, &cursor) || read_len == 0 || read_len > kAtDataMax) {
        writeSpiCommandError("len must be 1..128");
        return;
    }

    int port = SPI_HOST_DEFAULT;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeSpiCommandError("invalid port");
            return;
        }
    }

    uint8_t rx_data[kAtDataMax] = {};
    const esp_err_t err = spi.read(cs, rx_data, read_len, port);
    if (err != ESP_OK) {
        writeSpiRuntimeError("spi.read", err);
        return;
    }

    writeHexResponse("SPIRX", rx_data, read_len);
    at.writeLine("OK");
}

void atSpiTxRx(const char *args)
{
    int cs = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &cs, &cursor) || !consumeComma(&cursor)) {
        writeSpiCommandError("use <cs>,<hex>[,<port>]");
        return;
    }

    uint8_t tx_data[kAtDataMax] = {};
    size_t tx_len = 0;
    if (!parseHexBytes(cursor, tx_data, sizeof(tx_data), &tx_len, &cursor)) {
        writeSpiCommandError("invalid hex payload");
        return;
    }

    int port = SPI_HOST_DEFAULT;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeSpiCommandError("invalid port");
            return;
        }
    }

    uint8_t rx_data[kAtDataMax] = {};
    const esp_err_t err = spi.transfer(cs, tx_data, rx_data, tx_len, port);
    if (err != ESP_OK) {
        writeSpiRuntimeError("spi.transfer", err);
        return;
    }

    writeHexResponse("SPIRX", rx_data, tx_len);
    at.writeLine("OK");
}

void atSpiRegWrite(const char *args)
{
    int cs = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &cs, &cursor) || !consumeComma(&cursor)) {
        writeSpiCommandError("use <cs>,<reg>,<value>[,<port>]");
        return;
    }

    uint32_t reg = 0;
    if (!parseUnsigned(cursor, &reg, &cursor) || reg > 0xFFU || !consumeComma(&cursor)) {
        writeSpiCommandError("invalid reg");
        return;
    }

    uint8_t tx_data[kAtDataMax] = {};
    size_t tx_len = 0;
    if (!parseHexBytes(cursor, tx_data, sizeof(tx_data), &tx_len, &cursor)) {
        uint32_t value = 0;
        if (!parseUnsigned(cursor, &value, &cursor) || value > 0xFFU) {
            writeSpiCommandError("invalid value");
            return;
        }
        tx_data[0] = static_cast<uint8_t>(value);
        tx_len = 1;
    }

    int port = SPI_HOST_DEFAULT;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeSpiCommandError("invalid port");
            return;
        }
    }

    esp_err_t err = ESP_OK;
    if (tx_len == 1) {
        err = spi.regWrite8(cs, static_cast<uint8_t>(reg), tx_data[0], port);
    } else {
        err = spi.regWrite(cs, static_cast<uint8_t>(reg), tx_data, tx_len, port);
    }
    if (err != ESP_OK) {
        writeSpiRuntimeError("spi.regWrite", err);
        return;
    }

    at.writeLine("SPIREGWRITE=0x%02X,%u", static_cast<unsigned>(reg), static_cast<unsigned>(tx_len));
    at.writeLine("OK");
}

void atSpiRegRead(const char *args)
{
    int cs = -1;
    const char *cursor = nullptr;
    if (!parseInt(args, &cs, &cursor) || !consumeComma(&cursor)) {
        writeSpiCommandError("use <cs>,<reg>[,<port>]");
        return;
    }

    uint32_t reg = 0;
    if (!parseUnsigned(cursor, &reg, &cursor) || reg > 0xFFU) {
        writeSpiCommandError("invalid reg");
        return;
    }

    uint32_t read_len = 1;
    if (consumeComma(&cursor)) {
        if (!parseUnsigned(cursor, &read_len, &cursor) || read_len == 0 || read_len > kAtDataMax) {
            writeSpiCommandError("len must be 1..128");
            return;
        }
    }

    int port = SPI_HOST_DEFAULT;
    if (consumeComma(&cursor)) {
        if (!parseInt(cursor, &port)) {
            writeSpiCommandError("invalid port");
            return;
        }
    }

    uint8_t rx_data[kAtDataMax] = {};
    const esp_err_t err = spi.regRead(cs, static_cast<uint8_t>(reg), rx_data, read_len, port);
    if (err != ESP_OK) {
        writeSpiRuntimeError("spi.regRead", err);
        return;
    }

    writeHexResponse("SPIREGRX", rx_data, read_len);
    at.writeLine("OK");
}

esp_err_t unregisterCommand(const char *command)
{
    const esp_err_t err = at.unregisterCmd(command);
    return (err == ESP_OK || err == ESP_ERR_NOT_FOUND) ? ESP_OK : err;
}

#endif

} // namespace

esp_err_t Spi::at(bool enable) const
{
#if !ESP32LIBFUN_HAS_AT
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!::at.isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        if (s_spi_at_enabled) {
            return ESP_OK;
        }

        ESP_ERROR_CHECK(::at.registerCmd("AT+SPIBUS", atSpiBusSet, "SPI bus init: <sclk>,<mosi>[,<miso>[,<port>[,<max_transfer>]]]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SPIBUS?", atSpiBusGet, "SPI bus state: AT+SPIBUS?<port>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SPIBUSOFF", atSpiBusOff, "SPI bus end: <port>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SPIADD", atSpiAdd, "SPI add device: <cs>,<clock>[,<mode>[,<port>[,<queue_size>]]]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SPIDEV?", atSpiDevGet, "SPI device state: AT+SPIDEV?<cs>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SPIREMOVE", atSpiRemove, "SPI remove device: <cs>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SPICMD", atSpiCmd, "SPI command byte: <cs>,<value>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SPIWRITE", atSpiWrite, "SPI write bytes: <cs>,<hex>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SPIREAD", atSpiRead, "SPI read bytes: <cs>,<len>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SPITXRX", atSpiTxRx, "SPI full-duplex transfer: <cs>,<hex>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SPIREGWRITE", atSpiRegWrite, "SPI register write: <cs>,<reg>,<hex>[,<port>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+SPIREGREAD", atSpiRegRead, "SPI register read: <cs>,<reg>[,<len>[,<port>]]"));
        s_spi_at_enabled = true;
        return ESP_OK;
    }

    if (!s_spi_at_enabled) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(unregisterCommand("AT+SPIBUS"));
    ESP_ERROR_CHECK(unregisterCommand("AT+SPIBUS?"));
    ESP_ERROR_CHECK(unregisterCommand("AT+SPIBUSOFF"));
    ESP_ERROR_CHECK(unregisterCommand("AT+SPIADD"));
    ESP_ERROR_CHECK(unregisterCommand("AT+SPIDEV?"));
    ESP_ERROR_CHECK(unregisterCommand("AT+SPIREMOVE"));
    ESP_ERROR_CHECK(unregisterCommand("AT+SPICMD"));
    ESP_ERROR_CHECK(unregisterCommand("AT+SPIWRITE"));
    ESP_ERROR_CHECK(unregisterCommand("AT+SPIREAD"));
    ESP_ERROR_CHECK(unregisterCommand("AT+SPITXRX"));
    ESP_ERROR_CHECK(unregisterCommand("AT+SPIREGWRITE"));
    ESP_ERROR_CHECK(unregisterCommand("AT+SPIREGREAD"));
    s_spi_at_enabled = false;
    return ESP_OK;
#endif
}

bool Spi::atEnabled(void) const
{
    return s_spi_at_enabled;
}

} // namespace esp32libfun
