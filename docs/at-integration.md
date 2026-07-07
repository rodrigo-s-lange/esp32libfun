# AT Integration

AT support in `esp32libfun` is an optional diagnostic and bring-up layer.

The base module must remain usable without AT. A sidecar adds commands on top of
an existing module without moving transport or device logic into the AT core.

## Sidecar Shape

For a component named:

```text
framework/core/esp32libfun_gpio
```

the AT sidecar file is:

```text
framework/core/esp32libfun_gpio/esp32libfun_gpio_at.cpp
```

The public module usually exposes:

```cpp
esp_err_t at(bool enable = true) const;
bool atEnabled(void) const;
```

`at(true)` registers commands. `at(false)` unregisters them.

## Command Rules

- command names start with `AT+`
- query commands end with `?`
- command help text must be short enough for `AT+HELP?`
- invalid user input returns `ERROR: ...`
- successful operations return useful data and/or `OK`
- commands should not hide board-specific pin choices

## Registration Rules

Use the global AT object from `esp32libfun_at` and keep the command handlers in
the sidecar translation unit.

Sidecars should:

- register all commands atomically from the user's point of view
- unregister all commands on `at(false)`
- keep parser helpers local to the sidecar
- call the module's public API instead of reaching into private state

## Documentation Rules

Each module with a sidecar must document:

- how to enable the sidecar from C++
- each command syntax
- expected response shape
- one short terminal session

Example:

```cpp
ESP_ERROR_CHECK(serial.at(true));
ESP_ERROR_CHECK(gpio.at(true));
```

Terminal:

```text
AT+GPIO=43,1
OK
AT+GPIO?43
GPIO43=1
OK
```
