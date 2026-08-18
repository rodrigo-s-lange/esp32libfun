#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "soc/soc_caps.h"

#include "../../esp32libfun_spi/include/esp32libfun_spi.hpp"

#define ESP32LIBFUN_W5500_VERSION "v0.1.1"
#define ESP32LIBFUN_W5500_VERSION_MAJOR 0
#define ESP32LIBFUN_W5500_VERSION_MINOR 1
#define ESP32LIBFUN_W5500_VERSION_PATCH 1

namespace esp32libfun {

/// Thin W5500 Ethernet wrapper built on top of the official ESP-IDF driver.
///
/// This module owns one dedicated SPI bus for the W5500 and exposes one small
/// lifecycle around `begin()`, `start()`, `stop()`, and `end()`.
class W5500 {
public:
#if SOC_SPI_PERIPH_NUM > 2
    static constexpr int DEFAULT_HOST = SPI_HOST_2;
#else
    static constexpr int DEFAULT_HOST = SPI_HOST_DEFAULT;
#endif

    static constexpr uint32_t DEFAULT_CLOCK_HZ = 40000000;
    static constexpr size_t DEFAULT_QUEUE_SIZE = 20;
    static constexpr uint32_t DEFAULT_POLL_PERIOD_MS = 10;

    /// Initializes the W5500 Ethernet backend on one dedicated SPI bus.
    ///
    /// `int_pin = -1` enables polling mode. Otherwise the driver uses the W5500
    /// interrupt pin and does not poll periodically.
    ///
    /// @param miso_pin MISO GPIO for the dedicated SPI bus.
    /// @param mosi_pin MOSI GPIO for the dedicated SPI bus.
    /// @param sclk_pin SCLK GPIO for the dedicated SPI bus.
    /// @param cs_pin Chip-select GPIO for the W5500.
    /// @param int_pin W5500 interrupt GPIO, or `-1` to poll instead.
    /// @param rst_pin W5500 hardware reset GPIO, or `-1` if not wired.
    /// @param host SPI host to dedicate to the W5500.
    /// @param clock_hz SPI clock speed in Hz.
    /// @param queue_size SPI transaction queue depth.
    /// @param poll_period_ms Polling period in milliseconds, used only when `int_pin == -1`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t begin(int miso_pin,
                    int mosi_pin,
                    int sclk_pin,
                    int cs_pin,
                    int int_pin = -1,
                    int rst_pin = -1,
                    int host = DEFAULT_HOST,
                    uint32_t clock_hz = DEFAULT_CLOCK_HZ,
                    size_t queue_size = DEFAULT_QUEUE_SIZE,
                    uint32_t poll_period_ms = DEFAULT_POLL_PERIOD_MS) const;
    /// Starts the Ethernet state machine and DHCP flow.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t start(void) const;
    /// Stops the Ethernet state machine.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t stop(void) const;
    /// Stops Ethernet, releases the SPI bus, and resets the wrapper state.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(void) const;

    /// Returns true when the wrapper is initialized and owns the Ethernet backend.
    ///
    /// @return true when `begin()` has already succeeded.
    [[nodiscard]] bool ready(void) const;
    /// Returns true when the Ethernet state machine is running.
    ///
    /// @return true when `start()` is currently active.
    [[nodiscard]] bool started(void) const;
    /// Returns true when the Ethernet link is up.
    ///
    /// @return true when the physical link is up.
    [[nodiscard]] bool connected(void) const;
    /// Returns true when the netif has one valid IPv4 address.
    ///
    /// @return true when a valid IPv4 address is assigned.
    [[nodiscard]] bool hasIp(void) const;
    /// Waits for the Ethernet link to come up.
    ///
    /// @param timeout_ms Maximum time to wait, in milliseconds.
    /// @return true when connected before the timeout elapsed.
    [[nodiscard]] bool waitConnected(uint32_t timeout_ms) const;

    /// Static network settings must be configured together before begin().
    ///
    /// @param ip Static IPv4 address as a dotted-decimal string.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t ip(const char *ip) const;
    /// Sets the static gateway. Use together with `ip()` and `subnet()`.
    ///
    /// @param gateway Gateway IPv4 address as a dotted-decimal string.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t gateway(const char *gateway) const;
    /// Sets the static subnet mask. Use together with `ip()` and `gateway()`.
    ///
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

    /// Returns the current local IPv4 as text.
    ///
    /// @return IPv4 address as text, or an empty string when not connected.
    [[nodiscard]] const char *localIP(void) const;
    /// Returns the configured hostname as text.
    ///
    /// @return Hostname as text.
    [[nodiscard]] const char *hostname(void) const;
    /// Returns the current gateway IPv4 as text.
    ///
    /// @return Gateway IPv4 address as text.
    [[nodiscard]] const char *gateway(void) const;
    /// Returns the current subnet mask as text.
    ///
    /// @return Subnet mask as text.
    [[nodiscard]] const char *subnet(void) const;
    /// Returns the configured MISO pin.
    ///
    /// @return MISO GPIO number.
    [[nodiscard]] int misoPin(void) const;
    /// Returns the configured MOSI pin.
    ///
    /// @return MOSI GPIO number.
    [[nodiscard]] int mosiPin(void) const;
    /// Returns the configured SCLK pin.
    ///
    /// @return SCLK GPIO number.
    [[nodiscard]] int sclkPin(void) const;
    /// Returns the configured CS pin.
    ///
    /// @return Chip-select GPIO number.
    [[nodiscard]] int csPin(void) const;
    /// Returns the configured interrupt pin or `-1`.
    ///
    /// @return Interrupt GPIO number, or `-1` when polling instead.
    [[nodiscard]] int intPin(void) const;
    /// Returns the configured reset pin or `-1`.
    ///
    /// @return Reset GPIO number, or `-1` when not wired.
    [[nodiscard]] int rstPin(void) const;
    /// Returns the SPI host used by the W5500 backend.
    ///
    /// @return SPI host number.
    [[nodiscard]] int host(void) const;
    /// Returns the configured SPI clock in hertz.
    ///
    /// @return SPI clock speed in Hz.
    [[nodiscard]] uint32_t clockHz(void) const;
    /// Returns the configured SPI queue size.
    ///
    /// @return SPI transaction queue depth.
    [[nodiscard]] size_t queueSize(void) const;
    /// Returns the configured polling period in milliseconds.
    ///
    /// @return Polling period in milliseconds.
    [[nodiscard]] uint32_t pollPeriodMs(void) const;
    /// Enables or disables the optional W5500 AT command set.
    ///
    /// @param enable true to register the sidecar commands, false to unregister them.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t at(bool enable = true) const;
    /// Returns true when the W5500 AT command set is registered.
    ///
    /// @return true when the sidecar commands are currently registered.
    [[nodiscard]] bool atEnabled(void) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t validateIpString(const char *value);
    static bool isValidHost(int host);
    static esp_err_t copyString(char *dst, size_t dst_len, const char *src);
    static esp_err_t applyHostname(void);
    static esp_err_t applyNetifConfig(void);
};

extern W5500 w5500;

} // namespace esp32libfun

using esp32libfun::w5500;
using esp32libfun::W5500;
