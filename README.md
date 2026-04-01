# esp32libfun

**A small C++ framework on top of ESP-IDF 6.0 built for short code, direct hardware work, and fast comprehension by humans and LLMs.**

`esp32libfun` exists to make ESP-IDF projects feel lighter without hiding the SDK.
It keeps the core small, builds reusable `esp_*` libraries on top of it, and
favors APIs that are easy to scan, easy to type, and easy to extend.

## Why It Exists

- ESP-IDF is powerful, but common paths can become verbose
- many embedded projects repeat the same transport and device setup
- AI-assisted development works better when naming and structure are predictable

## Why It Feels Different

- thin `esp32libfun_*` core modules instead of a giant monolith
- `esp_*` libraries for devices and higher-level behavior
- direct C++ APIs with low ceremony
- readable enough for fast "vibe coding" without losing the ESP-IDF escape hatch

## The Idea In Code

```cpp
#include "esp32libfun.hpp"

uint64_t tick = 0;

extern "C" void app_main(void)
{
    esp32libfun_init();

    serial.println(C "Hello from Libfun! Version: %s", ESP32LIBFUN_VERSION);

    while (true) {
        serial.println(O "Tick: " C "%llu", tick++);
        delay.s(1);
    }
}
```

```cpp
#include "esp32libfun_i2c.hpp"
#include "esp_pca9685.hpp"

Pca9685 pca9685;

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(i2c.begin(8, 9, I2C_FAST));
    ESP_ERROR_CHECK(pca9685.init());
    ESP_ERROR_CHECK(pca9685.duty(0, 50));
}
```

## Mental Model

```text
framework/core/esp32libfun_*   -> thin core modules over ESP-IDF
framework/libs/esp_*           -> device and higher-level libraries
framework/libs/esp_component_template
                               -> starting point for new esp_* libraries
docs/examples/                 -> real usage examples
main/                          -> fast iteration and hardware validation
```

## Project Shape

`esp32libfun` is organized so a new reader can infer the structure quickly:

- use the core directly when you want thin wrappers over ESP-IDF
- use `esp_*` libraries when you want device-focused behavior on top of the core
- use `esp_component_template` when creating a new library in the project style

---

This README is being built in stages. The next section will be `Get Started`.
