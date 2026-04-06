#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

namespace esp32libfun {

constexpr int PCNT_RISE = 1;
constexpr int PCNT_FALL = 2;
constexpr int PCNT_BOTH = 3;

using pcnt_callback_t = void (*)(int pin, int watch_point, void *user_ctx);

class Pcnt {
public:
    static constexpr size_t MAX_COUNTERS = 8;

    /// Starts one simple pulse counter on the selected pin.
    esp_err_t begin(int pin,
                    int edge = PCNT_RISE,
                    int low_limit = -32768,
                    int high_limit = 32767,
                    uint32_t glitch_ns = 0) const;
    /// Stops and releases one pulse counter.
    esp_err_t end(int pin) const;
    /// Returns true when the selected pin is already attached to PCNT.
    [[nodiscard]] bool ready(int pin) const;

    /// Starts counting pulses on one configured pin.
    esp_err_t start(int pin) const;
    /// Stops counting pulses on one configured pin.
    esp_err_t stop(int pin) const;
    /// Clears the current count to zero.
    esp_err_t clear(int pin) const;

    /// Reads the current accumulated count.
    esp_err_t count(int pin, int *value) const;
    /// Reads the current accumulated count.
    [[nodiscard]] int count(int pin) const;

    /// Registers one optional watch point callback in task-context.
    esp_err_t watch(int pin, int watch_point, pcnt_callback_t callback, void *user_ctx = nullptr) const;
    /// Disables the watch point callback for one configured pin.
    esp_err_t watchOff(int pin) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t ensureCallbackRuntime(void);
    static int findSlotByPin(int pin);
    static int findFreeSlot(void);
};

extern Pcnt pcnt;

} // namespace esp32libfun

using esp32libfun::pcnt;
using esp32libfun::PCNT_RISE;
using esp32libfun::PCNT_FALL;
using esp32libfun::PCNT_BOTH;
using esp32libfun::pcnt_callback_t;
