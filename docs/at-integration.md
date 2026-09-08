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
- command descriptions must remain short and useful for module-local discovery
- every sidecar command should identify its AT library when registered
- `AT+HELP?` lists the basic AT core commands plus one entry for each active
  AT library; it does not dump the libraries' individual commands
- `AT+<LIBRARY>?` lists only the commands currently registered by that library
- a library identifier must be uppercase and must not collide with one of the
  library's functional AT commands
- invalid user input returns `ERROR: ...`
- successful operations return useful data and/or `OK`
- commands should not hide board-specific pin choices

## Registration Rules

Use the global AT object from `esp32libfun_at` and keep the command handlers in
the sidecar translation unit.

Sidecars should:

- register all commands atomically from the user's point of view
- unregister all commands on `at(false)`
- use the same library name for every command so `AT+HELP?` adds and removes
  the library index entry with the command registry
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
