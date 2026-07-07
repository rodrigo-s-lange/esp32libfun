#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "soc/soc_caps.h"

namespace esp32libfun {

constexpr uint32_t LEDC_PWM = 5000;
constexpr uint32_t LEDC_AUDIO = 20000;
constexpr uint32_t LEDC_SERVO = 50;

class Ledc {
public:
    static constexpr size_t MAX_CHANNELS = SOC_LEDC_CHANNEL_NUM;
    static constexpr uint8_t DEFAULT_RESOLUTION_BITS = 10;

    /// Starts one LEDC PWM output on the selected pin.
    ///
    /// @param pin GPIO to drive with PWM.
    /// @param freq_hz PWM frequency in Hz (see `LEDC_PWM`, `LEDC_AUDIO`, `LEDC_SERVO`).
    /// @param resolution_bits Duty resolution in bits.
    /// @param channel LEDC channel to use, or `-1` to pick the first free one.
    /// @param invert When true, inverts the output polarity.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t begin(int pin,
                    uint32_t freq_hz = LEDC_PWM,
                    uint8_t resolution_bits = DEFAULT_RESOLUTION_BITS,
                    int channel = -1,
                    bool invert = false) const;
    /// Stops and releases one LEDC PWM output.
    ///
    /// @param pin GPIO whose PWM output should be released.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(int pin) const;
    /// Returns true when the selected pin is already attached to LEDC.
    ///
    /// @param pin Pin to check.
    /// @return true when the pin already owns a LEDC channel.
    [[nodiscard]] bool ready(int pin) const;

    /// Sets the raw duty value for one LEDC output.
    ///
    /// @param pin Pin whose duty should change.
    /// @param value Raw duty value, up to `maxDuty(pin)`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t duty(int pin, uint32_t value) const;
    /// Returns the current raw duty value.
    ///
    /// @param pin Pin to query.
    /// @return Current raw duty value.
    [[nodiscard]] uint32_t duty(int pin) const;
    /// Sets the duty in percent for one LEDC output.
    ///
    /// @param pin Pin whose duty should change.
    /// @param value Duty as a percentage from `0.0` to `100.0`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t percent(int pin, float value) const;

    /// Changes the PWM frequency of one LEDC output.
    ///
    /// @param pin Pin whose frequency should change.
    /// @param hz New PWM frequency in Hz.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t freq(int pin, uint32_t hz) const;
    /// Returns the configured PWM frequency.
    ///
    /// @param pin Pin to query.
    /// @return Configured PWM frequency in Hz.
    [[nodiscard]] uint32_t freq(int pin) const;
    /// Returns the configured duty resolution in bits.
    ///
    /// @param pin Pin to query.
    /// @return Duty resolution in bits.
    [[nodiscard]] uint8_t resolution(int pin) const;
    /// Returns the maximum raw duty value for the selected output.
    ///
    /// @param pin Pin to query.
    /// @return Maximum raw duty value accepted by `duty(pin, value)`.
    [[nodiscard]] uint32_t maxDuty(int pin) const;

    /// Starts a simple fade to one target duty.
    ///
    /// @param pin Pin to fade.
    /// @param target_duty Raw duty value to fade towards.
    /// @param time_ms Fade duration in milliseconds.
    /// @param wait_done When true, blocks until the fade finishes.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t fade(int pin, uint32_t target_duty, uint32_t time_ms, bool wait_done = false) const;
    /// Enables or disables the optional LEDC AT command set.
    ///
    /// When enabled, this registers helpers such as `AT+LEDCCFG=<...>` and
    /// `AT+LEDC=<pin>,<percent>`.
    ///
    /// @param enable true to register the sidecar commands, false to unregister them.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t at(bool enable = true) const;
    /// Returns true when the LEDC AT command set is registered.
    ///
    /// @return true when the sidecar commands are currently registered.
    [[nodiscard]] bool atEnabled(void) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t ensureFadeSupport(void);
    static int findChannelByPin(int pin);
    static int findFreeChannel(void);
    static int findCompatibleTimer(uint32_t freq_hz, uint8_t resolution_bits);
    static int findFreeTimer(void);
};

/// Global LEDC convenience object.
extern Ledc ledc;

} // namespace esp32libfun

using esp32libfun::ledc;
using esp32libfun::LEDC_PWM;
using esp32libfun::LEDC_AUDIO;
using esp32libfun::LEDC_SERVO;
