# esp32libfun_adc

Thin ADC oneshot wrapper for `esp32libfun`.

This module owns GPIO-to-ADC resolution, ADC unit lifetime, channel
configuration, raw reads, and optional calibration for millivolt reads. It does
not implement filtering, sampling pipelines, DMA capture, or sensor-specific
conversion math.

## Scope

- configure one ADC-capable GPIO for oneshot reads
- share ADC unit handles across configured GPIOs
- read raw conversion values
- read calibrated millivolts when calibration is available
- resolve GPIO to ADC unit/channel

Continuous ADC, averaging filters, voltage dividers, NTC conversion, and sensor
math belong in application code or an `esp_*` library.

## Enable It

```text
CONFIG_ESP32LIBFUN_ADC=y
```

## Public API

- `adc.begin(pin, atten, bitwidth, calibrate)`: configures one ADC GPIO.
- `adc.end(pin)`: releases the configured GPIO.
- `adc.ready(pin)`: reports whether a GPIO is configured.
- `adc.read(pin, &raw)`: reads a raw ADC value.
- `adc.read(pin)`: returns a raw ADC value or `0` on failure.
- `adc.voltage(pin, &mv)`: reads calibrated millivolts.
- `adc.voltage(pin)`: returns calibrated millivolts or `0` on failure.
- `adc.channel(pin, &unit, &channel)`: resolves GPIO to ADC unit/channel.

## Notes

- `begin()` still succeeds when calibration is not available; `voltage()` then
  returns `ESP_ERR_NOT_SUPPORTED`.
- ADC2 may conflict with Wi-Fi on targets where the SoC shares that hardware.
- Raw values depend on attenuation, bit width, target, input impedance, and
  board-level analog design.

## Usage

```cpp
#include "esp32libfun.hpp"

constexpr int kAdcPin = 4;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(adc.begin(kAdcPin));

    while (true) {
        int raw = adc.read(kAdcPin);
        int mv = adc.voltage(kAdcPin);
        serial.println(C "ADC GPIO%d raw=%d mv=%d", kAdcPin, raw, mv);
        delay.ms(500);
    }
}
```
