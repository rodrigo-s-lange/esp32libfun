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
    esp_err_t begin(int pin,
                    uint32_t freq_hz = LEDC_PWM,
                    uint8_t resolution_bits = DEFAULT_RESOLUTION_BITS,
                    int channel = -1,
                    bool invert = false) const;
    /// Stops and releases one LEDC PWM output.
    esp_err_t end(int pin) const;
    /// Returns true when the selected pin is already attached to LEDC.
    [[nodiscard]] bool ready(int pin) const;

    /// Sets the raw duty value for one LEDC output.
    esp_err_t duty(int pin, uint32_t value) const;
    /// Returns the current raw duty value.
    [[nodiscard]] uint32_t duty(int pin) const;
    /// Sets the duty in percent for one LEDC output.
    esp_err_t percent(int pin, float value) const;

    /// Changes the PWM frequency of one LEDC output.
    esp_err_t freq(int pin, uint32_t hz) const;
    /// Returns the configured PWM frequency.
    [[nodiscard]] uint32_t freq(int pin) const;
    /// Returns the configured duty resolution in bits.
    [[nodiscard]] uint8_t resolution(int pin) const;
    /// Returns the maximum raw duty value for the selected output.
    [[nodiscard]] uint32_t maxDuty(int pin) const;

    /// Starts a simple fade to one target duty.
    esp_err_t fade(int pin, uint32_t target_duty, uint32_t time_ms, bool wait_done = false) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t ensureFadeSupport(void);
    static int findChannelByPin(int pin);
    static int findFreeChannel(void);
    static int findCompatibleTimer(uint32_t freq_hz, uint8_t resolution_bits);
    static int findFreeTimer(void);
};

extern Ledc ledc;

} // namespace esp32libfun

using esp32libfun::ledc;
using esp32libfun::LEDC_PWM;
using esp32libfun::LEDC_AUDIO;
using esp32libfun::LEDC_SERVO;
