#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

namespace esp32libfun {

class Delay {
public:
    // Cooperative delay for the current FreeRTOS task only.
    void h(uint64_t hours) const;
    // Cooperative delay for the current FreeRTOS task only.
    void m(uint64_t minutes) const;
    // Cooperative delay for the current FreeRTOS task only.
    void s(uint64_t seconds) const;
    // Cooperative delay for the current FreeRTOS task only.
    void ms(uint64_t milliseconds) const;
    // Busy-wait delay in microseconds. Use only for short delays.
    // This does not yield like vTaskDelay-based delays.
    void us(uint64_t microseconds) const;
    /*
     * Delays for a number of FreeRTOS ticks.
     * This suspends only the current task and yields execution to other ready tasks.
     * With the current project configuration, 1 tick is equivalent to portTICK_PERIOD_MS milliseconds.
     */
    void t(uint64_t ticks) const;
    /*
     * Waits until the system uptime in milliseconds is greater than or equal to target_millis.
     * Useful for waiting until hardware settles after power-up or reset.
     * This suspends only the current task while waiting.
     */
    void millis(uint64_t target_millis) const;

private:
    static uint64_t uptimeMillis(void);
    static void delayTicks(uint64_t ticks);
};

extern Delay delay;

} // namespace esp32libfun

using esp32libfun::delay;
