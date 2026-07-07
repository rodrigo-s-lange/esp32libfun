# esp32libfun_delay

Small delay helpers for `esp32libfun`.

This module keeps common waits short and readable without hiding whether the
delay is cooperative or busy.

## Scope

- task-friendly delays in hours, minutes, seconds, milliseconds, and ticks
- short busy-wait delays in microseconds
- waiting until a target uptime in milliseconds

This module does not create timers, callbacks, schedules, or background tasks.
It is only a compact timing helper.

## Enable It

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_DELAY=y
```

## Public API

- `delay.h(hours)`: suspends the current task for a number of hours.
- `delay.m(minutes)`: suspends the current task for a number of minutes.
- `delay.s(seconds)`: suspends the current task for a number of seconds.
- `delay.ms(milliseconds)`: suspends the current task for a number of milliseconds.
- `delay.us(microseconds)`: busy-waits for a short number of microseconds.
- `delay.t(ticks)`: suspends the current task for a number of FreeRTOS ticks.
- `delay.millis(target_millis)`: waits until system uptime reaches a target millisecond mark.

## Timing Notes

- `delay.h()`, `delay.m()`, `delay.s()`, and `delay.t()` are cooperative delays for the current FreeRTOS task.
- `delay.ms()` yields through FreeRTOS for whole ticks and uses a short busy-wait only for the remaining sub-tick fraction.
- `delay.us()` is a pure busy-wait and should stay limited to short hardware timing gaps.
- `delay.millis()` is useful for startup sequencing when you want to wait until a known uptime threshold.

## AT Integration

No AT integration is provided for `delay`.

That is intentional:

- there is no useful persistent state to inspect
- command-driven delay control would only block the console task
- the module is already fully expressed by its direct API

`delay` is a good example of a core module that should stay simple and not grow
an AT sidecar.

## Usage

```cpp
#include "esp32libfun.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();

    while (true) {
        serial.println("tick");
        delay.s(1);
    }
}
```

## Startup Wait Example

```cpp
#include "esp32libfun.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();

    delay.millis(2000);
    serial.println("system uptime reached 2000 ms");
}
```

## Example

- `examples/basic/serial_at`
