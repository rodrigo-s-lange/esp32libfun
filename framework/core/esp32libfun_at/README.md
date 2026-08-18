# esp32libfun_at

Thin AT-style console layer for `esp32libfun`.

This module keeps command-driven console interaction short and readable without
hiding the serial transport or inventing a large shell framework.

## Scope

- fixed-size AT command registry
- built-in `AT`, `AT+HELP?`, and `AT+VER?`
- serial-backed console task
- explicit `init()`, `start()`, `stop()`, and `deinit()`
- manual command registration and self-registering command helpers

This module does not parse complex grammars, manage history, or build a full
CLI. It focuses on small command sets that fit the framework style.

## Enable It

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_SERIAL=y
CONFIG_ESP32LIBFUN_AT=y
```

Set the fixed registry capacity as needed:

```text
CONFIG_ESP32LIBFUN_AT_MAX_CMDS=16
```

## Public API

- `at.init()`: initializes the AT registry and synchronization primitives.
- `at.deinit()`: stops the console task and releases AT resources.
- `at.isInitialized()`: reports whether the AT subsystem is initialized.
- `at.start()`: starts the serial-backed AT console task.
- `at.stop()`: stops the serial-backed AT console task.
- `at.feedLine(line)`: parses and dispatches one AT line without CR/LF.
- `at.registerCmd(command, handler, help)`: adds one command to the fixed-size registry.
- `at.unregisterCmd(command)`: removes one previously registered command.
- `at.commandCount()`: returns the number of registered commands.
- `at.help()`: prints the built-in and registered command list.
- `at.version()`: prints the framework version.
- `at.writeLine(fmt, ...)`: prints one response line.
- `at.writeOk(fmt, ...)`: prints one green success line; defaults to `OK`.
- `at.writeWarn(fmt, ...)`: prints one yellow non-fatal warning line.
- `at.writeAlert(fmt, ...)`: prints one orange alert for accepted but risky or
  degenerate input.
- `at.writeError(fmt, ...)`: prints one error response line.

## Console Notes

- `esp32libfun_init()` already initializes `serial`, then initializes and starts `at` when `CONFIG_ESP32LIBFUN_AT=y`.
- handlers receive only the argument tail after `=` or whitespace.
- commands are ASCII-oriented and intended for small embedded console workflows.
- `AtRegistrar` allows static self-registration from any component `.cpp` file.
- optional library command sets should use the sidecar pattern described in [`docs/at-integration.md`](../../../docs/at-integration.md).

## Built-In Commands

- `AT`: replies with `OK`
- `AT+HELP?`: lists built-in and registered commands
- `AT+VER?`: prints the framework version

## Usage

```cpp
#include <string.h>

#include "esp32libfun.hpp"

namespace {

constexpr int LedPin = 8;

void atPing(const char *args)
{
    if (args != nullptr && args[0] != '\0') {
        at.writeLine("PONG %s", args);
    } else {
        at.writeLine("PONG");
    }
    at.writeOk();
}

void atCount(const char *args)
{
    (void) args;
    at.writeLine("CMDS=%u", static_cast<unsigned>(at.commandCount()));
    at.writeOk();
}

void atUnregister(const char *args)
{
    if (args == nullptr || args[0] == '\0') {
        at.writeError("use a command name");
        return;
    }

    if (at.unregisterCmd(args) == ESP_OK) {
        at.writeOk();
        return;
    }

    at.writeError("command not found");
}

} // namespace

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(gpio.cfg(LedPin, OUTPUT));
    ESP_ERROR_CHECK(gpio.low(LedPin));
    ESP_ERROR_CHECK(serial.at(true));
    ESP_ERROR_CHECK(gpio.at(true));
    ESP_ERROR_CHECK(at.registerCmd("AT+PING", atPing, "Reply with PONG"));
    ESP_ERROR_CHECK(at.registerCmd("AT+COUNT?", atCount, "Show the registered command count"));
    ESP_ERROR_CHECK(at.registerCmd("AT+UNREG", atUnregister, "Remove one registered command by name"));

    serial.println(O "AT console ready");
    serial.println(C "GPIO: " W "AT+GPIO=8,1" W ", " W "AT+GPIOTOGGLE=8" W ", " W "AT+GPIO?8" W);
    serial.println(C "SERIAL: " W "AT+SERIALECHO?" W ", " W "AT+SERIALBACKEND?" W);
    serial.println(C "CORE: " W "AT+PING" W ", " W "AT+COUNT?" W ", " W "AT+UNREG=AT+PING" W);

    while (true) {
        delay.s(1);
    }
}
```

## Self-Registration Example

```cpp
static void wifiEnable(const char *args)
{
    (void) args;
    at.writeOk();
}

static AtRegistrar wifi_enable("AT+WIFIEN", wifiEnable, "Enable Wi-Fi");
```

## Sidecar Integration Pattern

The preferred way to extend the AT console from another component is:

- keep the base library independent from `esp32libfun_at`
- expose one explicit opt-in in the public API, such as `gpio.at(true)`
- implement registration and handlers in a dedicated `*_at.cpp` file
- unregister the same commands with `lib.at(false)`

Current reference implementations:

- `esp32libfun_gpio`
- `esp32libfun_serial`

Minimal example:

```cpp
esp32libfun_init();

ESP_ERROR_CHECK(serial.at(true));
ESP_ERROR_CHECK(gpio.at(true));
```

## Example

- `examples/basic/serial_at`
