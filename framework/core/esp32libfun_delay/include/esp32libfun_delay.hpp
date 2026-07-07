#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

#define ESP32LIBFUN_DELAY_VERSION "v0.1.0"
#define ESP32LIBFUN_DELAY_VERSION_MAJOR 0
#define ESP32LIBFUN_DELAY_VERSION_MINOR 1
#define ESP32LIBFUN_DELAY_VERSION_PATCH 0

namespace esp32libfun {

/// Small delay helpers built on top of FreeRTOS and ESP-IDF timing primitives.
class Delay {
public:
    /// Suspends the current task for a number of hours.
    ///
    /// This is a cooperative delay built on top of the millisecond path.
    ///
    /// @param hours Number of hours to sleep.
    void h(uint64_t hours) const;
    /// Suspends the current task for a number of minutes.
    ///
    /// This is a cooperative delay built on top of the millisecond path.
    ///
    /// @param minutes Number of minutes to sleep.
    void m(uint64_t minutes) const;
    /// Suspends the current task for a number of seconds.
    ///
    /// This is a cooperative delay built on top of the millisecond path.
    ///
    /// @param seconds Number of seconds to sleep.
    void s(uint64_t seconds) const;
    /// Suspends the current task for a number of milliseconds.
    ///
    /// Whole ticks yield through FreeRTOS. Sub-tick remainders fall back to a
    /// short busy-wait in microseconds.
    ///
    /// @param milliseconds Number of milliseconds to sleep.
    void ms(uint64_t milliseconds) const;
    /// Busy-waits for a number of microseconds.
    ///
    /// Use only for short delays. This does not yield like vTaskDelay-based
    /// delays.
    ///
    /// @param microseconds Number of microseconds to busy-wait.
    void us(uint64_t microseconds) const;
    /// Suspends the current task for a number of FreeRTOS ticks.
    ///
    /// This yields execution to other ready tasks. With the current project
    /// configuration, one tick is equivalent to `portTICK_PERIOD_MS`
    /// milliseconds.
    ///
    /// @param ticks Number of FreeRTOS ticks to sleep.
    void t(uint64_t ticks) const;
    /// Waits until system uptime reaches or passes `target_millis`.
    ///
    /// Useful for startup settling windows after reset or power-up. This
    /// suspends only the current task while waiting.
    ///
    /// @param target_millis Absolute system uptime, in milliseconds, to wait for.
    void millis(uint64_t target_millis) const;

private:
    static uint64_t uptimeMillis(void);
    static void delayTicks(uint64_t ticks);
};

/// Global delay convenience object.
extern Delay delay;

} // namespace esp32libfun

using esp32libfun::delay;
