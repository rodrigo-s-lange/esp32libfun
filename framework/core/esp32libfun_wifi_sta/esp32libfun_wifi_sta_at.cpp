#include "esp32libfun_wifi_sta.hpp"

#include <ctype.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"
#include "nvs_flash.h"

#ifndef ESP32LIBFUN_HAS_AT
#define ESP32LIBFUN_HAS_AT 0
#endif

#if ESP32LIBFUN_HAS_AT
#include "apps/ping/ping_sock.h"

#include "../esp32libfun_at/include/esp32libfun_at.hpp"
#endif

namespace esp32libfun {

namespace {

constexpr size_t kSsidMaxLen = 32;
constexpr size_t kPasswordMaxLen = 64;
constexpr size_t kHostnameMaxLen = 32;
constexpr size_t kIpv4StringMaxLen = 16;
constexpr uint32_t kDefaultConnectTimeoutMs = 15000;
constexpr uint32_t kDefaultPingCount = 4;
constexpr uint32_t kPingTimeoutMs = 1000;
constexpr char kWifiNvsNamespace[] = "wifi_sta_at";
constexpr char kWifiNvsKeySsid[] = "ssid";
constexpr char kWifiNvsKeyPassword[] = "pass";
constexpr char kWifiNvsKeyHostname[] = "host";
constexpr char kWifiNvsKeyIp[] = "ip";
constexpr char kWifiNvsKeyGateway[] = "gw";
constexpr char kWifiNvsKeySubnet[] = "sub";

bool s_wifi_at_enabled = false;

struct WifiAtConfig {
    bool ssid_set = false;
    bool password_set = false;
    bool hostname_set = false;
    bool static_ip_set = false;
    char ssid[kSsidMaxLen + 1] = {};
    char password[kPasswordMaxLen + 1] = {};
    char hostname[kHostnameMaxLen + 1] = {};
    char ip[kIpv4StringMaxLen] = {};
    char gateway[kIpv4StringMaxLen] = {};
    char subnet[kIpv4StringMaxLen] = {};
};

WifiAtConfig s_wifi_at_config = {};

#if ESP32LIBFUN_HAS_AT

struct PingSessionState {
    SemaphoreHandle_t done = nullptr;
    uint32_t transmitted = 0;
    uint32_t received = 0;
    uint32_t duration_ms = 0;
    uint32_t last_time_ms = 0;
    bool finished = false;
    bool success = false;
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

esp_err_t ensureNvsReady(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_flash_init();
    }
    return (err == ESP_OK || err == ESP_ERR_INVALID_STATE) ? ESP_OK : err;
}

void clearPendingConfig(void)
{
    s_wifi_at_config = {};
}

void writeWifiError(const char *message)
{
    at.writeError("%s", message);
}

void writeWifiRuntimeError(const char *operation, esp_err_t err)
{
    at.writeError("%s: %s", operation, esp_err_to_name(err));
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

esp_err_t saveKeyString(nvs_handle_t handle, const char *key, const char *value, bool set_value)
{
    if (!set_value || value == nullptr || value[0] == '\0') {
        const esp_err_t err = nvs_erase_key(handle, key);
        return (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;
    }

    return nvs_set_str(handle, key, value);
}

esp_err_t readKeyString(nvs_handle_t handle, const char *key, char *value, size_t value_len, bool *set_out)
{
    if (value == nullptr || value_len == 0 || set_out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    value[0] = '\0';
    *set_out = false;

    size_t required = value_len;
    esp_err_t err = nvs_get_str(handle, key, value, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }

    if (err == ESP_OK) {
        *set_out = true;
    }

    return err;
}

esp_err_t savePendingConfigToNvs(void)
{
    if (!s_wifi_at_config.ssid_set) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ensureNvsReady();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(kWifiNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = saveKeyString(handle, kWifiNvsKeySsid, s_wifi_at_config.ssid, s_wifi_at_config.ssid_set);
    if (err == ESP_OK) {
        err = saveKeyString(handle, kWifiNvsKeyPassword, s_wifi_at_config.password, s_wifi_at_config.password_set);
    }
    if (err == ESP_OK) {
        err = saveKeyString(handle, kWifiNvsKeyHostname, s_wifi_at_config.hostname, s_wifi_at_config.hostname_set);
    }
    if (err == ESP_OK) {
        err = saveKeyString(handle, kWifiNvsKeyIp, s_wifi_at_config.ip, s_wifi_at_config.static_ip_set);
    }
    if (err == ESP_OK) {
        err = saveKeyString(handle, kWifiNvsKeyGateway, s_wifi_at_config.gateway, s_wifi_at_config.static_ip_set);
    }
    if (err == ESP_OK) {
        err = saveKeyString(handle, kWifiNvsKeySubnet, s_wifi_at_config.subnet, s_wifi_at_config.static_ip_set);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t loadPendingConfigFromNvs(void)
{
    esp_err_t err = ensureNvsReady();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(kWifiNvsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    WifiAtConfig loaded = {};
    err = readKeyString(handle, kWifiNvsKeySsid, loaded.ssid, sizeof(loaded.ssid), &loaded.ssid_set);
    if (err == ESP_OK) {
        err = readKeyString(handle, kWifiNvsKeyPassword, loaded.password, sizeof(loaded.password), &loaded.password_set);
    }
    if (err == ESP_OK) {
        err = readKeyString(handle, kWifiNvsKeyHostname, loaded.hostname, sizeof(loaded.hostname), &loaded.hostname_set);
    }
    bool ip_set = false;
    bool gateway_set = false;
    bool subnet_set = false;
    if (err == ESP_OK) {
        err = readKeyString(handle, kWifiNvsKeyIp, loaded.ip, sizeof(loaded.ip), &ip_set);
    }
    if (err == ESP_OK) {
        err = readKeyString(handle, kWifiNvsKeyGateway, loaded.gateway, sizeof(loaded.gateway), &gateway_set);
    }
    if (err == ESP_OK) {
        err = readKeyString(handle, kWifiNvsKeySubnet, loaded.subnet, sizeof(loaded.subnet), &subnet_set);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        return err;
    }

    if (!loaded.ssid_set) {
        return ESP_ERR_NOT_FOUND;
    }

    loaded.static_ip_set = ip_set && gateway_set && subnet_set;
    s_wifi_at_config = loaded;
    return ESP_OK;
}

esp_err_t erasePendingConfigFromNvs(void)
{
    esp_err_t err = ensureNvsReady();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle = 0;
    err = nvs_open(kWifiNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t applyPendingConfig(void)
{
    if (!s_wifi_at_config.ssid_set) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = wifi.hostname(s_wifi_at_config.hostname_set ? s_wifi_at_config.hostname : nullptr);
    if (err != ESP_OK) {
        return err;
    }

    if (s_wifi_at_config.static_ip_set) {
        err = wifi.ip(s_wifi_at_config.ip);
        if (err == ESP_OK) {
            err = wifi.gateway(s_wifi_at_config.gateway);
        }
        if (err == ESP_OK) {
            err = wifi.subnet(s_wifi_at_config.subnet);
        }
    } else {
        err = wifi.ip(nullptr);
        if (err == ESP_OK) {
            err = wifi.gateway(nullptr);
        }
        if (err == ESP_OK) {
            err = wifi.subnet(nullptr);
        }
    }

    if (err != ESP_OK) {
        return err;
    }

    return wifi.begin(
        s_wifi_at_config.ssid,
        s_wifi_at_config.password_set ? s_wifi_at_config.password : nullptr);
}

void printWifiStatus(void)
{
    at.writeLine("WIFICONNECTED=%d", wifi.isConnected() ? 1 : 0);
    at.writeLine("WIFIIP=%s", wifi.localIP());
    at.writeLine("WIFIGW=%s", wifi.gatewayIP());
    at.writeLine("WIFISUBNET=%s", wifi.subnetMask());
    at.writeLine("WIFIDNS1=%s", wifi.dns1());
    at.writeLine("WIFIDNS2=%s", wifi.dns2());
    at.writeLine("WIFISSID=%s", s_wifi_at_config.ssid_set ? s_wifi_at_config.ssid : "");
    at.writeLine("WIFIPASSSET=%d", s_wifi_at_config.password_set ? 1 : 0);
    at.writeLine("WIFIHOST=%s", s_wifi_at_config.hostname_set ? s_wifi_at_config.hostname : "");
    at.writeLine("WIFIMODE=%s", s_wifi_at_config.static_ip_set ? "STATIC" : "DHCP");
    if (s_wifi_at_config.static_ip_set) {
        at.writeLine("WIFISTATIC=%s,%s,%s",
                     s_wifi_at_config.ip,
                     s_wifi_at_config.gateway,
                     s_wifi_at_config.subnet);
    }
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
    state->finished = true;
    state->success = state->received > 0;

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
        const TickType_t wait_time = pdMS_TO_TICKS((count * (config.timeout_ms + config.interval_ms)) + 2000);
        if (xSemaphoreTake(done, wait_time) != pdTRUE) {
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

void atWifiCred(const char *args)
{
    char ssid[kSsidMaxLen + 1] = {};
    const char *cursor = nullptr;
    if (!parseField(args, ssid, sizeof(ssid), &cursor) || ssid[0] == '\0') {
        writeWifiError("use \"ssid\"[,\"password\"]");
        return;
    }

    char password[kPasswordMaxLen + 1] = {};
    bool password_set = false;
    if (consumeComma(&cursor)) {
        if (!parseField(cursor, password, sizeof(password), nullptr, true)) {
            writeWifiError("invalid password");
            return;
        }
        password_set = password[0] != '\0';
    }

    s_wifi_at_config.ssid_set = true;
    s_wifi_at_config.password_set = password_set;
    copyString(s_wifi_at_config.ssid, sizeof(s_wifi_at_config.ssid), ssid);
    copyString(s_wifi_at_config.password, sizeof(s_wifi_at_config.password), password_set ? password : nullptr);

    at.writeLine("WIFISSID=%s", s_wifi_at_config.ssid);
    at.writeLine("WIFIPASSSET=%d", s_wifi_at_config.password_set ? 1 : 0);
    at.writeLine("OK");
}

void atWifiHost(const char *args)
{
    char hostname[kHostnameMaxLen + 1] = {};
    if (!parseField(args, hostname, sizeof(hostname), nullptr, true)) {
        writeWifiError("use \"hostname\" or empty to clear");
        return;
    }

    const bool hostname_set = hostname[0] != '\0';
    const esp_err_t err = copyString(
        s_wifi_at_config.hostname,
        sizeof(s_wifi_at_config.hostname),
        hostname_set ? hostname : nullptr);
    if (err != ESP_OK) {
        writeWifiRuntimeError("hostname", err);
        return;
    }

    s_wifi_at_config.hostname_set = hostname_set;
    at.writeLine("WIFIHOST=%s", s_wifi_at_config.hostname_set ? s_wifi_at_config.hostname : "");
    at.writeLine("OK");
}

void atWifiIpSet(const char *args)
{
    char mode_or_ip[kIpv4StringMaxLen] = {};
    const char *cursor = nullptr;
    if (!parseField(args, mode_or_ip, sizeof(mode_or_ip), &cursor)) {
        writeWifiError("use DHCP or <ip>,<gateway>,<subnet>");
        return;
    }

    if (strcmp(mode_or_ip, "DHCP") == 0) {
        s_wifi_at_config.static_ip_set = false;
        s_wifi_at_config.ip[0] = '\0';
        s_wifi_at_config.gateway[0] = '\0';
        s_wifi_at_config.subnet[0] = '\0';
        at.writeLine("WIFIMODE=DHCP");
        at.writeLine("OK");
        return;
    }

    if (!consumeComma(&cursor)) {
        writeWifiError("use DHCP or <ip>,<gateway>,<subnet>");
        return;
    }

    char gateway[kIpv4StringMaxLen] = {};
    char subnet[kIpv4StringMaxLen] = {};
    if (!parseField(cursor, gateway, sizeof(gateway), &cursor) || !consumeComma(&cursor) ||
        !parseField(cursor, subnet, sizeof(subnet))) {
        writeWifiError("use <ip>,<gateway>,<subnet>");
        return;
    }

    ip4_addr_t ip_addr = {};
    ip4_addr_t gateway_addr = {};
    ip4_addr_t subnet_addr = {};
    if (!ip4addr_aton(mode_or_ip, &ip_addr) ||
        !ip4addr_aton(gateway, &gateway_addr) ||
        !ip4addr_aton(subnet, &subnet_addr)) {
        writeWifiError("invalid IPv4 values");
        return;
    }

    copyString(s_wifi_at_config.ip, sizeof(s_wifi_at_config.ip), mode_or_ip);
    copyString(s_wifi_at_config.gateway, sizeof(s_wifi_at_config.gateway), gateway);
    copyString(s_wifi_at_config.subnet, sizeof(s_wifi_at_config.subnet), subnet);
    s_wifi_at_config.static_ip_set = true;

    at.writeLine("WIFISTATIC=%s,%s,%s",
                 s_wifi_at_config.ip,
                 s_wifi_at_config.gateway,
                 s_wifi_at_config.subnet);
    at.writeLine("OK");
}

void atWifiIpGet(const char *args)
{
    (void)args;
    at.writeLine("WIFIIP=%s", wifi.localIP());
    at.writeLine("WIFIGW=%s", wifi.gatewayIP());
    at.writeLine("WIFISUBNET=%s", wifi.subnetMask());
    at.writeLine("WIFIDNS1=%s", wifi.dns1());
    at.writeLine("WIFIDNS2=%s", wifi.dns2());
    at.writeLine("WIFIMODE=%s", s_wifi_at_config.static_ip_set ? "STATIC" : "DHCP");
    if (s_wifi_at_config.static_ip_set) {
        at.writeLine("WIFISTATIC=%s,%s,%s",
                     s_wifi_at_config.ip,
                     s_wifi_at_config.gateway,
                     s_wifi_at_config.subnet);
    }
    at.writeLine("OK");
}

void atWifiStatus(const char *args)
{
    (void)args;
    printWifiStatus();
    at.writeLine("OK");
}

void atWifiConnect(const char *args)
{
    uint32_t timeout_ms = kDefaultConnectTimeoutMs;
    const char *text = skipSeparators(args);
    if (text != nullptr && *text != '\0' && !parseUnsigned(text, &timeout_ms)) {
        writeWifiError("use optional timeout in milliseconds");
        return;
    }

    esp_err_t err = applyPendingConfig();
    if (err != ESP_OK) {
        writeWifiRuntimeError("wifi.begin", err);
        return;
    }

    if (!wifi.waitConnected(timeout_ms)) {
        writeWifiError("connect timeout");
        return;
    }

    at.writeLine("WIFICONNECTED=1");
    at.writeLine("WIFIIP=%s", wifi.localIP());
    at.writeLine("OK");
}

void atWifiReconnect(const char *args)
{
    wifi.disconnect();
    atWifiConnect(args);
}

void atWifiDisconnect(const char *args)
{
    (void)args;
    const esp_err_t err = wifi.disconnect();
    if (err != ESP_OK) {
        writeWifiRuntimeError("wifi.disconnect", err);
        return;
    }

    at.writeLine("WIFICONNECTED=0");
    at.writeLine("WIFIIP=%s", wifi.localIP());
    at.writeLine("OK");
}

void atWifiSave(const char *args)
{
    (void)args;
    const esp_err_t err = savePendingConfigToNvs();
    if (err != ESP_OK) {
        writeWifiRuntimeError("wifisave", err);
        return;
    }

    at.writeLine("WIFISAVED=1");
    at.writeLine("OK");
}

void atWifiLoad(const char *args)
{
    (void)args;
    const esp_err_t err = loadPendingConfigFromNvs();
    if (err != ESP_OK) {
        writeWifiRuntimeError("wifiload", err);
        return;
    }

    printWifiStatus();
    at.writeLine("WIFILOAD=1");
    at.writeLine("OK");
}

void atWifiForget(const char *args)
{
    (void)args;
    esp_err_t err = erasePendingConfigFromNvs();
    if (err == ESP_OK) {
        clearPendingConfig();
        err = wifi.clean();
    }
    if (err != ESP_OK) {
        writeWifiRuntimeError("wififorget", err);
        return;
    }

    at.writeLine("WIFISAVED=0");
    at.writeLine("OK");
}

void atWifiPing(const char *args)
{
    if (!wifi.isConnected()) {
        writeWifiError("wifi not connected");
        return;
    }

    char host[64] = {};
    const char *cursor = nullptr;
    if (!parseField(args, host, sizeof(host), &cursor) || host[0] == '\0') {
        writeWifiError("use <host>[,<count>]");
        return;
    }

    uint32_t count = kDefaultPingCount;
    if (consumeComma(&cursor)) {
        if (!parseUnsigned(cursor, &count) || count == 0 || count > 16) {
            writeWifiError("count must be 1..16");
            return;
        }
    }

    PingSessionState state = {};
    const esp_err_t err = runPing(host, count, &state);
    if (err != ESP_OK) {
        writeWifiRuntimeError("wifiping", err);
        return;
    }

    at.writeLine("PINGREQ=%u", static_cast<unsigned>(state.transmitted));
    at.writeLine("PINGREP=%u", static_cast<unsigned>(state.received));
    at.writeLine("PINGTIME=%u", static_cast<unsigned>(state.duration_ms));
    at.writeLine("PINGLAST=%u", static_cast<unsigned>(state.last_time_ms));
    at.writeLine("OK");
}

void atWifiPingRouter(const char *args)
{
    if (!wifi.isConnected()) {
        writeWifiError("wifi not connected");
        return;
    }

    uint32_t count = kDefaultPingCount;
    const char *text = skipSeparators(args);
    if (text != nullptr && *text != '\0') {
        if (!parseUnsigned(text, &count) || count == 0 || count > 16) {
            writeWifiError("count must be 1..16");
            return;
        }
    }

    const char *gateway = wifi.gatewayIP();
    if (gateway == nullptr || gateway[0] == '\0' || strcmp(gateway, "0.0.0.0") == 0) {
        writeWifiError("gateway unavailable");
        return;
    }

    PingSessionState state = {};
    const esp_err_t err = runPing(gateway, count, &state);
    if (err != ESP_OK) {
        writeWifiRuntimeError("wifipingrouter", err);
        return;
    }

    at.writeLine("PINGHOST=%s", gateway);
    at.writeLine("PINGREQ=%u", static_cast<unsigned>(state.transmitted));
    at.writeLine("PINGREP=%u", static_cast<unsigned>(state.received));
    at.writeLine("PINGTIME=%u", static_cast<unsigned>(state.duration_ms));
    at.writeLine("PINGLAST=%u", static_cast<unsigned>(state.last_time_ms));
    at.writeLine("OK");
}

esp_err_t unregisterCommand(const char *command)
{
    const esp_err_t err = at.unregisterCmd(command);
    return (err == ESP_OK || err == ESP_ERR_NOT_FOUND) ? ESP_OK : err;
}

#endif

} // namespace

esp_err_t WifiSta::at(bool enable) const
{
#if !ESP32LIBFUN_HAS_AT
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!::at.isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enable) {
        if (s_wifi_at_enabled) {
            return ESP_OK;
        }

        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFICRED", atWifiCred, "Wi-Fi credentials: \"ssid\"[,\"password\"]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFIHOST", atWifiHost, "Wi-Fi hostname: \"hostname\" or empty to clear"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFIIP", atWifiIpSet, "Wi-Fi IP mode: DHCP or <ip>,<gateway>,<subnet>"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFIIP?", atWifiIpGet, "Wi-Fi local and pending IP state"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFI?", atWifiStatus, "Wi-Fi status summary"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFICONNECT", atWifiConnect, "Wi-Fi connect using pending config [timeout_ms]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFIRECONNECT", atWifiReconnect, "Wi-Fi reconnect using pending config [timeout_ms]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFIDISCONNECT", atWifiDisconnect, "Wi-Fi disconnect current session"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFISAVE", atWifiSave, "Save pending Wi-Fi config to NVS"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFILOAD", atWifiLoad, "Load Wi-Fi config from NVS"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFIFORGET", atWifiForget, "Erase saved Wi-Fi config and clean session"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFIPING", atWifiPing, "Ping host through Wi-Fi: <host>[,<count>]"));
        ESP_ERROR_CHECK(::at.registerCmd("AT+WIFIPINGROUTER", atWifiPingRouter, "Ping current Wi-Fi gateway [count]"));
        s_wifi_at_enabled = true;
        return ESP_OK;
    }

    if (!s_wifi_at_enabled) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(unregisterCommand("AT+WIFICRED"));
    ESP_ERROR_CHECK(unregisterCommand("AT+WIFIHOST"));
    ESP_ERROR_CHECK(unregisterCommand("AT+WIFIIP"));
    ESP_ERROR_CHECK(unregisterCommand("AT+WIFIIP?"));
    ESP_ERROR_CHECK(unregisterCommand("AT+WIFI?"));
    ESP_ERROR_CHECK(unregisterCommand("AT+WIFICONNECT"));
    ESP_ERROR_CHECK(unregisterCommand("AT+WIFIRECONNECT"));
    ESP_ERROR_CHECK(unregisterCommand("AT+WIFIDISCONNECT"));
    ESP_ERROR_CHECK(unregisterCommand("AT+WIFISAVE"));
    ESP_ERROR_CHECK(unregisterCommand("AT+WIFILOAD"));
    ESP_ERROR_CHECK(unregisterCommand("AT+WIFIFORGET"));
    ESP_ERROR_CHECK(unregisterCommand("AT+WIFIPING"));
    ESP_ERROR_CHECK(unregisterCommand("AT+WIFIPINGROUTER"));
    s_wifi_at_enabled = false;
    return ESP_OK;
#endif
}

bool WifiSta::atEnabled(void) const
{
    return s_wifi_at_enabled;
}

} // namespace esp32libfun
