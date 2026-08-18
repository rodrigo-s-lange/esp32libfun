#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hal/adc_types.h"

#define ESP32LIBFUN_ADC_VERSION "v0.1.2"
#define ESP32LIBFUN_ADC_VERSION_MAJOR 0
#define ESP32LIBFUN_ADC_VERSION_MINOR 1
#define ESP32LIBFUN_ADC_VERSION_PATCH 2

namespace esp32libfun {

class Adc {
public:
    static constexpr size_t MAX_CHANNELS = 16;

    /// Configures one ADC pad for oneshot reads.
    ///
    /// Calibration is attempted when `calibrate` is true. If the target or
    /// eFuse data cannot provide calibration, `begin()` still succeeds and
    /// `voltage()` later returns `ESP_ERR_NOT_SUPPORTED`.
    ///
    /// @param pin GPIO with ADC capability.
    /// @param atten ADC attenuation, usually `ADC_ATTEN_DB_12` for the widest input range.
    /// @param bitwidth ADC bit width, usually `ADC_BITWIDTH_DEFAULT`.
    /// @param calibrate When true, attempts to create a calibration handle for millivolt reads.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t begin(int pin,
                    adc_atten_t atten = ADC_ATTEN_DB_12,
                    adc_bitwidth_t bitwidth = ADC_BITWIDTH_DEFAULT,
                    bool calibrate = true) const;
    /// Releases one configured ADC pad.
    ///
    /// @param pin GPIO whose ADC configuration should be released.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(int pin) const;
    /// Returns true when the selected GPIO is configured for ADC reads.
    ///
    /// @param pin GPIO to check.
    /// @return true when the pin already owns an ADC slot.
    [[nodiscard]] bool ready(int pin) const;

    /// Reads the raw ADC conversion result.
    ///
    /// @param pin GPIO to read.
    /// @param value Receives the raw ADC conversion result.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t read(int pin, int *value) const;
    /// Reads the raw ADC conversion result.
    ///
    /// @param pin GPIO to read.
    /// @return Raw ADC value, or `0` on failure.
    [[nodiscard]] int read(int pin) const;

    /// Reads the calibrated ADC voltage in millivolts.
    ///
    /// @param pin GPIO to read.
    /// @param millivolts Receives the calibrated voltage in mV.
    /// @return `ESP_OK` on success, `ESP_ERR_NOT_SUPPORTED` when calibration is unavailable, or another `esp_err_t`.
    esp_err_t voltage(int pin, int *millivolts) const;
    /// Reads the calibrated ADC voltage in millivolts.
    ///
    /// @param pin GPIO to read.
    /// @return Calibrated voltage in mV, or `0` on failure.
    [[nodiscard]] int voltage(int pin) const;

    /// Resolves a GPIO to its ADC unit and channel.
    ///
    /// @param pin GPIO to resolve.
    /// @param unit Receives the ADC unit.
    /// @param channel Receives the ADC channel.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t channel(int pin, adc_unit_t *unit, adc_channel_t *channel) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static int findSlotByPin(int pin);
    static int findFreeSlot(void);
};

/// Global ADC convenience object.
extern Adc adc;

} // namespace esp32libfun

using esp32libfun::adc;
using esp32libfun::Adc;
