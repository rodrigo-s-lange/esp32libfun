#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "soc/soc_caps.h"

#include "../../esp32libfun_spi/include/esp32libfun_spi.hpp"

namespace esp32libfun {

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
    esp_err_t start(void) const;
    /// Stops the Ethernet state machine.
    esp_err_t stop(void) const;
    /// Stops Ethernet, releases the SPI bus, and resets the wrapper state.
    esp_err_t end(void) const;

    [[nodiscard]] bool ready(void) const;
    [[nodiscard]] bool started(void) const;
    [[nodiscard]] bool connected(void) const;
    [[nodiscard]] bool hasIp(void) const;
    [[nodiscard]] bool waitConnected(uint32_t timeout_ms) const;

    /// Restarts DHCP on the Ethernet netif.
    esp_err_t renew(void) const;
    /// Sets the Ethernet hostname. Call before begin() for the cleanest path.
    esp_err_t hostname(const char *value) const;
    /// Copies the current MAC address into `out[6]`.
    esp_err_t mac(uint8_t out[6]) const;

    [[nodiscard]] const char *localIP(void) const;
    [[nodiscard]] const char *gateway(void) const;
    [[nodiscard]] const char *subnet(void) const;
    [[nodiscard]] int host(void) const;
    [[nodiscard]] uint32_t clockHz(void) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static bool isValidHost(int host);
    static esp_err_t copyString(char *dst, size_t dst_len, const char *src);
};

extern W5500 w5500;

} // namespace esp32libfun

using esp32libfun::w5500;
using esp32libfun::W5500;
