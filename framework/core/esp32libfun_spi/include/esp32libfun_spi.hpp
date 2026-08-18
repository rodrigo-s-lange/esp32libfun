#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "soc/soc_caps.h"

#define ESP32LIBFUN_SPI_VERSION "v0.1.2"
#define ESP32LIBFUN_SPI_VERSION_MAJOR 0
#define ESP32LIBFUN_SPI_VERSION_MINOR 1
#define ESP32LIBFUN_SPI_VERSION_PATCH 2

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

/// Thin wrapper around the ESP-IDF SPI master driver.
class Spi {
public:
    static constexpr size_t MAX_DEVICES = 16;
    static constexpr size_t DEFAULT_MAX_TRANSFER = 4096;

    /// Initializes one SPI master bus on the selected host or acquires an existing compatible bus.
    ///
    /// @param sclk_pin SCLK GPIO for the bus.
    /// @param mosi_pin MOSI GPIO for the bus.
    /// @param miso_pin MISO GPIO for the bus, or `-1` when the bus is write-only.
    /// @param port SPI host to use (see `SPI_HOST_DEFAULT`, `SPI_HOST_2`, `SPI_HOST_3`).
    /// @param max_transfer_sz Maximum single-transfer size, in bytes, the bus should support.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t begin(int sclk_pin,
                    int mosi_pin,
                    int miso_pin = -1,
                    int port = SPI_HOST_DEFAULT,
                    size_t max_transfer_sz = DEFAULT_MAX_TRANSFER) const;
    /// Releases one SPI master bus reference. The bus is deinitialized only when no users and no devices remain.
    ///
    /// @param port SPI host to release.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(int port = SPI_HOST_DEFAULT) const;
    /// Returns true when the selected SPI bus is already initialized.
    ///
    /// @param port SPI host to check.
    /// @return true when the host already has an initialized bus.
    [[nodiscard]] bool ready(int port = SPI_HOST_DEFAULT) const;
    /// Returns true when one device is already registered on the selected SPI bus.
    ///
    /// @param cs_pin Chip-select GPIO identifying the device.
    /// @param port SPI host the device lives on.
    /// @return true when the device is already registered on that host.
    [[nodiscard]] bool has(int cs_pin, int port = SPI_HOST_DEFAULT) const;

    /// Registers one SPI device on a previously initialized bus or acquires an existing compatible registration.
    ///
    /// @param cs_pin Chip-select GPIO for the device.
    /// @param clock_hz SPI clock speed in Hz for this device.
    /// @param mode One of `SPI_MODE_0`..`SPI_MODE_3`.
    /// @param port SPI host the device lives on.
    /// @param queue_size Number of transactions the device can have queued.
    /// @param flags Raw ESP-IDF `spi_device_interface_config_t::flags` value.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t add(int cs_pin,
                  uint32_t clock_hz,
                  int mode = SPI_MODE_0,
                  int port = SPI_HOST_DEFAULT,
                  size_t queue_size = 1,
                  uint32_t flags = 0) const;
    /// Releases one SPI device registration from the selected bus.
    ///
    /// @param cs_pin Chip-select GPIO identifying the device.
    /// @param port SPI host the device lives on.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t remove(int cs_pin, int port = SPI_HOST_DEFAULT) const;

    /// Executes one generic SPI transfer on one registered device.
    ///
    /// @param cs_pin Chip-select GPIO identifying the device.
    /// @param tx_data Bytes to send, or `nullptr` to send zeros.
    /// @param rx_data Buffer that receives the read bytes, or `nullptr` to discard them.
    /// @param len Number of bytes to transfer.
    /// @param port SPI host the device lives on.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t transfer(int cs_pin,
                       const uint8_t *tx_data,
                       uint8_t *rx_data,
                       size_t len,
                       int port = SPI_HOST_DEFAULT) const;
    /// Writes raw bytes to one registered device.
    ///
    /// @param cs_pin Chip-select GPIO identifying the device.
    /// @param data Bytes to write.
    /// @param len Number of bytes in `data`.
    /// @param port SPI host the device lives on.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t write(int cs_pin, const uint8_t *data, size_t len, int port = SPI_HOST_DEFAULT) const;
    /// Reads raw bytes from one registered device while clocking out zeros.
    ///
    /// @param cs_pin Chip-select GPIO identifying the device.
    /// @param data Buffer that receives the read bytes.
    /// @param len Number of bytes to read into `data`.
    /// @param port SPI host the device lives on.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t read(int cs_pin, uint8_t *data, size_t len, int port = SPI_HOST_DEFAULT) const;
    /// Writes one command byte only.
    ///
    /// @param cs_pin Chip-select GPIO identifying the device.
    /// @param value Command byte to write.
    /// @param port SPI host the device lives on.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t cmd(int cs_pin, uint8_t value, int port = SPI_HOST_DEFAULT) const;
    /// Writes one register/command byte then one payload while keeping CS asserted.
    ///
    /// @param cs_pin Chip-select GPIO identifying the device.
    /// @param reg Register/command byte to write first.
    /// @param data Payload bytes to write after `reg`.
    /// @param len Number of bytes in `data`.
    /// @param port SPI host the device lives on.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t regWrite(int cs_pin, uint8_t reg, const uint8_t *data, size_t len, int port = SPI_HOST_DEFAULT) const;
    /// Writes one single byte to one register/command.
    ///
    /// @param cs_pin Chip-select GPIO identifying the device.
    /// @param reg Register/command byte to write first.
    /// @param value Byte value to write after `reg`.
    /// @param port SPI host the device lives on.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t regWrite8(int cs_pin, uint8_t reg, uint8_t value, int port = SPI_HOST_DEFAULT) const;
    /// Writes one register/command byte then reads one payload back while keeping CS asserted.
    ///
    /// @param cs_pin Chip-select GPIO identifying the device.
    /// @param reg Register/command byte to write first.
    /// @param data Buffer that receives the read bytes.
    /// @param len Number of bytes to read into `data`.
    /// @param port SPI host the device lives on.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t regRead(int cs_pin, uint8_t reg, uint8_t *data, size_t len, int port = SPI_HOST_DEFAULT) const;
    /// Reads one single byte from one register/command.
    ///
    /// @param cs_pin Chip-select GPIO identifying the device.
    /// @param reg Register/command byte to write first.
    /// @param value Receives the read byte.
    /// @param port SPI host the device lives on.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t regRead8(int cs_pin, uint8_t reg, uint8_t *value, int port = SPI_HOST_DEFAULT) const;
    /// Enables or disables the optional SPI AT command set.
    ///
    /// When enabled, this registers helpers such as `AT+SPIBUS=<...>` and
    /// `AT+SPICMD=<cs>,<value>`.
    ///
    /// @param enable true to register the sidecar commands, false to unregister them.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t at(bool enable = true) const;
    /// Returns true when the SPI AT command set is registered.
    ///
    /// @return true when the sidecar commands are currently registered.
    [[nodiscard]] bool atEnabled(void) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static bool isValidHost(int port);
    static int hostIndex(int port);
    static int findDeviceIndex(int cs_pin, int port);
};

/// Global SPI convenience object.
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
