# esp32libfun_ledc

Thin LEDC PWM wrapper for `esp32libfun`.

This module keeps one-pin PWM setup short and readable while staying close to
the ESP-IDF LEDC driver.

## Scope

- start one PWM output on a selected pin
- stop and release one PWM output
- read and write duty in raw or percent form
- read and change frequency
- inspect resolution and maximum duty
- optional fade helper

This module does not implement waveform scheduling, pattern engines, or board-
specific pin conventions.

## Enable It

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_LEDC=y
```

Typical dependencies for examples:

```text
CONFIG_ESP32LIBFUN_SERIAL=y
CONFIG_ESP32LIBFUN_DELAY=y
```

## Public API

- `ledc.begin(pin, freq_hz, resolution_bits, channel, invert)`: starts one PWM output.
- `ledc.end(pin)`: stops and releases one PWM output.
- `ledc.ready(pin)`: reports whether the pin is already attached to LEDC.
- `ledc.duty(pin, value)`: sets the raw duty value.
- `ledc.duty(pin)`: returns the current raw duty value.
- `ledc.percent(pin, value)`: sets duty in percent.
- `ledc.freq(pin, hz)`: changes the PWM frequency for the attached timer.
- `ledc.freq(pin)`: returns the configured PWM frequency.
- `ledc.resolution(pin)`: returns duty resolution in bits.
- `ledc.maxDuty(pin)`: returns the inclusive maximum raw duty for the current resolution.
- `ledc.fade(pin, target_duty, time_ms, wait_done)`: starts a hardware fade.
- `ledc.at(true)`: registers the optional LEDC AT sidecar.

## Frequency Helpers

- `LEDC_PWM`: generic PWM default, `5000`
- `LEDC_AUDIO`: audio-oriented helper, `20000`
- `LEDC_SERVO`: servo-oriented helper, `50`

## Notes

- one pin maps to one LEDC channel
- channels may share timers when frequency and resolution match
- changing frequency on a shared timer returns `ESP_ERR_INVALID_STATE`
- `fade()` installs LEDC fade support on first use
- for an N-bit resolution, full-scale duty is `2^N`; for example, 10-bit PWM uses `1024` for true 100%.
- ESP-IDF notes that on some targets 100% duty can be unreliable when using the target's maximum LEDC duty resolution. Prefer a lower resolution when true full-scale output matters.

## AT Integration

When `CONFIG_ESP32LIBFUN_AT=y`, `ledc.at(true)` registers:

- `AT+LEDCCFG=<pin>,<freq>[,<resolution>[,<channel>[,<invert>]]]`
- `AT+LEDC=<pin>,<percent>`
- `AT+LEDC?<pin>`
- `AT+LEDCDUTY=<pin>,<raw_duty>`
- `AT+LEDCFREQ=<pin>,<freq>`
- `AT+LEDCFADE=<pin>,<percent>,<ms>[,<wait>]`
- `AT+LEDCOFF=<pin>`

Behavior notes:

- `AT+LEDC=<pin>,<percent>` clamps percent to `0..100`.
- `AT+LEDCDUTY=<pin>,<raw_duty>` uses raw inclusive duty, so 10-bit full-scale is `1024`.
- `AT+LEDCFADE=<pin>,<percent>,<ms>` requires the pin to already be configured with `AT+LEDCCFG` or `ledc.begin()`.

### AT Session Example

```text
AT+LEDCCFG=43,1000,10
AT+LEDC=43,50
AT+LEDC?43
AT+LEDCDUTY=43,1024
AT+LEDCFADE=43,0,1000
AT+LEDCOFF=43
```

## Usage

```cpp
#include "esp32libfun.hpp"

static constexpr int kPwmPin = 8;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(ledc.begin(kPwmPin, 1000, 10));
    ESP_ERROR_CHECK(ledc.percent(kPwmPin, 50.0f));

    while (true) {
        serial.println(C "freq=%u duty=%u/%u",
                       static_cast<unsigned>(ledc.freq(kPwmPin)),
                       static_cast<unsigned>(ledc.duty(kPwmPin)),
                       static_cast<unsigned>(ledc.maxDuty(kPwmPin)));
        delay.s(1);
    }
}
```

## Example

- `examples/basic/ledc_fade`
