#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ESP32LIBFUN_MCPWM_VERSION "v0.2.0"
#define ESP32LIBFUN_MCPWM_VERSION_MAJOR 0
#define ESP32LIBFUN_MCPWM_VERSION_MINOR 2
#define ESP32LIBFUN_MCPWM_VERSION_PATCH 0

namespace esp32libfun {

constexpr uint32_t MCPWM_PWM = 20000;
constexpr uint32_t MCPWM_SERVO = 50;

class Mcpwm {
public:
    static constexpr size_t MAX_CHANNELS = 6;
    static constexpr uint32_t DEFAULT_RESOLUTION_HZ = 1000000;

    /// Starts one simple MCPWM output on the selected pin.
    ///
    /// @param pin GPIO to drive with PWM.
    /// @param freq_hz PWM frequency in Hz (see `MCPWM_PWM`, `MCPWM_SERVO`).
    /// @param duty_percent Initial duty cycle in percent, from `0.0` to `100.0`.
    /// @param group MCPWM hardware group to use.
    /// @param resolution_hz Timer tick resolution in Hz.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t begin(int pin,
                    uint32_t freq_hz = MCPWM_PWM,
                    float duty_percent = 0.0f,
                    int group = 0,
                    uint32_t resolution_hz = DEFAULT_RESOLUTION_HZ) const;
    /// Starts one complementary MCPWM pair on the selected pins.
    ///
    /// The high pin outputs the base PWM waveform. The low pin outputs the
    /// complementary waveform and can later receive hardware deadtime.
    ///
    /// @param pin_high GPIO for the high-side (base) output.
    /// @param pin_low GPIO for the low-side (complementary) output.
    /// @param freq_hz PWM frequency in Hz.
    /// @param duty_percent Initial duty cycle in percent, from `0.0` to `100.0`.
    /// @param group MCPWM hardware group to use.
    /// @param resolution_hz Timer tick resolution in Hz.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t beginComplementary(int pin_high,
                                 int pin_low,
                                 uint32_t freq_hz = MCPWM_PWM,
                                 float duty_percent = 0.0f,
                                 int group = 0,
                                 uint32_t resolution_hz = DEFAULT_RESOLUTION_HZ) const;
    /// Stops and releases one MCPWM output.
    ///
    /// If the selected pin belongs to a complementary pair, the whole pair is
    /// released.
    ///
    /// @param pin Pin (or either pin of a complementary pair) to release.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(int pin) const;
    /// Returns true when the selected pin is already attached to MCPWM.
    ///
    /// @param pin Pin to check.
    /// @return true when the pin already owns an MCPWM output.
    [[nodiscard]] bool ready(int pin) const;
    /// Returns true when the selected pin belongs to a complementary pair.
    ///
    /// @param pin Pin to check.
    /// @return true when the pin is part of a complementary pair.
    [[nodiscard]] bool complementary(int pin) const;

    /// Sets the PWM duty cycle in percent.
    ///
    /// @param pin Pin (or either pin of a complementary pair) to update.
    /// @param percent Duty cycle in percent, from `0.0` to `100.0`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t duty(int pin, float percent) const;
    /// Returns the PWM duty cycle in percent.
    ///
    /// @param pin Pin to query.
    /// @return Current duty cycle in percent.
    [[nodiscard]] float duty(int pin) const;

    /// Changes the PWM frequency while keeping the current duty ratio.
    ///
    /// @param pin Pin (or either pin of a complementary pair) to update.
    /// @param hz New PWM frequency in Hz.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t freq(int pin, uint32_t hz) const;
    /// Returns the configured PWM frequency.
    ///
    /// @param pin Pin to query.
    /// @return Configured PWM frequency in Hz.
    [[nodiscard]] uint32_t freq(int pin) const;

    /// Applies symmetric hardware deadtime to one complementary pair.
    ///
    /// The selected pin can be either member of the pair. The high-side output
    /// gets rising-edge delay, and the low-side output gets falling-edge delay
    /// plus inversion, which yields an active-high complementary pair.
    ///
    /// @param pin Either pin of the complementary pair.
    /// @param deadtime_us Deadtime in microseconds.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t deadtime(int pin, uint32_t deadtime_us) const;
    /// Returns the configured deadtime in microseconds for one complementary pair.
    ///
    /// @param pin Either pin of the complementary pair.
    /// @return Configured deadtime in microseconds.
    [[nodiscard]] uint32_t deadtime(int pin) const;

    /// Sets the PWM pulse width directly in microseconds.
    ///
    /// @param pin Pin (or either pin of a complementary pair) to update.
    /// @param high_us High time in microseconds.
    /// @param period_us Full PWM period in microseconds.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t pulse(int pin, uint32_t high_us, uint32_t period_us = 20000) const;
    /// Forces the output level immediately.
    ///
    /// On complementary pairs, this method uses final output levels for both
    /// pins even when the low side is inverted by the deadtime block.
    ///
    /// @param pin Pin (or either pin of a complementary pair) to force.
    /// @param level Logical level to force: `0` for low, non-zero for high.
    /// @param hold_on When true, keeps the forced level active instead of a one-shot pulse.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t force(int pin, int level, bool hold_on = false) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static int findSlotByPin(int pin);
    static int findFreeSlot(void);
};

/// Global MCPWM convenience object.
extern Mcpwm mcpwm;

} // namespace esp32libfun

using esp32libfun::mcpwm;
using esp32libfun::MCPWM_PWM;
using esp32libfun::MCPWM_SERVO;
