# esp32libfun_gpio

Thin GPIO wrapper for `esp32libfun`.

This module keeps direct digital I/O short and readable without hiding the
ESP-IDF GPIO model.

## Scope

- input and output configuration
- pull-up and pull-down input shortcuts
- output writes and toggles
- output shadow state via `state()`
- simple GPIO interrupts dispatched from a background task
- interrupt handler registration and unregistration
- modes: `INPUT`, `OUTPUT`, `INPUT_PULLUP`, `INPUT_PULLDOWN`, `INPUT_OUTPUT`, `INPUT_OUTPUT_OPENDRAIN`, `OUTPUT_OPENDRAIN`
- interrupt types: `GPIO_IRQ_RISING`, `GPIO_IRQ_FALLING`, `GPIO_IRQ_CHANGE`, `GPIO_IRQ_DISABLE`, `GPIO_IRQ_LOW_LEVEL`, `GPIO_IRQ_HIGH_LEVEL`

This module does not define board profiles, pin maps, or fixed pin ownership.
Pin choice stays at application level.

## Enable It

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_GPIO=y
```

## Public API

- `gpio.cfg(pin, mode)`: configures one pin for input, output, or open-drain use.
- `gpio.write(pin, level)`: writes one logical level to an output-capable pin.
- `gpio.high(pin)`: shorthand for writing high.
- `gpio.low(pin)`: shorthand for writing low.
- `gpio.read(pin)`: reads the electrical level currently seen on the pin.
- `gpio.state(pin)`: returns the last output state tracked by the library, or falls back to `read(pin)`.
- `gpio.toggle(pin)`: flips the last known output state of the pin.
- `gpio.irq(pin, type, callback, user_ctx)`: registers one interrupt callback dispatched from a background task.
- `gpio.irqOff(pin)`: disables and unregisters the interrupt callback for that pin.

## Modes

- `INPUT`: input without internal pull resistor.
- `INPUT_PULLUP`: input with internal pull-up enabled.
- `INPUT_PULLDOWN`: input with internal pull-down enabled.
- `OUTPUT`: output push-pull.
- `INPUT_OUTPUT`: bidirectional input/output.
- `INPUT_OUTPUT_OPENDRAIN`: bidirectional open-drain input/output.
- `OUTPUT_OPENDRAIN`: output open-drain.

## Interrupt Types

- `GPIO_IRQ_DISABLE`: disables interrupt handling.
- `GPIO_IRQ_RISING`: triggers on rising edge.
- `GPIO_IRQ_FALLING`: triggers on falling edge.
- `GPIO_IRQ_CHANGE`: triggers on both edges.
- `GPIO_IRQ_LOW_LEVEL`: triggers while the pin stays low.
- `GPIO_IRQ_HIGH_LEVEL`: triggers while the pin stays high.

## AT Integration

- `gpio.at(true)`: registers the optional GPIO AT command set.
- `gpio.at(false)`: unregisters the GPIO AT command set.
- `gpio.atEnabled()`: reports whether the GPIO AT command set is active.

This follows the sidecar AT pattern documented in [`docs/at-integration.md`](../../../docs/at-integration.md). The base GPIO API stays independent from the AT parser, and the command layer can be enabled only when it is useful for bring-up, demos, or live testing.

Commands registered by `gpio.at(true)`:

- `AT+GPIO=<pin>,<0|1>`
- `AT+GPIO?<pin>`
- `AT+GPIOTOGGLE=<pin>`
- `AT+GPIOCFG=<pin>,<mode>`

Behavior notes:

- `gpio.at(true)` is idempotent. Calling it more than once does not duplicate commands.
- `gpio.at(false)` is idempotent. Calling it more than once is safe.
- when `CONFIG_ESP32LIBFUN_AT=n`, the method returns `ESP_ERR_NOT_SUPPORTED`.
- when the AT subsystem is not initialized, the method returns `ESP_ERR_INVALID_STATE`.

### Command Details

- `AT+GPIO=<pin>,<0|1>`: writes one logical level to an already configured output-capable pin.
- `AT+GPIO?<pin>`: reads the current tracked state for the pin, falling back to the electrical level when needed.
- `AT+GPIOTOGGLE=<pin>`: toggles the current output state and prints the resulting value.
- `AT+GPIOCFG=<pin>,<mode>`: configures one pin using the same modes exposed by `gpio.cfg()`.

### AT Example

```cpp
#include "esp32libfun.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(gpio.cfg(8, OUTPUT));
    ESP_ERROR_CHECK(gpio.low(8));
    ESP_ERROR_CHECK(gpio.at(true));

    serial.println(C "try: " W "AT+GPIO=8,1" W ", " W "AT+GPIOTOGGLE=8" W ", " W "AT+GPIO?8" W);

    while (true) {
        delay.s(1);
    }
}
```

## Usage

```cpp
#include "esp32libfun.hpp"

constexpr int LedPin = 8;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(gpio.cfg(LedPin, OUTPUT));

    while (true) {
        ESP_ERROR_CHECK(gpio.toggle(LedPin));
        delay.s(1);
    }
}
```

## Input Example

```cpp
#include "esp32libfun.hpp"

constexpr int ButtonPin = 9;

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(gpio.cfg(ButtonPin, INPUT_PULLUP));

    while (true) {
        serial.println("button=%d", gpio.read(ButtonPin) ? 1 : 0);
        delay.ms(100);
    }
}
```

## Interrupt Example

```cpp
#include "esp32libfun.hpp"

constexpr int LedPin = 8;
constexpr int ButtonPin = 0;
volatile bool ToggleRequested = false;

void onEdge(int pin, bool level, void *user_ctx)
{
    (void) pin;
    (void) level;
    (void) user_ctx;
    ToggleRequested = true;
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(gpio.cfg(LedPin, OUTPUT));
    ESP_ERROR_CHECK(gpio.cfg(ButtonPin, INPUT_PULLUP));
    ESP_ERROR_CHECK(gpio.irq(ButtonPin, GPIO_IRQ_FALLING, onEdge));

    while (true) {
        if (ToggleRequested) {
            ToggleRequested = false;
            ESP_ERROR_CHECK(gpio.toggle(LedPin));
            serial.println("led=%d", gpio.state(LedPin) ? 1 : 0);
        }
        delay.ms(10);
    }
}
```

## Example

- `examples/basic/gpio_blink`
