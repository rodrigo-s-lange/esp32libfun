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
    esp_err_t begin(int mdc_pin,
                    int mdio_pin,
                    int phy_addr = DEFAULT_PHY_ADDR,
                    int rst_pin = DEFAULT_RESET_PIN,
                    int clock_mode = LAN8720_CLK_EXT_IN,
                    int clock_gpio = DEFAULT_CLOCK_GPIO,
                    int clock_enable_pin = DEFAULT_CLOCK_ENABLE_PIN,
                    int intr_priority = 0) const;
    /// Starts the Ethernet state machine and DHCP flow.
    esp_err_t start(void) const;
    /// Stops the Ethernet state machine.
    esp_err_t stop(void) const;
    /// Stops Ethernet, releases the driver resources, and resets the wrapper state.
    esp_err_t end(void) const;

    [[nodiscard]] bool ready(void) const;
    [[nodiscard]] bool started(void) const;
    [[nodiscard]] bool connected(void) const;
    [[nodiscard]] bool hasIp(void) const;
    /// Blocks until the link is up or `timeout_ms` elapses.
    /// Pass `UINT32_MAX` to wait indefinitely.
    [[nodiscard]] bool waitConnected(uint32_t timeout_ms) const;

    /// Static IP settings — all three must be set together; a partial set returns
    /// ESP_ERR_INVALID_STATE when begin() (or an individual setter after begin())
    /// tries to apply the configuration. Call all three before begin() for a
    /// clean static-IP bring-up; DHCP is used when none of them is set.
    esp_err_t ip(const char *ip) const;
    esp_err_t gateway(const char *gateway) const;
    esp_err_t subnet(const char *subnet) const;

    /// Restarts DHCP on the Ethernet netif.
    esp_err_t renew(void) const;
    /// Sets the Ethernet hostname. Call before begin() for the cleanest path.
    esp_err_t hostname(const char *value) const;
    /// Copies the current MAC address into `out[6]`.
    esp_err_t mac(uint8_t out[6]) const;

    /// The three getters below copy into a per-getter static buffer and return a
    /// pointer to it. The pointer is valid until the next call to the same getter
    /// from any task — do not hold it across a context switch or a concurrent call.
    [[nodiscard]] const char *localIP(void) const;
    [[nodiscard]] const char *gateway(void) const;
    [[nodiscard]] const char *subnet(void) const;
    [[nodiscard]] int phyAddr(void) const;
    [[nodiscard]] int clockMode(void) const;
    [[nodiscard]] int clockGpio(void) const;
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
