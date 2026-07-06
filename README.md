# esp32libfun

**A small C++ HAL-oriented framework on top of ESP-IDF 6.0 built for short code, direct hardware work, and fast comprehension by humans and LLMs.**

**Originally created by Rodrigo Lange CWB/BRAZIL**

`esp32libfun` exists to make ESP-IDF projects feel lighter without hiding the SDK.
This repository is the core HAL foundation: small `esp32libfun_*` components,
focused examples, and documentation for reusable libraries built on top.

The official workflow is native ESP-IDF. PlatformIO can be useful for some
application projects, but this repository treats ESP-IDF CMake as the source of
truth.

## Why It Exists

- ESP-IDF is powerful, but common paths can become verbose.
- Embedded projects repeat the same transport and base hardware setup.
- AI-assisted development works better when naming and structure are predictable.

## Repository Scope

This repository owns:

- `framework/core/esp32libfun_*`: HAL core modules, thin over ESP-IDF.
- `framework/libs/esp_component_template`: a local template for external `esp_*` libraries.
- `docs/`: architecture, style, and core usage documentation.
- `main/`: a small validation application.

This repository does not own device libraries such as sensors, displays,
keypads, actuators, or product-specific behavior. Those belong in separate
`esp_*` repositories and should depend on the specific `esp32libfun_*` modules
they use.

## The Idea In Code

```cpp
#include "esp32libfun.hpp"

constexpr int kLedPin = 8;

extern "C" void app_main(void)
{
    esp32libfun_init();

    serial.println(C "Hello from Libfun! Version: %s", ESP32LIBFUN_VERSION);

    gpio.cfg(kLedPin, OUTPUT);

    while (true) {
        gpio.toggle(kLedPin);
        serial.println(O "LED on GPIO " C "%d", kLedPin);
        delay.s(1);
    }
}
```

## Mental Model

```text
framework/core/esp32libfun_*   -> HAL core modules, thin over ESP-IDF
framework/libs/esp_component_template
                               -> template for external esp_* libraries
docs/examples/                 -> core usage examples
main/                          -> fast iteration and hardware validation
```

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

Build, flash, and monitor from the terminal:

```bash
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with your serial port, such as `COM5` on Windows.

Changing the target:

```bash
idf.py set-target esp32s3
idf.py build
```

## Configuration

- ESP-IDF 6.0 is the baseline.
- `IDF_TARGET` is the hardware baseline.
- `sdkconfig.defaults` defines common repository defaults.
- `sdkconfig.defaults.<target>` defines target-specific defaults.
- local `sdkconfig` overrides defaults.
- `Kconfig` defines module options.
- `esp32libfun.hpp` exposes enabled core modules only.

## Core Features

- `esp32libfun`: core module with basic types, macros, and utilities. Convenience bootstrap with `esp32libfun_init()`.
- `esp32libfun_at`: basic AT command parser for simple text-based protocols and colorful console commands.
- `esp32libfun_delay`: simple delay functions for readable loops.
- `esp32libfun_gpio`: thin wrapper over ESP-IDF GPIO APIs.
- `esp32libfun_i2c`: thin wrapper over ESP-IDF I2C APIs.
- `esp32libfun_ledc`: thin wrapper over ESP-IDF LEDC APIs for PWM and fades.
- `esp32libfun_mcpwm`: thin wrapper over ESP-IDF MCPWM APIs for servo and pulse generation.
- `esp32libfun_pcnt`: thin wrapper over ESP-IDF PCNT APIs for pulse counting.
- `esp32libfun_serial`: fast formatted output with the ESP-IDF console backend.
- `esp32libfun_spi`: thin wrapper over ESP-IDF SPI APIs.
- `esp32libfun_w5500`: dedicated SPI Ethernet wrapper over ESP-IDF `esp_eth` and the official W5500 driver.
- `esp32libfun_lan8720`: RMII Ethernet wrapper over ESP-IDF `esp_eth`, the internal EMAC, and the official LAN87xx PHY driver component.
- `esp32libfun_webserver`: simple HTTP server with route handling and static file serving.
- `esp32libfun_wifi_sta`: basic Wi-Fi station mode management with event callbacks.

## Library Template

- `esp_component_template`: starting point for external `esp_*` libraries, with a simple API and optional managed task.

## Contributing

- Keep the core small and stable.
- Keep device behavior in separate `esp_*` library repositories.
- Prefer thin wrappers over ESP-IDF instead of reimplementing SDK drivers.
- Read `docs/architecture.md`, `docs/style-guide.md`, and `docs/vibe_coding.md` before large changes.

## License

Licensed under MIT. See [LICENSE](LICENSE) for details.
