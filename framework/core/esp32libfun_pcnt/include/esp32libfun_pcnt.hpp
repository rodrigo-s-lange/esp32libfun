#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ESP32LIBFUN_PCNT_VERSION "v0.1.1"
#define ESP32LIBFUN_PCNT_VERSION_MAJOR 0
#define ESP32LIBFUN_PCNT_VERSION_MINOR 1
#define ESP32LIBFUN_PCNT_VERSION_PATCH 1

namespace esp32libfun {

constexpr int PCNT_RISE = 1;
constexpr int PCNT_FALL = 2;
constexpr int PCNT_BOTH = 3;

/// Callback fired in task context by `Pcnt::watch()`.
///
/// @param pin Pin that reached the watch point.
/// @param watch_point Watch point value that was hit.
/// @param user_ctx Opaque pointer passed through from `Pcnt::watch()`.
using pcnt_callback_t = void (*)(int pin, int watch_point, void *user_ctx);

class Pcnt {
public:
    static constexpr size_t MAX_COUNTERS = 8;

    /// Configures and immediately starts one simple pulse counter on the selected pin.
    ///
    /// The hardware counter uses `low_limit` and `high_limit`, while the driver
    /// accumulation path lets `count()` report values beyond that range.
    ///
    /// @param pin Input GPIO to count pulses on.
    /// @param edge One of `PCNT_RISE`, `PCNT_FALL`, or `PCNT_BOTH`.
    /// @param low_limit Hardware low limit; must be negative.
    /// @param high_limit Hardware high limit; must be positive.
    /// @param glitch_ns Glitch filter width in nanoseconds, or `0` to disable it.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t begin(int pin,
                    int edge = PCNT_RISE,
                    int low_limit = -32768,
                    int high_limit = 32767,
                    uint32_t glitch_ns = 0) const;
    /// Stops and releases one pulse counter.
    ///
    /// @param pin Pin whose counter should be released.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(int pin) const;
    /// Returns true when the selected pin is already attached to PCNT.
    ///
    /// @param pin Pin to check.
    /// @return true when the pin already owns a counter.
    [[nodiscard]] bool ready(int pin) const;

    /// Restarts counting on one configured pin after stop().
    ///
    /// @param pin Pin whose counter should resume.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t start(int pin) const;
    /// Pauses counting pulses on one configured pin.
    ///
    /// @param pin Pin whose counter should pause.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t stop(int pin) const;
    /// Clears the current count to zero.
    ///
    /// @param pin Pin whose count should be cleared.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t clear(int pin) const;

    /// Reads the current accumulated count.
    ///
    /// @param pin Pin to read.
    /// @param value Receives the current accumulated count.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t count(int pin, int *value) const;
    /// Reads the current accumulated count.
    ///
    /// @param pin Pin to read.
    /// @return Current accumulated count, or `0` on failure.
    [[nodiscard]] int count(int pin) const;

    /// Registers one optional watch point callback in task-context.
    ///
    /// @param pin Pin to watch.
    /// @param watch_point Count value that should trigger the callback.
    /// @param callback Function invoked in task context when the watch point is reached.
    /// @param user_ctx Opaque pointer passed through to `callback`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t watch(int pin, int watch_point, pcnt_callback_t callback, void *user_ctx = nullptr) const;
    /// Disables the watch point callback for one configured pin.
    ///
    /// @param pin Pin whose watch point callback should be removed.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t watchOff(int pin) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t ensureCallbackRuntime(void);
    static int findSlotByPin(int pin);
    static int findFreeSlot(void);
};

/// Global PCNT convenience object.
extern Pcnt pcnt;

} // namespace esp32libfun

using esp32libfun::pcnt;
using esp32libfun::PCNT_RISE;
using esp32libfun::PCNT_FALL;
using esp32libfun::PCNT_BOTH;
using esp32libfun::pcnt_callback_t;
