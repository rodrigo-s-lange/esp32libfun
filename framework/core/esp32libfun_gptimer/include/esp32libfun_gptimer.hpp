#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ESP32LIBFUN_GPTIMER_VERSION "v0.2.0"
#define ESP32LIBFUN_GPTIMER_VERSION_MAJOR 0
#define ESP32LIBFUN_GPTIMER_VERSION_MINOR 2
#define ESP32LIBFUN_GPTIMER_VERSION_PATCH 0

namespace esp32libfun {

/// Callback fired in task context by `Gptimer::alarm()`.
///
/// @param timer Timer slot index that triggered the alarm.
/// @param count Current timer count when the alarm fired.
/// @param user_ctx Opaque pointer passed through from `Gptimer::alarm()`.
using gptimer_callback_t = void (*)(int timer, uint64_t count, void *user_ctx);

class Gptimer {
public:
    static constexpr size_t MAX_TIMERS = 4;
    static constexpr uint32_t DEFAULT_RESOLUTION_HZ = 1000000;

    /// Creates and optionally starts one general-purpose timer.
    ///
    /// @param timer Logical timer slot index owned by this wrapper.
    /// @param resolution_hz Timer resolution in Hz.
    /// @param start_now When true, starts counting immediately after setup.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t begin(int timer = 0,
                    uint32_t resolution_hz = DEFAULT_RESOLUTION_HZ,
                    bool start_now = true) const;
    /// Stops, disables, and releases one general-purpose timer.
    ///
    /// @param timer Logical timer slot index to release.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(int timer = 0) const;
    /// Returns true when the selected timer slot is configured.
    ///
    /// @param timer Logical timer slot index to check.
    /// @return true when the timer slot owns a driver handle.
    [[nodiscard]] bool ready(int timer = 0) const;

    /// Starts counting on one configured timer.
    ///
    /// @param timer Logical timer slot index to start.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t start(int timer = 0) const;
    /// Stops counting on one configured timer.
    ///
    /// @param timer Logical timer slot index to stop.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t stop(int timer = 0) const;
    /// Clears the raw count to zero.
    ///
    /// @param timer Logical timer slot index to clear.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t clear(int timer = 0) const;

    /// Reads the raw timer count.
    ///
    /// @param timer Logical timer slot index to read.
    /// @param value Receives the raw timer count.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t count(int timer, uint64_t *value) const;
    /// Reads the raw timer count.
    ///
    /// @param timer Logical timer slot index to read.
    /// @return Raw timer count, or `0` on failure.
    [[nodiscard]] uint64_t count(int timer = 0) const;
    /// Reads elapsed time in microseconds.
    ///
    /// @param timer Logical timer slot index to read.
    /// @return Elapsed microseconds, or `0` on failure.
    [[nodiscard]] uint64_t micros(int timer = 0) const;
    /// Reads elapsed time in milliseconds.
    ///
    /// @param timer Logical timer slot index to read.
    /// @return Elapsed milliseconds, or `0` on failure.
    [[nodiscard]] uint64_t millis(int timer = 0) const;
    /// Returns the configured timer resolution.
    ///
    /// @param timer Logical timer slot index to query.
    /// @return Timer resolution in Hz, or `0` on failure.
    [[nodiscard]] uint32_t resolution(int timer = 0) const;

    /// Configures an alarm and delivers callbacks in task context.
    ///
    /// The timer count is reset to zero when the alarm is configured so
    /// `period_us` is relative to the configuration call.
    ///
    /// @param timer Logical timer slot index to configure.
    /// @param period_us Alarm period in microseconds.
    /// @param callback Function invoked in task context when the alarm fires.
    /// @param user_ctx Opaque pointer passed through to `callback`.
    /// @param auto_reload When true, repeats the alarm periodically.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t alarm(int timer,
                    uint64_t period_us,
                    gptimer_callback_t callback,
                    void *user_ctx = nullptr,
                    bool auto_reload = true) const;
    /// Disables the alarm for one configured timer.
    ///
    /// @param timer Logical timer slot index whose alarm should be disabled.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t alarmOff(int timer = 0) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t ensureCallbackRuntime(void);
};

/// Global GPTimer convenience object.
extern Gptimer gptimer;

} // namespace esp32libfun

using esp32libfun::gptimer;
using esp32libfun::Gptimer;
using esp32libfun::gptimer_callback_t;
