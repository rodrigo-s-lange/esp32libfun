#include "esp32libfun_w5500.hpp"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/netdb.h"

#ifndef ESP32LIBFUN_HAS_AT
#define ESP32LIBFUN_HAS_AT 0
#endif

#if ESP32LIBFUN_HAS_AT
#include "apps/ping/ping_sock.h"

#include "../esp32libfun_at/include/esp32libfun_at.hpp"
#endif

namespace esp32libfun {

namespace {

constexpr size_t kHostnameMaxLen = 32;
constexpr size_t kIpv4StringMaxLen = 16;
constexpr uint32_t kDefaultStartTimeoutMs = 15000;
constexpr uint32_t kDefaultPingCount = 4;
constexpr uint32_t kPingTimeoutMs = 1000;
constexpr char kExternalPingHost[] = "8.8.8.8";

bool s_w5500_at_enabled = false;

struct W5500AtConfig {
    bool bus_set = false;
    bool hostname_set = false;
    bool static_ip_set = false;
    int miso_pin = -1;
    int mosi_pin = -1;
    int sclk_pin = -1;
    int cs_pin = -1;
    int int_pin = -1;
    int rst_pin = -1;
    int host = W5500::DEFAULT_HOST;
    uint32_t clock_hz = W5500::DEFAULT_CLOCK_HZ;
    size_t queue_size = W5500::DEFAULT_QUEUE_SIZE;
    uint32_t poll_period_ms = W5500::DEFAULT_POLL_PERIOD_MS;
    char hostname[kHostnameMaxLen + 1] = {};
    char ip[kIpv4StringMaxLen] = {};
    char gateway[kIpv4StringMaxLen] = {};
    char subnet[kIpv4StringMaxLen] = {};
};

W5500AtConfig s_w5500_at_config = {};

#if ESP32LIBFUN_HAS_AT

struct PingSessionState {
    SemaphoreHandle_t done = nullptr;
    uint32_t transmitted = 0;
    uint32_t received = 0;
    uint32_t duration_ms = 0;
    uint32_t last_time_ms = 0;
};

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

bool parseInteger(const char *text, int32_t *value_out, const char **next_out = nullptr)
{
    if (text == nullptr || value_out == nullptr) {
        return false;
    }

    text = skipSeparators(text);
    if (*text == '\0') {
        return false;
    }

    char *end = nullptr;
    const long value = strtol(text, &end, 0);
    if (end == text) {
        return false;
    }

    *value_out = static_cast<int32_t>(value);
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
            char ch = *text;
            if (ch == '\\' && text[1] != '\0') {
                ++text;
                ch = *text;
            }

            if (index + 1 >= out_len) {
                return false;
            }

            out[index++] = ch;
            ++text;
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

bool isValidHostValue(int host)
{
    switch (host) {
        case SPI_HOST_2:
#if SOC_SPI_PERIPH_NUM > 2
        case SPI_HOST_3:
#endif
            return true;
        default:
            return false;
    }
}

esp_err_t copyString(char *dst, size_t dst_len, const char *src)
{
    if (dst == nullptr || dst_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (src == nullptr || src[0] == '\0') {
        dst[0] = '\0';
        return ESP_OK;
    }

    const size_t len = strlen(src);
    if (len >= dst_len) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(dst, src, len + 1);
    return ESP_OK;
}

void syncPendingConfigFromRuntime(void)
{
    if (!w5500.ready()) {
        return;
    }

    s_w5500_at_config.bus_set = true;
    s_w5500_at_config.miso_pin = w5500.misoPin();
    s_w5500_at_config.mosi_pin = w5500.mosiPin();
    s_w5500_at_config.sclk_pin = w5500.sclkPin();
    s_w5500_at_config.cs_pin = w5500.csPin();
    s_w5500_at_config.int_pin = w5500.intPin();
    s_w5500_at_config.rst_pin = w5500.rstPin();
    s_w5500_at_config.host = w5500.host();
    s_w5500_at_config.clock_hz = w5500.clockHz();
    s_w5500_at_config.queue_size = w5500.queueSize();
    s_w5500_at_config.poll_period_ms = w5500.pollPeriodMs();

    const char *hostname = w5500.hostname();
    s_w5500_at_config.hostname_set = hostname != nullptr && hostname[0] != '\0';
    copyString(s_w5500_at_config.hostname,
               sizeof(s_w5500_at_config.hostname),
               s_w5500_at_config.hostname_set ? hostname : nullptr);
}

void writeW5500Error(const char *message)
{
    at.writeError("%s", message);
}

void writeW5500RuntimeError(const char *operation, esp_err_t err)
{
    at.writeError("%s: %s", operation, esp_err_to_name(err));
}

bool waitForIp(uint32_t timeout_ms)
{
    const uint32_t step_ms = 100;
    uint32_t elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
        if (w5500.hasIp()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        elapsed_ms += step_ms;
    }
    return w5500.hasIp();
}

void printPendingConfig(void)
{
    at.writeLine("W5500CFG=%d,%d,%d,%d,%d,%d,%d,%lu,%u,%lu",
                 s_w5500_at_config.miso_pin,
                 s_w5500_at_config.mosi_pin,
                 s_w5500_at_config.sclk_pin,
                 s_w5500_at_config.cs_pin,
                 s_w5500_at_config.int_pin,
                 s_w5500_at_config.rst_pin,
                 s_w5500_at_config.host,
                 static_cast<unsigned long>(s_w5500_at_config.clock_hz),
                 static_cast<unsigned>(s_w5500_at_config.queue_size),
                 static_cast<unsigned long>(s_w5500_at_config.poll_period_ms));
    at.writeLine("W5500HOST=%s", s_w5500_at_config.hostname_set ? s_w5500_at_config.hostname : "");
    at.writeLine("W5500MODE=%s", s_w5500_at_config.static_ip_set ? "STATIC" : "DHCP");
    if (s_w5500_at_config.static_ip_set) {
        at.writeLine("W5500STATIC=%s,%s,%s",
                     s_w5500_at_config.ip,
                     s_w5500_at_config.gateway,
                     s_w5500_at_config.subnet);
    }
}

void printRuntimeStatus(void)
{
    at.writeLine("W5500READY=%d", w5500.ready() ? 1 : 0);
    at.writeLine("W5500STARTED=%d", w5500.started() ? 1 : 0);
    at.writeLine("W5500LINK=%d", w5500.connected() ? 1 : 0);
    at.writeLine("W5500HASIP=%d", w5500.hasIp() ? 1 : 0);
    at.writeLine("W5500IP=%s", w5500.localIP());
    at.writeLine("W5500GW=%s", w5500.gateway());
    at.writeLine("W5500SUBNET=%s", w5500.subnet());
}

bool pendingBusMatchesRuntime(void)
{
    return w5500.ready() &&
           w5500.misoPin() == s_w5500_at_config.miso_pin &&
           w5500.mosiPin() == s_w5500_at_config.mosi_pin &&
           w5500.sclkPin() == s_w5500_at_config.sclk_pin &&
           w5500.csPin() == s_w5500_at_config.cs_pin &&
           w5500.intPin() == s_w5500_at_config.int_pin &&
           w5500.rstPin() == s_w5500_at_config.rst_pin &&
           w5500.host() == s_w5500_at_config.host &&
           w5500.clockHz() == s_w5500_at_config.clock_hz &&
           w5500.queueSize() == s_w5500_at_config.queue_size &&
           w5500.pollPeriodMs() == s_w5500_at_config.poll_period_ms;
}

esp_err_t applyPendingNetworkConfig(void)
{
    esp_err_t err = w5500.hostname(s_w5500_at_config.hostname_set ? s_w5500_at_config.hostname : nullptr);
    if (err != ESP_OK) {
        return err;
    }

    if (s_w5500_at_config.static_ip_set) {
        err = w5500.ip(s_w5500_at_config.ip);
        if (err == ESP_OK) {
            err = w5500.gateway(s_w5500_at_config.gateway);
        }
        if (err == ESP_OK) {
            err = w5500.subnet(s_w5500_at_config.subnet);
        }
        return err;
    }

    err = w5500.ip(nullptr);
    if (err == ESP_OK) {
        err = w5500.gateway(nullptr);
    }
    if (err == ESP_OK) {
        err = w5500.subnet(nullptr);
    }
    return err;
}

esp_err_t applyPendingConfig(uint32_t timeout_ms)
{
    if (!s_w5500_at_config.bus_set) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_OK;
    if (w5500.ready()) {
        if (!pendingBusMatchesRuntime()) {
            return ESP_ERR_NOT_SUPPORTED;
        }

        err = w5500.stop();
        if (err != ESP_OK) {
            return err;
        }
    } else {
        err = w5500.begin(s_w5500_at_config.miso_pin,
                          s_w5500_at_config.mosi_pin,
                          s_w5500_at_config.sclk_pin,
                          s_w5500_at_config.cs_pin,
                          s_w5500_at_config.int_pin,
                          s_w5500_at_config.rst_pin,
                          s_w5500_at_config.host,
                          s_w5500_at_config.clock_hz,
                          s_w5500_at_config.queue_size,
                          s_w5500_at_config.poll_period_ms);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = applyPendingNetworkConfig();
    if (err != ESP_OK) {
        return err;
    }

    err = w5500.start();
    if (err != ESP_OK) {
        return err;
    }

    if (timeout_ms == 0) {
        return ESP_OK;
    }

    if (!w5500.waitConnected(timeout_ms)) {
        return ESP_ERR_TIMEOUT;
    }

    if (!waitForIp(timeout_ms)) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

void onPingEnd(esp_ping_handle_t hdl, void *args)
{
    PingSessionState *state = static_cast<PingSessionState *>(args);
    if (state == nullptr) {
        return;
    }

    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &state->transmitted, sizeof(state->transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &state->received, sizeof(state->received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &state->duration_ms, sizeof(state->duration_ms));
    if (state->done != nullptr) {
        xSemaphoreGive(state->done);
    }
}

void onPingSuccess(esp_ping_handle_t hdl, void *args)
{
    PingSessionState *state = static_cast<PingSessionState *>(args);
    if (state == nullptr) {
        return;
    }

    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &state->last_time_ms, sizeof(state->last_time_ms));
}

esp_err_t runPing(const char *host, uint32_t count, PingSessionState *state_out)
{
    if (host == nullptr || host[0] == '\0' || state_out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    struct addrinfo hint = {};
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_RAW;
    struct addrinfo *result = nullptr;
    if (getaddrinfo(host, nullptr, &hint, &result) != 0 || result == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    ip_addr_t target_addr = {};
    const struct sockaddr_in *address = reinterpret_cast<const struct sockaddr_in *>(result->ai_addr);
    inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &address->sin_addr);
    freeaddrinfo(result);

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (done == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    PingSessionState state = {};
    state.done = done;

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count = count;
    config.timeout_ms = kPingTimeoutMs;
    config.interval_ms = 250;
    config.target_addr = target_addr;

    esp_ping_callbacks_t callbacks = {};
    callbacks.cb_args = &state;
    callbacks.on_ping_success = &onPingSuccess;
    callbacks.on_ping_end = &onPingEnd;

    esp_ping_handle_t handle = nullptr;
    esp_err_t err = esp_ping_new_session(&config, &callbacks, &handle);
    if (err == ESP_OK) {
        err = esp_ping_start(handle);
    }

    if (err == ESP_OK) {
        const TickType_t wait_ticks = pdMS_TO_TICKS((count * (config.timeout_ms + config.interval_ms)) + 2000);
        if (xSemaphoreTake(done, wait_ticks) != pdTRUE) {
            err = ESP_ERR_TIMEOUT;
        }
    }

    if (handle != nullptr) {
        esp_ping_delete_session(handle);
    }

    vSemaphoreDelete(done);

    if (err == ESP_OK) {
        *state_out = state;
    }

    return err;
}

void writePingResult(const char *host, const PingSessionState &state)
{
    at.writeLine("PINGHOST=%s", host);
    at.writeLine("PINGREQ=%u", static_cast<unsigned>(state.transmitted));
    at.writeLine("PINGREP=%u", static_cast<unsigned>(state.received));
    at.writeLine("PINGTIME=%u", static_cast<unsigned>(state.duration_ms));
    at.writeLine("PINGLAST=%u", static_cast<unsigned>(state.last_time_ms));
    at.writeLine("OK");
}

void atW5500Cfg(const char *args)
{
    const char *cursor = args;
    int32_t miso_pin = -1;
    int32_t mosi_pin = -1;
    int32_t sclk_pin = -1;
    int32_t cs_pin = -1;
    int32_t int_pin = -1;
    int32_t rst_pin = -1;
    int32_t host = W5500::DEFAULT_HOST;
    uint32_t clock_hz = W5500::DEFAULT_CLOCK_HZ;
    uint32_t queue_size = static_cast<uint32_t>(W5500::DEFAULT_QUEUE_SIZE);
    uint32_t poll_period_ms = W5500::DEFAULT_POLL_PERIOD_MS;

    if (!parseInteger(cursor, &miso_pin, &cursor) ||
        !consumeComma(&cursor) ||
        !parseInteger(cursor, &mosi_pin, &cursor) ||
        !consumeComma(&cursor) ||
        !parseInteger(cursor, &sclk_pin, &cursor) ||
        !consumeComma(&cursor) ||
        !parseInteger(cursor, &cs_pin, &cursor)) {
        writeW5500Error("use <miso>,<mosi>,<sclk>,<cs>[,<int>[,<rst>[,<host>[,<clock>[,<queue>[,<poll_ms>]]]]]]");
        return;
    }

    if (consumeComma(&cursor) && !parseInteger(cursor, &int_pin, &cursor)) {
        writeW5500Error("invalid INT pin");
        return;
    }
    if (consumeComma(&cursor) && !parseInteger(cursor, &rst_pin, &cursor)) {
        writeW5500Error("invalid RST pin");
        return;
    }
    if (consumeComma(&cursor) && !parseInteger(cursor, &host, &cursor)) {
        writeW5500Error("invalid SPI host");
        return;
    }
    if (consumeComma(&cursor) && !parseUnsigned(cursor, &clock_hz, &cursor)) {
        writeW5500Error("invalid SPI clock");
        return;
    }
    if (consumeComma(&cursor) && !parseUnsigned(cursor, &queue_size, &cursor)) {
        writeW5500Error("invalid queue size");
        return;
    }
    if (consumeComma(&cursor) && !parseUnsigned(cursor, &poll_period_ms, &cursor)) {
        writeW5500Error("invalid poll period");
        return;
    }

    if (miso_pin < 0 || mosi_pin < 0 || sclk_pin < 0 || cs_pin < 0 ||
        !isValidHostValue(host) ||
        queue_size == 0 ||
        (int_pin < 0 && poll_period_ms == 0)) {
        writeW5500Error("invalid W5500 configuration");
        return;
    }

    s_w5500_at_config.bus_set = true;
    s_w5500_at_config.miso_pin = miso_pin;
    s_w5500_at_config.mosi_pin = mosi_pin;
    s_w5500_at_config.sclk_pin = sclk_pin;
    s_w5500_at_config.cs_pin = cs_pin;
    s_w5500_at_config.int_pin = int_pin;
    s_w5500_at_config.rst_pin = rst_pin;
    s_w5500_at_config.host = host;
    s_w5500_at_config.clock_hz = clock_hz;
    s_w5500_at_config.queue_size = queue_size;
    s_w5500_at_config.poll_period_ms = poll_period_ms;

    printPendingConfig();
    at.writeLine("OK");
}

void atW5500Host(const char *args)
{
    char hostname[kHostnameMaxLen + 1] = {};
    if (!parseField(args, hostname, sizeof(hostname), nullptr, true)) {
        writeW5500Error("use \"hostname\" or empty to clear");
        return;
    }

    const bool hostname_set = hostname[0] != '\0';
    const esp_err_t err = copyString(s_w5500_at_config.hostname,
                                     sizeof(s_w5500_at_config.hostname),
                                     hostname_set ? hostname : nullptr);
    if (err != ESP_OK) {
        writeW5500RuntimeError("hostname", err);
        return;
    }

    s_w5500_at_config.hostname_set = hostname_set;
    at.writeLine("W5500HOST=%s", s_w5500_at_config.hostname_set ? s_w5500_at_config.hostname : "");
    at.writeLine("OK");
}

void atW5500IpSet(const char *args)
{
    char mode_or_ip[kIpv4StringMaxLen] = {};
    const char *cursor = nullptr;
    if (!parseField(args, mode_or_ip, sizeof(mode_or_ip), &cursor)) {
        writeW5500Error("use DHCP or <ip>,<gateway>,<subnet>");
        return;
    }

    if (strcmp(mode_or_ip, "DHCP") == 0) {
        s_w5500_at_config.static_ip_set = false;
        s_w5500_at_config.ip[0] = '\0';
        s_w5500_at_config.gateway[0] = '\0';
        s_w5500_at_config.subnet[0] = '\0';
        at.writeLine("W5500MODE=DHCP");
        at.writeLine("OK");
        return;
    }

    if (!consumeComma(&cursor)) {
        writeW5500Error("use DHCP or <ip>,<gateway>,<subnet>");
        return;
    }

    char gateway[kIpv4StringMaxLen] = {};
    char subnet[kIpv4StringMaxLen] = {};
    if (!parseField(cursor, gateway, sizeof(gateway), &cursor) ||
        !consumeComma(&cursor) ||
        !parseField(cursor, subnet, sizeof(subnet))) {
        writeW5500Error("use <ip>,<gateway>,<subnet>");
        return;
    }

    ip4_addr_t ip_addr = {};
    ip4_addr_t gateway_addr = {};
    ip4_addr_t subnet_addr = {};
    if (!ip4addr_aton(mode_or_ip, &ip_addr) ||
        !ip4addr_aton(gateway, &gateway_addr) ||
        !ip4addr_aton(subnet, &subnet_addr)) {
        writeW5500Error("invalid IPv4 values");
        return;
    }

    copyString(s_w5500_at_config.ip, sizeof(s_w5500_at_config.ip), mode_or_ip);
    copyString(s_w5500_at_config.gateway, sizeof(s_w5500_at_config.gateway), gateway);
    copyString(s_w5500_at_config.subnet, sizeof(s_w5500_at_config.subnet), subnet);
    s_w5500_at_config.static_ip_set = true;

    at.writeLine("W5500STATIC=%s,%s,%s",
                 s_w5500_at_config.ip,
                 s_w5500_at_config.gateway,
                 s_w5500_at_config.subnet);
    at.writeLine("OK");
}

void atW5500IpGet(const char *args)
{
    (void)args;
    at.writeLine("W5500IP=%s", w5500.localIP());
    at.writeLine("W5500GW=%s", w5500.gateway());
    at.writeLine("W5500SUBNET=%s", w5500.subnet());
    at.writeLine("W5500MODE=%s", s_w5500_at_config.static_ip_set ? "STATIC" : "DHCP");
    if (s_w5500_at_config.static_ip_set) {
        at.writeLine("W5500STATIC=%s,%s,%s",
                     s_w5500_at_config.ip,
                     s_w5500_at_config.gateway,
                     s_w5500_at_config.subnet);
    }
    at.writeLine("OK");
}

void atW5500Status(const char *args)
{
    (void)args;
    printPendingConfig();
    printRuntimeStatus();
    at.writeLine("OK");
}

void atW5500Start(const char *args)
{
    uint32_t timeout_ms = kDefaultStartTimeoutMs;
    const char *text = skipSeparators(args);
    if (text != nullptr && *text != '\0' && !parseUnsigned(text, &timeout_ms)) {
        writeW5500Error("use optional timeout in milliseconds");
        return;
    }

    if (!s_w5500_at_config.bus_set) {
        syncPendingConfigFromRuntime();
    }

    const esp_err_t err = applyPendingConfig(timeout_ms);
    if (err == ESP_ERR_INVALID_STATE) {
        writeW5500Error("configure the bus first with AT+W5500CFG");
        return;
    }
    if (err == ESP_ERR_NOT_SUPPORTED) {
        writeW5500Error("bus change after begin is not supported yet");
        return;
    }
    if (err != ESP_OK) {
        writeW5500RuntimeError("w5500.start", err);
        return;
    }

    printRuntimeStatus();
    at.writeLine("OK");
}

void atW5500Stop(const char *args)
{
    (void)args;
    const esp_err_t err = w5500.stop();
    if (err != ESP_OK) {
        writeW5500RuntimeError("w5500.stop", err);
        return;
    }

    at.writeLine("W5500STARTED=0");
    at.writeLine("OK");
}

void atW5500End(const char *args)
{
    (void)args;
    const esp_err_t err = w5500.end();
    if (err != ESP_OK) {
        writeW5500RuntimeError("w5500.end", err);
        return;
    }

    at.writeLine("W5500READY=0");
    at.writeLine("OK");
}

void atW5500Renew(const char *args)
{
    (void)args;
    if (s_w5500_at_config.static_ip_set) {
        writeW5500Error("dhcp renew is disabled in STATIC mode");
        return;
    }

    const esp_err_t err = w5500.renew();
    if (err != ESP_OK) {
        writeW5500RuntimeError("w5500.renew", err);
        return;
    }

    at.writeLine("W5500MODE=DHCP");
    at.writeLine("OK");
}

void atW5500Ping(const char *args)
{
    if (!w5500.hasIp()) {
        writeW5500Error("w5500 has no IPv4 address");
        return;
    }

    char host[64] = {};
    const char *cursor = nullptr;
    if (!parseField(args, host, sizeof(host), &cursor) || host[0] == '\0') {
        writeW5500Error("use <host>[,<count>]");
        return;
    }

    uint32_t count = kDefaultPingCount;
    if (consumeComma(&cursor)) {
        if (!parseUnsigned(cursor, &count) || count == 0 || count > 16) {
            writeW5500Error("count must be 1..16");
            return;
        }
    }

    PingSessionState state = {};
    const esp_err_t err = runPing(host, count, &state);
    if (err != ESP_OK) {
        writeW5500RuntimeError("w5500ping", err);
        return;
    }

    writePingResult(host, state);
}

void atW5500PingRouter(const char *args)
{
    if (!w5500.hasIp()) {
        writeW5500Error("w5500 has no IPv4 address");
        return;
    }

    uint32_t count = kDefaultPingCount;
    const char *text = skipSeparators(args);
    if (text != nullptr && *text != '\0') {
        if (!parseUnsigned(text, &count) || count == 0 || count > 16) {
            writeW5500Error("count must be 1..16");
            return;
        }
    }

    const char *gateway = w5500.gateway();
    if (gateway == nullptr || gateway[0] == '\0' || strcmp(gateway, "0.0.0.0") == 0) {
        writeW5500Error("gateway unavailable");
        return;
    }

    PingSessionState state = {};
    const esp_err_t err = runPing(gateway, count, &state);
    if (err != ESP_OK) {
        writeW5500RuntimeError("w5500pingrouter", err);
        return;
    }

    writePingResult(gateway, state);
}

void atW5500PingExternal(const char *args)
{
    if (!w5500.hasIp()) {
        writeW5500Error("w5500 has no IPv4 address");
        return;
    }

    uint32_t count = kDefaultPingCount;
    const char *text = skipSeparators(args);
    if (text != nullptr && *text != '\0') {
        if (!parseUnsigned(text, &count) || count == 0 || count > 16) {
            writeW5500Error("count must be 1..16");
            return;
        }
    }

    PingSessionState state = {};
    const esp_err_t err = runPing(kExternalPingHost, count, &state);
    if (err != ESP_OK) {
        writeW5500RuntimeError("w5500pingexternal", err);
        return;
    }

    writePingResult(kExternalPingHost, state);
}

esp_err_t unregisterCommand(const char *command)
{
    const esp_err_t err = at.unregisterCmd(command);
    return (err == ESP_OK || err == ESP_ERR_NOT_FOUND) ? ESP_OK : err;
}

#endif

} // namespace

esp_err_t W5500::at(bool enable) const
{
#if !ESP32LIBFUN_HAS_AT
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!::at.isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        if (s_w5500_at_enabled) {
            return ESP_OK;
        }

        syncPendingConfigFromRuntime();
        ESP_ERROR_CHECK(::at.registerCmd("AT+W5500CFG", atW5500Cfg, "W5500 cfg: <miso>,<mosi>,<sclk>,<cs>[,<int>[,<rst>...]]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+W5500HOST", atW5500Host, "W5500 host: \"name\" or empty"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+W5500IP", atW5500IpSet, "W5500 IP: DHCP or <ip>,<gateway>,<subnet>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+W5500IP?", atW5500IpGet, "W5500 IPv4 and pending mode"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+W5500?", atW5500Status, "W5500 status"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+W5500START", atW5500Start, "W5500 start with pending cfg [timeout_ms]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+W5500STOP", atW5500Stop, "W5500 stop"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+W5500END", atW5500End, "W5500 end and free bus"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+W5500RENEW", atW5500Renew, "W5500 DHCP renew"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+W5500PING", atW5500Ping, "W5500 ping: <host>[,<count>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+W5500PINGROUTER", atW5500PingRouter, "W5500 gateway ping [count]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+W5500PINGEXTERNAL", atW5500PingExternal, "W5500 ping 8.8.8.8 [count]"));
        s_w5500_at_enabled = true;
        return ESP_OK;
    }

    if (!s_w5500_at_enabled) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(unregisterCommand("AT+W5500CFG"));
    ESP_ERROR_CHECK(unregisterCommand("AT+W5500HOST"));
    ESP_ERROR_CHECK(unregisterCommand("AT+W5500IP"));
    ESP_ERROR_CHECK(unregisterCommand("AT+W5500IP?"));
    ESP_ERROR_CHECK(unregisterCommand("AT+W5500?"));
    ESP_ERROR_CHECK(unregisterCommand("AT+W5500START"));
    ESP_ERROR_CHECK(unregisterCommand("AT+W5500STOP"));
    ESP_ERROR_CHECK(unregisterCommand("AT+W5500END"));
    ESP_ERROR_CHECK(unregisterCommand("AT+W5500RENEW"));
    ESP_ERROR_CHECK(unregisterCommand("AT+W5500PING"));
    ESP_ERROR_CHECK(unregisterCommand("AT+W5500PINGROUTER"));
    ESP_ERROR_CHECK(unregisterCommand("AT+W5500PINGEXTERNAL"));
    s_w5500_at_enabled = false;
    return ESP_OK;
#endif
}

bool W5500::atEnabled(void) const
{
    return s_w5500_at_enabled;
}

} // namespace esp32libfun
