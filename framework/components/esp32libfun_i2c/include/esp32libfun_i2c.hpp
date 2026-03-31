#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "soc/soc_caps.h"

namespace esp32libfun {

constexpr int I2C_ADDR_7BIT = 7;
constexpr int I2C_ADDR_10BIT = 10;

constexpr uint32_t I2C_STANDARD = 100000;
constexpr uint32_t I2C_FAST = 400000;
constexpr uint32_t I2C_FAST_PLUS = 1000000;

class I2c {
public:
    static constexpr size_t MAX_BUSES = SOC_HP_I2C_NUM;
    static constexpr size_t MAX_DEVICES = 16;

    /// Initializes one I2C master bus on the selected port or acquires an existing compatible bus.
    esp_err_t begin(int sda_pin, int scl_pin, uint32_t speed_hz = I2C_STANDARD, int port = 0, bool internal_pullup = true) const;
    /// Releases one I2C master bus reference. The bus is deinitialized only when no users and no devices remain.
    esp_err_t end(int port = 0) const;
    /// Returns true when the selected bus was already initialized.
    [[nodiscard]] bool ready(int port = 0) const;
    /// Returns true when one device is already registered on the selected bus.
    [[nodiscard]] bool has(uint16_t address, int port = 0) const;

    /// Registers one I2C device on a previously initialized bus or acquires an existing compatible registration.
    esp_err_t add(uint16_t address, int port = 0, uint32_t speed_hz = 0, int addr_bits = I2C_ADDR_7BIT) const;
    /// Releases one I2C device registration from the selected bus.
    esp_err_t remove(uint16_t address, int port = 0) const;

    /// Probes one address on the selected bus.
    esp_err_t probe(uint16_t address, int port = 0, int timeout_ms = 100) const;
    /// Writes raw bytes to one registered device.
    esp_err_t write(uint16_t address, const uint8_t *data, size_t len, int port = 0, int timeout_ms = -1) const;
    /// Reads raw bytes from one registered device.
    esp_err_t read(uint16_t address, uint8_t *data, size_t len, int port = 0, int timeout_ms = -1) const;
    /// Writes bytes then reads bytes from one registered device.
    esp_err_t writeRead(uint16_t address, const uint8_t *write_data, size_t write_len, uint8_t *read_data, size_t read_len, int port = 0, int timeout_ms = -1) const;
    /// Writes one register address then sends a payload without copying it into a temporary buffer.
    esp_err_t regWrite(uint16_t address, uint8_t reg, const uint8_t *data, size_t len, int port = 0, int timeout_ms = -1) const;
    /// Writes one single byte to one register.
    esp_err_t regWrite8(uint16_t address, uint8_t reg, uint8_t value, int port = 0, int timeout_ms = -1) const;
    /// Writes one register address then reads one payload back.
    esp_err_t regRead(uint16_t address, uint8_t reg, uint8_t *data, size_t len, int port = 0, int timeout_ms = -1) const;
    /// Reads one single byte from one register.
    esp_err_t regRead8(uint16_t address, uint8_t reg, uint8_t *value, int port = 0, int timeout_ms = -1) const;

    /// Resets the selected bus in place.
    esp_err_t reset(int port = 0) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static bool isValidPort(int port);
    static bool isValidAddress(uint16_t address, int addr_bits);
    static int findDeviceIndex(uint16_t address, int port);
};

extern I2c i2c;

} // namespace esp32libfun

using esp32libfun::i2c;
using esp32libfun::I2C_ADDR_7BIT;
using esp32libfun::I2C_ADDR_10BIT;
using esp32libfun::I2C_STANDARD;
using esp32libfun::I2C_FAST;
using esp32libfun::I2C_FAST_PLUS;
