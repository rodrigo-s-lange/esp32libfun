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

constexpr int kLedPin = 8;

extern "C" void app_main(void)
{
    // Convenience bootstrap for the enabled core modules.
    esp32libfun_init();

    // Fast serial feedback from the core.
    serial.println(C "Hello from Libfun! Version: %s", ESP32LIBFUN_VERSION);

    // Thin GPIO wrapper over ESP-IDF.
    gpio.cfg(kLedPin, OUTPUT);

    while (true) {
        gpio.toggle(kLedPin);
        // C = cyan color macro, O = orange, W = white, etc.
        serial.println(O "LED on GPIO " C "%d", kLedPin);
        // Short delay API keeps the common path compact.
        delay.s(1);
    }
}
```

The core stays thin and direct:

- `serial` gives fast textual feedback
- `gpio` stays close to the hardware
- `delay` keeps simple loops readable

On top of that, `esp_*` libraries add reusable behavior without forcing a giant framework:

```cpp
#include "esp32libfun.hpp"
#include "esp_button.hpp"

// Event callback fired when the button confirms a click.
static void onButtonClick(Button &instance)
{
    serial.println(O "Button click on GPIO " C "%d", instance.pin());
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    // Configure one button in manual mode.
    button.init(9, BUTTON_INPUT_PULLUP, true);

    // Register one callback for the CLICK event.
    button.onClick(onButtonClick);

    while (true) {
        // Advance the button state machine explicitly.
        button.loop();
        delay.ms(5);
    }
}
```

The same library can also opt into a managed task when convenience matters more than manual polling:

```cpp
#include "esp32libfun.hpp"
#include "esp_button.hpp"

// Same callback as the manual example.
static void onButtonClick(Button &instance)
{
    serial.println(O "Button click on GPIO " C "%d", instance.pin());
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    // init() keeps the library predictable and ready.
    button.init(9, BUTTON_INPUT_PULLUP, true);
    button.onClick(onButtonClick);

    // start() enables the optional managed task.
    button.start();

    while (true) {
        // The button task is running in the background now.
        delay.s(1);
    }
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

## Get Started

This section is being built next. The intended flow is:

1. Install ESP-IDF 6.0 and the VS Code ESP-IDF extension
2. Open this repository in VS Code
3. Select the target and serial port
4. Build, flash, and monitor the example in `main/main.cpp`
5. Start editing the core or an `esp_*` library from there

Planned additions for this section:

- one clean VS Code screenshot
- the exact build / flash / monitor commands
- the shortest possible first-run path
- a pointer to `docs/examples/` and `esp_component_template`
