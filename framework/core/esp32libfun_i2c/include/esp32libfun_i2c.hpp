#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "soc/soc_caps.h"

#define ESP32LIBFUN_I2C_VERSION "v0.1.0"
#define ESP32LIBFUN_I2C_VERSION_MAJOR 0
#define ESP32LIBFUN_I2C_VERSION_MINOR 1
#define ESP32LIBFUN_I2C_VERSION_PATCH 0

namespace esp32libfun {

constexpr int I2C_ADDR_7BIT = 7;
constexpr int I2C_ADDR_10BIT = 10;

constexpr uint32_t I2C_STANDARD = 100000;
constexpr uint32_t I2C_FAST = 400000;
constexpr uint32_t I2C_FAST_PLUS = 1000000;

/// Thin wrapper around the ESP-IDF I2C master driver.
class I2c {
public:
    static constexpr size_t MAX_BUSES = SOC_HP_I2C_NUM;
    static constexpr size_t MAX_DEVICES = 16;

    /// Initializes one I2C master bus on the selected port or acquires an existing compatible bus.
    ///
    /// @param sda_pin SDA GPIO for the bus.
    /// @param scl_pin SCL GPIO for the bus.
    /// @param speed_hz Bus clock speed in Hz (see `I2C_STANDARD`, `I2C_FAST`, `I2C_FAST_PLUS`).
    /// @param port I2C port number.
    /// @param internal_pullup When true, enables the SoC's internal SDA/SCL pull-ups.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t begin(int sda_pin, int scl_pin, uint32_t speed_hz = I2C_STANDARD, int port = 0, bool internal_pullup = true) const;
    /// Releases one I2C master bus reference. The bus is deinitialized only when no users and no devices remain.
    ///
    /// @param port I2C port to release.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(int port = 0) const;
    /// Returns true when the selected bus was already initialized.
    ///
    /// @param port I2C port to check.
    /// @return true when the port already has an initialized bus.
    [[nodiscard]] bool ready(int port = 0) const;
    /// Returns true when one device is already registered on the selected bus.
    ///
    /// @param address 7-bit or 10-bit device address.
    /// @param port I2C port to check.
    /// @return true when the device is already registered on that port.
    [[nodiscard]] bool has(uint16_t address, int port = 0) const;

    /// Registers one I2C device on a previously initialized bus or acquires an existing compatible registration.
    ///
    /// @param address 7-bit or 10-bit device address.
    /// @param port I2C port the device lives on.
    /// @param speed_hz Per-device clock speed in Hz, or `0` to reuse the bus speed.
    /// @param addr_bits `I2C_ADDR_7BIT` or `I2C_ADDR_10BIT`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t add(uint16_t address, int port = 0, uint32_t speed_hz = 0, int addr_bits = I2C_ADDR_7BIT) const;
    /// Releases one I2C device registration from the selected bus.
    ///
    /// @param address Device address to remove.
    /// @param port I2C port the device lives on.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t remove(uint16_t address, int port = 0) const;

    /// Probes one address on the selected bus.
    ///
    /// @param address Device address to probe.
    /// @param port I2C port to probe on.
    /// @param timeout_ms Maximum time to wait for the probe, in milliseconds.
    /// @return `ESP_OK` when a device acknowledges the address, or an `esp_err_t` describing the failure.
    esp_err_t probe(uint16_t address, int port = 0, int timeout_ms = 100) const;
    /// Writes raw bytes to one registered device.
    ///
    /// @param address Target device address.
    /// @param data Bytes to write.
    /// @param len Number of bytes in `data`.
    /// @param port I2C port the device lives on.
    /// @param timeout_ms Maximum time to wait, in milliseconds; `-1` waits forever.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t write(uint16_t address, const uint8_t *data, size_t len, int port = 0, int timeout_ms = -1) const;
    /// Reads raw bytes from one registered device.
    ///
    /// @param address Target device address.
    /// @param data Buffer that receives the read bytes.
    /// @param len Number of bytes to read into `data`.
    /// @param port I2C port the device lives on.
    /// @param timeout_ms Maximum time to wait, in milliseconds; `-1` waits forever.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t read(uint16_t address, uint8_t *data, size_t len, int port = 0, int timeout_ms = -1) const;
    /// Writes bytes then reads bytes from one registered device.
    ///
    /// @param address Target device address.
    /// @param write_data Bytes to write first.
    /// @param write_len Number of bytes in `write_data`.
    /// @param read_data Buffer that receives the read bytes.
    /// @param read_len Number of bytes to read into `read_data`.
    /// @param port I2C port the device lives on.
    /// @param timeout_ms Maximum time to wait, in milliseconds; `-1` waits forever.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t writeRead(uint16_t address, const uint8_t *write_data, size_t write_len, uint8_t *read_data, size_t read_len, int port = 0, int timeout_ms = -1) const;
    /// Writes one register address then sends a payload without copying it into a temporary buffer.
    ///
    /// @param address Target device address.
    /// @param reg Register address to write to.
    /// @param data Payload bytes to write after the register address.
    /// @param len Number of bytes in `data`.
    /// @param port I2C port the device lives on.
    /// @param timeout_ms Maximum time to wait, in milliseconds; `-1` waits forever.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t regWrite(uint16_t address, uint8_t reg, const uint8_t *data, size_t len, int port = 0, int timeout_ms = -1) const;
    /// Writes one single byte to one register.
    ///
    /// @param address Target device address.
    /// @param reg Register address to write to.
    /// @param value Byte value to write.
    /// @param port I2C port the device lives on.
    /// @param timeout_ms Maximum time to wait, in milliseconds; `-1` waits forever.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t regWrite8(uint16_t address, uint8_t reg, uint8_t value, int port = 0, int timeout_ms = -1) const;
    /// Writes one register address then reads one payload back.
    ///
    /// @param address Target device address.
    /// @param reg Register address to read from.
    /// @param data Buffer that receives the read bytes.
    /// @param len Number of bytes to read into `data`.
    /// @param port I2C port the device lives on.
    /// @param timeout_ms Maximum time to wait, in milliseconds; `-1` waits forever.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t regRead(uint16_t address, uint8_t reg, uint8_t *data, size_t len, int port = 0, int timeout_ms = -1) const;
    /// Reads one single byte from one register.
    ///
    /// @param address Target device address.
    /// @param reg Register address to read from.
    /// @param value Receives the read byte.
    /// @param port I2C port the device lives on.
    /// @param timeout_ms Maximum time to wait, in milliseconds; `-1` waits forever.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t regRead8(uint16_t address, uint8_t reg, uint8_t *value, int port = 0, int timeout_ms = -1) const;

    /// Resets the selected bus in place.
    ///
    /// @param port I2C port to reset.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t reset(int port = 0) const;
    /// Enables or disables the optional I2C AT command set.
    ///
    /// When enabled, this registers helpers such as `AT+I2C=SCAN` and
    /// `AT+I2CBUS=<sda>,<scl>`.
    ///
    /// @param enable true to register the sidecar commands, false to unregister them.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t at(bool enable = true) const;
    /// Returns true when the I2C AT command set is registered.
    ///
    /// @return true when the sidecar commands are currently registered.
    [[nodiscard]] bool atEnabled(void) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static bool isValidPort(int port);
    static bool isValidAddress(uint16_t address, int addr_bits);
    static int findDeviceIndex(uint16_t address, int port);
};

/// Global I2C convenience object.
extern I2c i2c;

} // namespace esp32libfun

using esp32libfun::i2c;
using esp32libfun::I2C_ADDR_7BIT;
using esp32libfun::I2C_ADDR_10BIT;
using esp32libfun::I2C_STANDARD;
using esp32libfun::I2C_FAST;
using esp32libfun::I2C_FAST_PLUS;
