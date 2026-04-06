# Plugin System

## Goal

The plugin system lets a component extend `AT` without editing framework core
files.

A new component should be able to:

- expose its own public API
- register AT commands

This keeps `esp32libfun_at` small and generic.

## Model

Registration is based on static constructors.

A component declares static registrar objects in one of its `.cpp` files:

- `AtRegistrar` adds one AT command to the AT registry

These constructors run before `app_main()`.

`esp32libfun_init()` then performs the framework bootstrap:

- initializes the base framework
- starts the AT console when AT is enabled

The result is simple:

- adding a component does not require editing framework core files
- adding a component to `REQUIRES` is enough to make its AT commands available

## Registration Flow

```text
Build time
  user adds the component to REQUIRES
  linker includes the component

Boot before app_main
  C++ runs static constructors
  component registrars call at.add()
  the AT table is populated

app_main
  esp32libfun_init()
  registered AT commands are available
```

## What the Component Author Writes

```cpp
#include "esp32libfun_at.hpp"
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
```

No change is required in `esp32libfun_at.cpp`.

That is the preferred pattern for reusable libraries, because it keeps the AT
integration close to the component and avoids application glue code.

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

## Registering Commands In `main.cpp`

Yes, this is also supported.

This path is useful when:

- you are prototyping
- the command is application-specific
- you do not want a dedicated `*_at.cpp` file yet

In that case, register commands after `esp32libfun_init()`:

```cpp
#include <string.h>

#include "esp32libfun.hpp"

namespace {

constexpr int gpioPin = 8;

void atGpio8Set(const char *args)
{
    gpio.cfg(gpioPin, OUTPUT);

    if (strcmp(args, "1") == 0 || strcmp(args, "ON") == 0) {
        gpio.high(gpioPin);
        at.writeLine("OK");
        return;
    }

    if (strcmp(args, "0") == 0 || strcmp(args, "OFF") == 0) {
        gpio.low(gpioPin);
        at.writeLine("OK");
        return;
    }

    at.writeError("use 0/1 or OFF/ON");
}

void atGpio8Get(const char *)
{
    at.writeLine("GPIO8=%d", gpio.state(gpioPin) ? 1 : 0);
}

} // namespace

extern "C" void app_main(void)
{
    esp32libfun_init();

    gpio.cfg(gpioPin, OUTPUT);
    at.registerCmd("AT+GPIO8", atGpio8Set, "GPIO 8 control: 0|1|OFF|ON");
    at.registerCmd("AT+GPIO8?", atGpio8Get, "Read GPIO 8 output state");
}
```

Supported calls:

```text
AT+GPIO8=1
AT+GPIO8=0
AT+GPIO8 ON
AT+GPIO8 OFF
AT+GPIO8?
```

See also:

- `docs/examples/at_gpio_from_main.cpp`

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

## Lambda Rule

Handlers must be plain function pointers or captureless lambdas.

```cpp
[](const char *args) { wifi.connect(args); }
```

Captured lambdas are not supported here.

## Kconfig

The AT registry is fixed-size and configurable:

```kconfig
config ESP32LIBFUN_AT_MAX_CMDS
    int "Maximum AT commands"
    default 16
```

This keeps memory use explicit and predictable.

## Link Model

`esp32libfun_at` is linked as a whole-archive component so the bootstrap path
remains reliable even when the final link order changes.

This preserves the simple model:

- the AT registry is always present when enabled
- component authors register capabilities once
- application authors only add components and call `esp32libfun_init()`

## Rodrigo Lange 2026-03-31
