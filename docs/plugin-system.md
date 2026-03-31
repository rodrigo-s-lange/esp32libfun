# Plugin System

## Goal

The plugin system lets a component extend `AT` and `runtime` without editing
framework core files.

A new component should be able to:

- expose its own public API
- register AT commands
- register runtime hooks

This keeps `esp32libfun_at` and `esp32libfun_runtime` small and generic.

## Model

Registration is based on static constructors.

A component declares static registrar objects in one of its `.cpp` files:

- `AtRegistrar` adds one AT command to the AT registry
- `RuntimeRegistrar` adds one module to the runtime registry

These constructors run before `app_main()`.

`esp32libfun_init()` then performs the framework bootstrap:

- initializes the base framework
- runs self-registered runtime hooks
- starts the AT console when AT is enabled

The result is simple:

- adding a component does not require editing framework core files
- adding a component to `REQUIRES` is enough to make it part of the system

## Registration Flow

```text
Build time
  user adds the component to REQUIRES
  linker includes the component

Boot before app_main
  C++ runs static constructors
  component registrars call at.add() and runtime.add()
  AT and runtime tables are populated

app_main
  esp32libfun_init()
  framework bootstrap runs
  registered modules are live
  registered AT commands are available
```

## What the Component Author Writes

```cpp
#include "esp32libfun_at.hpp"
#include "esp32libfun_runtime.hpp"
#include "esp32libfun_wifi_sta.hpp"

using namespace esp32libfun;

static AtRegistrar wifi_en {
    "AT+WIFI+EN",
    [](const char *) { wifi.enable(); },
    "Enable Wi-Fi"
};

static AtRegistrar wifi_sta {
    "AT+WIFI+STA",
    [](const char *args) { wifi.connect(args); },
    "Connect using ssid,password"
};

static RuntimeRegistrar wifi_runtime {
    "wifi",
    []() { wifi.init(); },
    []() { wifi.deinit(); }
};
```

No change is required in `esp32libfun_at.cpp`.
No change is required in `esp32libfun_runtime.cpp`.

## What the Application Author Writes

```cmake
idf_component_register(
    SRCS "main.cpp"
    REQUIRES esp32libfun esp32libfun_wifi_sta
)
```

```cpp
#include "esp32libfun.hpp"
#include "esp32libfun_wifi_sta.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();
}
```

That is the intended user experience.

## AT Contract

`esp32libfun_at` is a fixed-size command registry.

Each command stores:

- command string
- handler function
- optional help text

Handlers receive raw arguments as a single string.

Supported forms:

```text
AT+GPIO+CFG=2,OUTPUT
AT+GPIO+CFG 2,OUTPUT
AT+WIFI+EN
```

Examples of `args`:

```text
AT+GPIO+CFG=2,OUTPUT   -> "2,OUTPUT"
AT+GPIO+CFG 2,OUTPUT   -> "2,OUTPUT"
AT+WIFI+EN             -> ""
```

Parsing stays in the handler. The AT core stays generic.

## Runtime Contract

`esp32libfun_runtime` is a fixed-size module registry.

Each module stores:

- module name
- init function
- optional deinit function

`runtime.initAll()` runs modules in registration order.

`runtime.deinitAll()` runs modules in reverse order.

This gives components a simple lifecycle hook with deterministic order.

## Lambda Rule

Handlers and runtime callbacks must be plain function pointers or captureless
lambdas.

```cpp
[](const char *args) { wifi.connect(args); }
[]() { wifi.init(); }
```

Captured lambdas are not supported here.

## Kconfig

Both registries are fixed-size and configurable:

```kconfig
config ESP32LIBFUN_AT_MAX_CMDS
    int "Maximum AT commands"
    default 16

config ESP32LIBFUN_RUNTIME_MAX_MODULES
    int "Maximum runtime modules"
    default 8
```

This keeps memory use explicit and predictable.

## Link Model

`esp32libfun_at` and `esp32libfun_runtime` are linked as whole-archive
components so the bootstrap path remains reliable even when the final link
order changes.

This preserves the simple model:

- registries are always present when enabled
- component authors register capabilities once
- application authors only add components and call `esp32libfun_init()`

## Rodrigo Lange 2026-03-31
