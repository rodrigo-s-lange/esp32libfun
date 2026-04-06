# esp32libfun_w5500

Thin W5500 Ethernet wrapper for `esp32libfun`.

This module keeps close to ESP-IDF:

- dedicated SPI bus owned by the W5500 module
- official `esp_eth` driver and official `espressif/w5500` MAC/PHY
- explicit `begin()` / `start()` / `stop()` / `end()`
- DHCP and link state exposed with short getters

## Clock Notes

- `40 MHz` is the validated default and the safest baseline
- `60 MHz` was validated on ESP32-S3 bring-up with real DHCP traffic
- `70-80 MHz` is hardware-dependent and should be treated as experimental

If bring-up fails very early with `reset timeout`, reduce the SPI clock first.
W5500 stability depends heavily on jumper length, breadboard quality, power
integrity, and module layout.

## Enable it

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_W5500=y
```

## Current direction

- dedicated SPI host
- 40 MHz first
- hardware reset by GPIO
- interrupt pin supported when available
- polling fallback when `int_pin = -1`

## Usage

```cpp
#include "esp32libfun.hpp"
#include "esp32libfun_w5500.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(w5500.begin(37, 35, 36, 38, 39, 40));
    ESP_ERROR_CHECK(w5500.start());

    while (true) {
        serial.println("link=%s ip=%s",
                       w5500.connected() ? "UP" : "DOWN",
                       w5500.localIP());
        delay.s(2);
    }
}
```

## Example

- `docs/examples/esp32libfun_w5500_basic.cpp`
- `docs/examples/esp32libfun_w5500_s3_dhcp.cpp`
