# esp32libfun_serial

Thin console wrapper for `esp32libfun`.

This module keeps console I/O short and readable without hiding the ESP-IDF
console backend model.

## Scope

- console init and deinit
- backend detection for diagnostics
- formatted output with `print()` and `println()`
- line-oriented input with `readLine()`
- byte-oriented input with `readByte()`
- lightweight ANSI color macros for terminal output

This module does not create a command shell by itself. It only provides the
console transport helpers used by the rest of the framework.
Console backend selection stays at project level.

## Enable It

Enable the module in Kconfig or `sdkconfig.defaults`:

```text
CONFIG_ESP32LIBFUN_SERIAL=y
```

For ESP32-S3, the usual console choice is:

```text
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

## Public API

- `serial.init()`: initializes the active ESP-IDF console backend.
- `serial.deinit()`: deinitializes the active console backend.
- `serial.isInitialized()`: reports whether the backend is ready.
- `serial.setEcho(enabled)`: enables or disables local echo for `readLine()`.
- `serial.echoEnabled()`: reports whether `readLine()` currently echoes typed characters.
- `serial.backend()`: returns the active backend name.
- `serial.print(fmt, ...)`: writes formatted text without a newline.
- `serial.println(fmt, ...)`: writes formatted text followed by CRLF.
- `serial.readByte(&ch)`: reads one byte, or returns a negative value on timeout. Used for esp32libfun_at and other byte-oriented parsers.
- `serial.readLine(buffer, length)`: reads one line into a buffer until `\n`, `\r`, or `\r\n`.

## Console Notes

- `serial` uses the console backend already selected in ESP-IDF.
- on ESP32-S3,C3,C6,etc `USB Serial/JTAG` is usually the simplest console path.
- `setEcho(true)` affects `readLine()` only.
- `readByte()` stays raw so higher-level code, like AT-style parsers, can keep full control.
- `readLine()` handles backspace and delete locally when echo is enabled.
- `readLine()` is intentionally conservative for terminal safety: it accepts printable ASCII, ignores ANSI escape sequences, and drops unsupported control or multibyte bytes.

## AT Integration

- `serial.at(true)`: registers the optional serial AT command set.
- `serial.at(false)`: unregisters the serial AT command set.
- `serial.atEnabled()`: reports whether the serial AT command set is active.

This follows the sidecar AT pattern documented in [`docs/at-integration.md`](../../../docs/at-integration.md). The base serial transport stays independent from the AT registry, and the command layer is only enabled when interactive control is useful.

Commands registered by `serial.at(true)`:

- `AT+SERIALPRINT=<text>`
- `AT+SERIALPRINTLN=<text>`
- `AT+SERIALECHO=<0|1>`
- `AT+SERIALECHO?`
- `AT+SERIALBACKEND?`

Behavior notes:

- `serial.at(true)` is idempotent. Calling it more than once does not duplicate commands.
- `serial.at(false)` is idempotent. Calling it more than once is safe.
- when `CONFIG_ESP32LIBFUN_AT=n`, the method returns `ESP_ERR_NOT_SUPPORTED`.
- when the AT subsystem is not initialized, the method returns `ESP_ERR_INVALID_STATE`.

### Command Details

- `AT+SERIALPRINT=<text>`: prints the text through `serial.print()` and then emits a line break.
- `AT+SERIALPRINTLN=<text>`: prints the text through `serial.println()`.
- `AT+SERIALECHO=<0|1>`: changes the local echo mode used by `serial.readLine()`.
- `AT+SERIALECHO?`: reports the current line-echo state.
- `AT+SERIALBACKEND?`: reports the active ESP-IDF console backend name.

### AT Example

```cpp
#include "esp32libfun.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(serial.at(true));

    serial.println(C "try: " W "AT+SERIALECHO?" W ", " W "AT+SERIALECHO=1" W ", " W "AT+SERIALBACKEND?" W);

    while (true) {
        delay.s(1);
    }
}
```

## Color Macros

- `G`: success or confirmed state color `green`
- `Y`: warning or attention color `yellow`
- `R`: error or critical state color `red`
- `O`: library tag or module identifier color `orange`
- `C`: general info or context color `cyan`
- `M`: secondary info or less important details color `magenta`
- `B`: debug or verbose output color `blue`
- `P`: decorative output color `purple`
- `K`: user input color `black`
- `W`: full reset to default terminal color `write reset`

`print()` and `println()` already append a reset sequence automatically, so `W`
is only needed when you want to reset the color before the end of the call.

## Usage

```cpp
#include "esp32libfun.hpp"

constexpr size_t LineBufferSize = 96;
char line[LineBufferSize] = {};

extern "C" void app_main(void)
{
    esp32libfun_init();
    serial.setEcho(true);

    serial.println(C "serial backend: " O "%s", serial.backend());
    serial.println(C "type one line and press enter");

    while (true) {
        serial.print(O "> " W);
        if (serial.readLine(line, sizeof(line)) == ESP_OK) {
            serial.println(C "rx: " O "%s", line);
        }
    }
}
```

## Byte Input Example

```cpp
#include "esp32libfun.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();
    serial.println(C "press keys to echo bytes");

    while (true) {
        char ch = 0;
        if (serial.readByte(&ch) > 0) {
            serial.println(C "byte: " O "%c" C " (0x%02X)", ch, static_cast<unsigned char>(ch));
        }
        delay.ms(10);
    }
}
```

## Example

- `examples/basic/serial_at`
