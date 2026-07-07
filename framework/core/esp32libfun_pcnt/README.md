# esp32libfun_pcnt

Thin pulse counter wrapper for `esp32libfun`.

This module keeps one-pin pulse counting short and readable while staying close
to the ESP-IDF PCNT driver.

## Scope

- start one pulse counter on a selected input pin
- stop and release one counter
- start, stop, and clear one counter
- read the current accumulated count
- optional watch-point callback delivered in task context

This module does not implement rotary-decoder helpers, quadrature abstraction,
or board-specific pulse-routing conventions.

## Enable It

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_PCNT=y
```

Typical dependencies for examples:

```text
CONFIG_ESP32LIBFUN_SERIAL=y
CONFIG_ESP32LIBFUN_DELAY=y
CONFIG_ESP32LIBFUN_GPIO=y
CONFIG_ESP32LIBFUN_LEDC=y
```

## Public API

- `pcnt.begin(pin, edge, low_limit, high_limit, glitch_ns)`: configures and immediately starts one counter on the selected pin.
- `pcnt.end(pin)`: stops and releases one counter.
- `pcnt.ready(pin)`: reports whether the pin is already attached to PCNT.
- `pcnt.start(pin)`: restarts counting after `pcnt.stop(pin)`.
- `pcnt.stop(pin)`: pauses counting without releasing the counter.
- `pcnt.clear(pin)`: clears the count to zero.
- `pcnt.count(pin, &value)`: reads the current accumulated count.
- `pcnt.count(pin)`: convenience overload that returns the current count.
- `pcnt.watch(pin, watch_point, callback, user_ctx)`: registers one watch point callback.
- `pcnt.watchOff(pin)`: disables the watch point callback.

`pcnt.begin()` enables ESP-IDF accumulated counting internally. The hardware
counter still uses the selected `low_limit` and `high_limit`, but
`pcnt.count()` can report values beyond that range because the driver adds
overflow/underflow events to an accumulated software value.

## Edge Helpers

- `PCNT_RISE`: count rising edges only
- `PCNT_FALL`: count falling edges only
- `PCNT_BOTH`: count both rising and falling edges

## Notes

- watch point callbacks run in task context, not directly in ISR context
- `glitch_ns` enables the hardware glitch filter when greater than zero
- use `low_limit` and `high_limit` to define the hardware accumulation limits
- the internal limit watch points required by ESP-IDF accumulation are reserved
  by the wrapper and are not delivered to user callbacks unless explicitly
  registered with `pcnt.watch()`
- for high-frequency validation, prefer a peripheral signal source such as
  LEDC, MCPWM, or RMT instead of software GPIO toggling

## Usage

```cpp
#include "esp32libfun.hpp"

static constexpr int kPulseOutPin = 8;
static constexpr int kPulseInPin = 9;

extern "C" void app_main(void)
{
    esp32libfun_init();

    serial.println(C "connect GPIO %d to GPIO %d", kPulseOutPin, kPulseInPin);
    ESP_ERROR_CHECK(gpio.cfg(kPulseOutPin, OUTPUT));
    ESP_ERROR_CHECK(pcnt.begin(kPulseInPin, PCNT_RISE));

    while (true) {
        ESP_ERROR_CHECK(gpio.on(kPulseOutPin));
        delay.ms(10);
        ESP_ERROR_CHECK(gpio.off(kPulseOutPin));
        delay.ms(10);
        serial.println(C "count=%d", pcnt.count(kPulseInPin));
    }
}
```

## Example

- `examples/basic/pcnt_ledc_counter`
