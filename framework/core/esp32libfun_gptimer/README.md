# esp32libfun_gptimer

Thin GPTimer wrapper for `esp32libfun`.

This module owns general-purpose timer lifetime, raw count reads, time
conversion, and optional alarms delivered in task context. It does not
implement schedulers, software timers, or application state machines.

## Scope

- create and release logical timer slots
- start, stop, and clear a timer
- read raw ticks, microseconds, and milliseconds
- configure one alarm per timer
- deliver alarm callbacks from a small internal task instead of ISR context

## Enable It

```text
CONFIG_ESP32LIBFUN_GPTIMER=y
```

## Public API

- `gptimer.begin(timer, resolution_hz, start_now)`: creates one timer.
- `gptimer.end(timer)`: releases one timer.
- `gptimer.ready(timer)`: reports whether a timer exists.
- `gptimer.start(timer)`: starts counting.
- `gptimer.stop(timer)`: stops counting.
- `gptimer.clear(timer)`: resets the count to zero.
- `gptimer.count(timer, &value)`: reads raw ticks.
- `gptimer.count(timer)`: returns raw ticks or `0` on failure.
- `gptimer.micros(timer)`: returns elapsed microseconds.
- `gptimer.millis(timer)`: returns elapsed milliseconds.
- `gptimer.resolution(timer)`: returns the timer resolution.
- `gptimer.alarm(timer, period_us, callback, user_ctx, auto_reload)`: configures an alarm.
- `gptimer.alarmOff(timer)`: disables the alarm.

## Notes

- Alarm callbacks run in task context, not ISR context.
- `alarm()` resets the timer count to zero so `period_us` is relative to the
  call that configured it.
- The default resolution is 1MHz, so raw ticks map directly to microseconds.

## Usage

```cpp
#include "esp32libfun.hpp"

void onTick(int timer, uint64_t count, void *user_ctx)
{
    (void)user_ctx;
    serial.println(C "timer=%d count=%llu", timer, static_cast<unsigned long long>(count));
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(gptimer.begin(0));
    ESP_ERROR_CHECK(gptimer.alarm(0, 1000000, onTick));
}
```
