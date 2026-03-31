#pragma once

#include <stdint.h>

#include "esp_err.h"

namespace esp32libfun {

class WifiSta {
public:
    esp_err_t begin(const char *ssid, const char *password = nullptr) const;
    // Clears stored STA credentials/config and resets the wrapper state.
    esp_err_t clean(void) const;
    esp_err_t disconnect(void) const;
    [[nodiscard]] bool isConnected(void) const;
    [[nodiscard]] bool waitConnected(uint32_t timeout_ms) const;

    // Static network settings must be configured before begin().
    esp_err_t ip(const char *ip) const;
    esp_err_t gateway(const char *gateway) const;
    esp_err_t subnet(const char *subnet) const;

    // Hostname should be configured before begin().
    esp_err_t hostname(const char *hostname) const;

    [[nodiscard]] const char *localIP(void) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t initStack(void);
    static esp_err_t validateIpString(const char *value);
    static esp_err_t copyString(char *dst, size_t dst_len, const char *src);
    static esp_err_t applyHostname(void);
    static esp_err_t applyNetifConfig(void);
};

extern WifiSta wifi;

} // namespace esp32libfun

using esp32libfun::wifi;
