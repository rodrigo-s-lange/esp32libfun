# esp32libfun

**A small C++ framework on top of ESP-IDF 6.0 built for short code, direct hardware work, and fast comprehension by humans and LLMs.**

**Originally created by Rodrigo Lange CWB/BRAZIL**

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
- `serial` follows the ESP-IDF console backend and works especially well with USB Serial/JTAG on C3/S3/C6/H2 targets
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

Use VS Code with the ESP-IDF extension.

On ESP32-C3, ESP32-S3, ESP32-C6, and similar chips with native USB, prefer the
ESP-IDF console backend `USB Serial/JTAG`. It removes the need for an external
USB-UART adapter and usually gives the best first-run experience with
`esp32libfun`.

Clone the repository and open it in VS Code:

```bash
git clone https://github.com/rodrigo-s-lange/esp32libfun.git
cd esp32libfun
code .
```

Then follow the first-run flow:

1. Install ESP-IDF 6.0 and the VS Code ESP-IDF extension
2. Open the cloned repository in VS Code
3. Select the target and serial port
4. Build, flash, and monitor the example in `main/main.cpp`
5. Start editing the core or an `esp_*` library from there

Planned additions for this section:

- one clean VS Code screenshot
- the exact build / flash / monitor commands
- the shortest possible first-run path
- a pointer to `docs/examples/` and `esp_component_template`

Console notes:

- `esp32libfun_serial` uses the ESP-IDF console backend selected by the build
- for native-USB targets, USB Serial/JTAG is the preferred path
- if the monitor shows boot output but you cannot interact, check the selected console backend first

## Contributing

Contributions are welcome.

Good contribution paths:

- open an issue when you find a bug, unclear behavior, or missing documentation
- open a pull request when you already have a concrete change to propose
- keep the core small and stable
- prefer new `esp_*` libraries in `framework/libs/` when adding device behavior

Before opening a PR, read:

- `docs/architecture.md`
- `docs/style-guide.md`
- `docs/vibe_coding.md`

If you want to create a new library, start from:

- `framework/libs/esp_component_template`
