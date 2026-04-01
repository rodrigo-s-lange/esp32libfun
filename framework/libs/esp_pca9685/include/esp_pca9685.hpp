#pragma once

#include <stdint.h>

#include "esp_err.h"

namespace esp_pca9685 {

class Pca9685 {
public:
    static constexpr uint16_t DEFAULT_ADDRESS = 0x40;
    static constexpr uint16_t DEFAULT_PWM_HZ = 1000;
    static constexpr uint16_t MIN_PWM_HZ = 24;
    static constexpr uint16_t MAX_PWM_HZ = 1526;
    static constexpr uint8_t CHANNELS = 16;

    /// Configures one PCA9685 device already reachable through `esp32libfun_i2c`.
    ///
    /// This library is direct and synchronous, so it uses `init()` / `end()`
    /// and does not expose `start()` / `stop()`.
    ///
    /// @param address I2C address of the PCA9685.
    /// @param port I2C bus index already initialized in `esp32libfun_i2c`.
    /// @param frequency_hz PWM base frequency for all channels.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t init(uint16_t address = DEFAULT_ADDRESS, int port = 0, uint16_t frequency_hz = DEFAULT_PWM_HZ);
    /// Compatibility alias for older examples.
    inline esp_err_t begin(uint16_t address = DEFAULT_ADDRESS, int port = 0, uint16_t frequency_hz = DEFAULT_PWM_HZ)
    {
        return init(address, port, frequency_hz);
    }
    /// Releases the registered I2C device reference.
    esp_err_t end(void);
    /// Returns true when the instance is attached to a registered PCA9685 device.
    [[nodiscard]] bool ready(void) const;

    /// Changes the PWM base frequency for all channels.
    esp_err_t freq(uint16_t frequency_hz);
    /// Writes one raw PWM frame to one channel.
    esp_err_t pwm(uint8_t channel, uint16_t on_count, uint16_t off_count, bool full_on = false, bool full_off = false) const;
    /// Writes one duty cycle from 0 to 100 percent.
    esp_err_t duty(uint8_t channel, uint8_t percent) const;
    /// Forces one channel fully on.
    esp_err_t on(uint8_t channel) const;
    /// Forces one channel fully off.
    esp_err_t off(uint8_t channel) const;
    /// Reads back one channel PWM frame.
    esp_err_t read(uint8_t channel, uint16_t *on_count, uint16_t *off_count, bool *full_on = nullptr, bool *full_off = nullptr) const;

    /// Reads MODE1.
    esp_err_t mode1(uint8_t *value) const;
    /// Reads MODE2.
    esp_err_t mode2(uint8_t *value) const;
    /// Reads PRE_SCALE.
    esp_err_t prescale(uint8_t *value) const;

    [[nodiscard]] uint16_t address(void) const;
    [[nodiscard]] int port(void) const;
    [[nodiscard]] uint16_t frequency(void) const;
    /// Converts one duty percentage into 12-bit PWM counts.
    [[nodiscard]] static uint16_t dutyCount(uint8_t percent);

private:
    static bool isValidChannel(uint8_t channel);
    static bool isValidFrequency(uint16_t frequency_hz);
    static uint8_t ledBaseRegister(uint8_t channel);
    static uint8_t computePrescale(uint16_t frequency_hz);
    esp_err_t ensureReady(void) const;

    bool initialized_ = false;
    uint16_t address_ = DEFAULT_ADDRESS;
    int port_ = 0;
    uint16_t frequency_hz_ = DEFAULT_PWM_HZ;
};

} // namespace esp_pca9685

using esp_pca9685::Pca9685;
