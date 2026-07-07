#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

namespace esp32libfun {

constexpr int LAN8720_CLK_EXT_IN = 0;
constexpr int LAN8720_CLK_OUT = 1;
constexpr int LAN8720_PHY_ADDR_AUTO = -1;

class Lan8720 {
public:
    static constexpr int DEFAULT_PHY_ADDR = LAN8720_PHY_ADDR_AUTO;
    static constexpr int DEFAULT_RESET_PIN = -1;
#if CONFIG_IDF_TARGET_ESP32
    static constexpr int DEFAULT_CLOCK_ENABLE_PIN = 12;
#else
    static constexpr int DEFAULT_CLOCK_ENABLE_PIN = -1;
#endif
#if CONFIG_IDF_TARGET_ESP32
    static constexpr int DEFAULT_CLOCK_GPIO = 0;
#elif CONFIG_IDF_TARGET_ESP32P4
    static constexpr int DEFAULT_CLOCK_GPIO = 50;
#else
    static constexpr int DEFAULT_CLOCK_GPIO = 0;
#endif

    /// Initializes the internal EMAC with one LAN87xx PHY in RMII mode.
    /// RMII data pins follow the target EMAC routing; this call configures MDC,
    /// MDIO, PHY address, optional reset, RMII clock source, and optional
    /// board GPIO used as PHY power or clock enable.
    ///
    /// @param mdc_pin MDC GPIO for the MII management interface.
    /// @param mdio_pin MDIO GPIO for the MII management interface.
    /// @param phy_addr PHY address, or `LAN8720_PHY_ADDR_AUTO` to auto-detect.
    /// @param rst_pin PHY hardware reset GPIO, or `-1` if not wired.
    /// @param clock_mode `LAN8720_CLK_EXT_IN` or `LAN8720_CLK_OUT`.
    /// @param clock_gpio GPIO used for the RMII reference clock.
    /// @param clock_enable_pin Optional GPIO used as PHY power/clock enable, or `-1`.
    /// @param intr_priority Interrupt priority for the EMAC driver, or `0` for the default.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t begin(int mdc_pin,
                    int mdio_pin,
                    int phy_addr = DEFAULT_PHY_ADDR,
                    int rst_pin = DEFAULT_RESET_PIN,
                    int clock_mode = LAN8720_CLK_EXT_IN,
                    int clock_gpio = DEFAULT_CLOCK_GPIO,
                    int clock_enable_pin = DEFAULT_CLOCK_ENABLE_PIN,
                    int intr_priority = 0) const;
    /// Starts the Ethernet state machine and DHCP flow.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t start(void) const;
    /// Stops the Ethernet state machine.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t stop(void) const;
    /// Stops Ethernet, releases the driver resources, and resets the wrapper state.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(void) const;

    /// @return true when `begin()` has already succeeded.
    [[nodiscard]] bool ready(void) const;
    /// @return true when `start()` is currently active.
    [[nodiscard]] bool started(void) const;
    /// @return true when the physical link is up.
    [[nodiscard]] bool connected(void) const;
    /// @return true when a valid IPv4 address is assigned.
    [[nodiscard]] bool hasIp(void) const;
    /// Blocks until the link is up or `timeout_ms` elapses.
    /// Pass `UINT32_MAX` to wait indefinitely.
    ///
    /// @param timeout_ms Maximum time to wait, in milliseconds.
    /// @return true when connected before the timeout elapsed.
    [[nodiscard]] bool waitConnected(uint32_t timeout_ms) const;

    /// Static IP settings — all three must be set together; a partial set returns
    /// ESP_ERR_INVALID_STATE when begin() (or an individual setter after begin())
    /// tries to apply the configuration. Call all three before begin() for a
    /// clean static-IP bring-up; DHCP is used when none of them is set.
    ///
    /// @param ip Static IPv4 address as a dotted-decimal string.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t ip(const char *ip) const;
    /// @param gateway Gateway IPv4 address as a dotted-decimal string.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t gateway(const char *gateway) const;
    /// @param subnet Subnet mask as a dotted-decimal string.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t subnet(const char *subnet) const;

    /// Restarts DHCP on the Ethernet netif.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t renew(void) const;
    /// Sets the Ethernet hostname. Call before begin() for the cleanest path.
    ///
    /// @param value Hostname to advertise on the network.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t hostname(const char *value) const;
    /// Copies the current MAC address into `out[6]`.
    ///
    /// @param out Receives the 6-byte MAC address.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t mac(uint8_t out[6]) const;

    /// The three getters below copy into a per-getter static buffer and return a
    /// pointer to it. The pointer is valid until the next call to the same getter
    /// from any task — do not hold it across a context switch or a concurrent call.
    ///
    /// @return IPv4 address as text, or an empty string when not connected.
    [[nodiscard]] const char *localIP(void) const;
    /// @return Gateway IPv4 address as text.
    [[nodiscard]] const char *gateway(void) const;
    /// @return Subnet mask as text.
    [[nodiscard]] const char *subnet(void) const;
    /// @return Configured PHY address, or `LAN8720_PHY_ADDR_AUTO`.
    [[nodiscard]] int phyAddr(void) const;
    /// @return Configured RMII clock mode.
    [[nodiscard]] int clockMode(void) const;
    /// @return GPIO used for the RMII reference clock.
    [[nodiscard]] int clockGpio(void) const;
    /// @return Configured PHY power/clock enable GPIO, or `-1`.
    [[nodiscard]] int clockEnablePin(void) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t validateIpString(const char *value);
    static esp_err_t copyString(char *dst, size_t dst_len, const char *src);
    static esp_err_t applyHostname(void);
    static esp_err_t applyNetifConfig(void);
};

extern Lan8720 lan8720;

} // namespace esp32libfun

using esp32libfun::lan8720;
using esp32libfun::Lan8720;
using esp32libfun::LAN8720_CLK_EXT_IN;
using esp32libfun::LAN8720_CLK_OUT;
using esp32libfun::LAN8720_PHY_ADDR_AUTO;
