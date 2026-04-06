#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "soc/soc_caps.h"

namespace esp32libfun {

constexpr uint32_t SPI_SLOW = 1000000;
constexpr uint32_t SPI_FAST = 10000000;
constexpr uint32_t SPI_DISPLAY = 40000000;

constexpr int SPI_MODE_0 = 0;
constexpr int SPI_MODE_1 = 1;
constexpr int SPI_MODE_2 = 2;
constexpr int SPI_MODE_3 = 3;

constexpr int SPI_HOST_DEFAULT = 1;
constexpr int SPI_HOST_2 = 1;
#if SOC_SPI_PERIPH_NUM > 2
constexpr int SPI_HOST_3 = 2;
#endif

class Spi {
public:
    static constexpr size_t MAX_DEVICES = 16;
    static constexpr size_t DEFAULT_MAX_TRANSFER = 4096;

    /// Initializes one SPI master bus on the selected host or acquires an existing compatible bus.
    esp_err_t begin(int sclk_pin,
                    int mosi_pin,
                    int miso_pin = -1,
                    int port = SPI_HOST_DEFAULT,
                    size_t max_transfer_sz = DEFAULT_MAX_TRANSFER) const;
    /// Releases one SPI master bus reference. The bus is deinitialized only when no users and no devices remain.
    esp_err_t end(int port = SPI_HOST_DEFAULT) const;
    /// Returns true when the selected SPI bus is already initialized.
    [[nodiscard]] bool ready(int port = SPI_HOST_DEFAULT) const;
    /// Returns true when one device is already registered on the selected SPI bus.
    [[nodiscard]] bool has(int cs_pin, int port = SPI_HOST_DEFAULT) const;

    /// Registers one SPI device on a previously initialized bus or acquires an existing compatible registration.
    esp_err_t add(int cs_pin,
                  uint32_t clock_hz,
                  int mode = SPI_MODE_0,
                  int port = SPI_HOST_DEFAULT,
                  size_t queue_size = 1,
                  uint32_t flags = 0) const;
    /// Releases one SPI device registration from the selected bus.
    esp_err_t remove(int cs_pin, int port = SPI_HOST_DEFAULT) const;

    /// Executes one generic SPI transfer on one registered device.
    esp_err_t transfer(int cs_pin,
                       const uint8_t *tx_data,
                       uint8_t *rx_data,
                       size_t len,
                       int port = SPI_HOST_DEFAULT) const;
    /// Writes raw bytes to one registered device.
    esp_err_t write(int cs_pin, const uint8_t *data, size_t len, int port = SPI_HOST_DEFAULT) const;
    /// Reads raw bytes from one registered device while clocking out zeros.
    esp_err_t read(int cs_pin, uint8_t *data, size_t len, int port = SPI_HOST_DEFAULT) const;
    /// Writes one command byte only.
    esp_err_t cmd(int cs_pin, uint8_t value, int port = SPI_HOST_DEFAULT) const;
    /// Writes one register/command byte then one payload while keeping CS asserted.
    esp_err_t regWrite(int cs_pin, uint8_t reg, const uint8_t *data, size_t len, int port = SPI_HOST_DEFAULT) const;
    /// Writes one single byte to one register/command.
    esp_err_t regWrite8(int cs_pin, uint8_t reg, uint8_t value, int port = SPI_HOST_DEFAULT) const;
    /// Writes one register/command byte then reads one payload back while keeping CS asserted.
    esp_err_t regRead(int cs_pin, uint8_t reg, uint8_t *data, size_t len, int port = SPI_HOST_DEFAULT) const;
    /// Reads one single byte from one register/command.
    esp_err_t regRead8(int cs_pin, uint8_t reg, uint8_t *value, int port = SPI_HOST_DEFAULT) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static bool isValidHost(int port);
    static int hostIndex(int port);
    static int findDeviceIndex(int cs_pin, int port);
};

extern Spi spi;

} // namespace esp32libfun

using esp32libfun::spi;
using esp32libfun::SPI_SLOW;
using esp32libfun::SPI_FAST;
using esp32libfun::SPI_DISPLAY;
using esp32libfun::SPI_MODE_0;
using esp32libfun::SPI_MODE_1;
using esp32libfun::SPI_MODE_2;
using esp32libfun::SPI_MODE_3;
using esp32libfun::SPI_HOST_DEFAULT;
using esp32libfun::SPI_HOST_2;
#if SOC_SPI_PERIPH_NUM > 2
using esp32libfun::SPI_HOST_3;
#endif
