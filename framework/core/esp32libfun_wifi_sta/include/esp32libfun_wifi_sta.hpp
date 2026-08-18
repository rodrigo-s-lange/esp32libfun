#pragma once

#include <stdint.h>

#include "esp_err.h"

#define ESP32LIBFUN_WIFI_STA_VERSION "v0.1.2"
#define ESP32LIBFUN_WIFI_STA_VERSION_MAJOR 0
#define ESP32LIBFUN_WIFI_STA_VERSION_MINOR 1
#define ESP32LIBFUN_WIFI_STA_VERSION_PATCH 2

namespace esp32libfun {

/// Thin wrapper around the ESP-IDF Wi-Fi station path.
class WifiSta {
public:
    /// Starts or reconfigures the STA connection flow.
    ///
    /// This core wrapper intentionally keeps Wi-Fi semantics close to ESP-IDF.
    /// It is not required to follow the richer `init/start/stop/end` pattern
    /// used by some `framework/libs/esp_*` components.
    ///
    /// @param ssid Network SSID to connect to.
    /// @param password Network password, or `nullptr` for an open network.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t begin(const char *ssid, const char *password = nullptr) const;
    /// Clears the current STA configuration and resets the wrapper state.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t clean(void) const;
    /// Disconnects the current STA session without clearing saved configuration.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t disconnect(void) const;
    /// Returns true when the STA netif currently holds a connected state.
    ///
    /// @return true when currently connected.
    [[nodiscard]] bool isConnected(void) const;
    /// Waits for the STA connection bit until timeout.
    ///
    /// @param timeout_ms Maximum time to wait, in milliseconds.
    /// @return true when connected before the timeout elapsed.
    [[nodiscard]] bool waitConnected(uint32_t timeout_ms) const;

    /// Sets the static IPv4 address to apply before `begin()`.
    ///
    /// @param ip IPv4 address as a dotted-decimal string.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t ip(const char *ip) const;
    /// Sets the static IPv4 gateway to apply before `begin()`.
    ///
    /// @param gateway Gateway IPv4 address as a dotted-decimal string.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t gateway(const char *gateway) const;
    /// Sets the static IPv4 subnet mask to apply before `begin()`.
    ///
    /// @param subnet Subnet mask as a dotted-decimal string.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t subnet(const char *subnet) const;

    /// Sets the station hostname to apply before `begin()`.
    ///
    /// @param hostname Hostname to advertise on the network.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t hostname(const char *hostname) const;

    /// Returns the last IPv4 address observed by the event handler.
    ///
    /// @return IPv4 address as text, or an empty string when not connected.
    [[nodiscard]] const char *localIP(void) const;
    /// Returns the current IPv4 gateway reported by the STA netif.
    ///
    /// @return Gateway IPv4 address as text.
    [[nodiscard]] const char *gatewayIP(void) const;
    /// Returns the current IPv4 subnet mask reported by the STA netif.
    ///
    /// @return Subnet mask as text.
    [[nodiscard]] const char *subnetMask(void) const;
    /// Returns the current primary DNS server reported by the STA netif.
    ///
    /// @return Primary DNS server IPv4 address as text.
    [[nodiscard]] const char *dns1(void) const;
    /// Returns the current secondary DNS server reported by the STA netif.
    ///
    /// @return Secondary DNS server IPv4 address as text.
    [[nodiscard]] const char *dns2(void) const;

    /// Enables or disables the optional Wi-Fi STA AT sidecar commands.
    ///
    /// @param enable true to register the sidecar commands, false to unregister them.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t at(bool enable = true) const;
    /// Returns true when the Wi-Fi STA AT sidecar is registered.
    ///
    /// @return true when the sidecar commands are currently registered.
    [[nodiscard]] bool atEnabled(void) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t initStack(void);
    static esp_err_t validateIpString(const char *value);
    static esp_err_t copyString(char *dst, size_t dst_len, const char *src);
    static esp_err_t applyHostname(void);
    static esp_err_t applyNetifConfig(void);
};

/// Global Wi-Fi station convenience object.
extern WifiSta wifi;

} // namespace esp32libfun

using esp32libfun::wifi;
