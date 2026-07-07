# esp32libfun_mcpwm

Thin MCPWM wrapper for `esp32libfun`.

This module keeps MCPWM setup short and readable while staying close to the
ESP-IDF MCPWM prelude driver.

## Scope

- start one MCPWM output on a selected pin
- start one complementary high/low pair
- stop and release one MCPWM output
- read and write duty in percent
- read and change frequency
- apply hardware deadtime to a complementary pair
- set pulse width directly in microseconds
- force the output level immediately

This module does not yet implement fault inputs, capture, sync orchestration,
or motor-specific bridge semantics.

## Enable It

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_MCPWM=y
```

Typical dependencies for examples:

```text
CONFIG_ESP32LIBFUN_SERIAL=y
CONFIG_ESP32LIBFUN_DELAY=y
```

## Public API

- `mcpwm.begin(pin, freq_hz, duty_percent, group, resolution_hz)`: starts one MCPWM output.
- `mcpwm.beginComplementary(pin_high, pin_low, freq_hz, duty_percent, group, resolution_hz)`: starts one active-high complementary pair.
- `mcpwm.end(pin)`: stops and releases one MCPWM output.
- `mcpwm.ready(pin)`: reports whether the pin is already attached to MCPWM.
- `mcpwm.complementary(pin)`: reports whether the selected pin belongs to a complementary pair.
- `mcpwm.duty(pin, percent)`: sets the PWM duty cycle in percent.
- `mcpwm.duty(pin)`: returns the current PWM duty cycle in percent.
- `mcpwm.freq(pin, hz)`: changes the PWM frequency while keeping the current duty ratio.
- `mcpwm.freq(pin)`: returns the configured PWM frequency.
- `mcpwm.deadtime(pin, deadtime_us)`: applies symmetric hardware deadtime to one complementary pair.
- `mcpwm.deadtime(pin)`: returns the configured deadtime in microseconds.
- `mcpwm.pulse(pin, high_us, period_us)`: sets pulse width directly in microseconds.
- `mcpwm.force(pin, level, hold_on)`: forces the output level immediately.

## Frequency Helpers

- `MCPWM_PWM`: generic PWM default, `20000`
- `MCPWM_SERVO`: servo-oriented helper, `50`

## Notes

- this wrapper is intentionally low-ceremony
- complementary mode uses a high pin plus an inverted low pin with hardware deadtime
- `pulse()` requires a resolution of `1000000 Hz` to map ticks directly to microseconds
- `force()` on complementary pairs works in final output levels, so `force(pin_low, 0)` still means final low level even though the low path is inverted internally
- for motor drivers and H-bridge behavior, prefer a higher-level library on top of this core module

## AT Integration

No AT sidecar is provided for `mcpwm`.

## Usage

```cpp
#include "esp32libfun_serial.hpp"
#include "esp32libfun_delay.hpp"
#include "esp32libfun_mcpwm.hpp"

static constexpr int kHighPin = 8;
static constexpr int kLowPin = 9;

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(serial.init());

    ESP_ERROR_CHECK(mcpwm.beginComplementary(kHighPin, kLowPin, MCPWM_PWM, 25.0f));
    ESP_ERROR_CHECK(mcpwm.deadtime(kHighPin, 2));

    while (true) {
        serial.println(C "freq=%u duty=%.1f%% deadtime=%uus",
                       static_cast<unsigned>(mcpwm.freq(kHighPin)),
                       static_cast<double>(mcpwm.duty(kHighPin)),
                       static_cast<unsigned>(mcpwm.deadtime(kHighPin)));
        delay.s(1);
    }
}
```

## Example

- `examples/basic/mcpwm_servo`
