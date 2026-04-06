#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

namespace esp32libfun {

constexpr uint32_t MCPWM_PWM = 20000;
constexpr uint32_t MCPWM_SERVO = 50;

class Mcpwm {
public:
    static constexpr size_t MAX_CHANNELS = 6;
    static constexpr uint32_t DEFAULT_RESOLUTION_HZ = 1000000;

    /// Starts one simple MCPWM output on the selected pin.
    esp_err_t begin(int pin,
                    uint32_t freq_hz = MCPWM_PWM,
                    float duty_percent = 0.0f,
                    int group = 0,
                    uint32_t resolution_hz = DEFAULT_RESOLUTION_HZ) const;
    /// Stops and releases one MCPWM output.
    esp_err_t end(int pin) const;
    /// Returns true when the selected pin is already attached to MCPWM.
    [[nodiscard]] bool ready(int pin) const;

    /// Sets the PWM duty cycle in percent.
    esp_err_t duty(int pin, float percent) const;
    /// Returns the PWM duty cycle in percent.
    [[nodiscard]] float duty(int pin) const;

    /// Changes the PWM frequency while keeping the current duty ratio.
    esp_err_t freq(int pin, uint32_t hz) const;
    /// Returns the configured PWM frequency.
    [[nodiscard]] uint32_t freq(int pin) const;

    /// Sets the PWM pulse width directly in microseconds.
    esp_err_t pulse(int pin, uint32_t high_us, uint32_t period_us = 20000) const;
    /// Forces the output level immediately.
    esp_err_t force(int pin, int level, bool hold_on = false) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static int findSlotByPin(int pin);
    static int findFreeSlot(void);
};

extern Mcpwm mcpwm;

} // namespace esp32libfun

using esp32libfun::mcpwm;
using esp32libfun::MCPWM_PWM;
using esp32libfun::MCPWM_SERVO;
