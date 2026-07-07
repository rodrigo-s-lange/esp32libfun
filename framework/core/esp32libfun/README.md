# esp32libfun

Application-facing aggregator for the enabled `esp32libfun_*` core modules.

This component is a convenience entry point. It exposes the public headers of
enabled core modules and provides the framework version helpers plus
`esp32libfun_init()`.

## Scope

- expose enabled core headers from one include
- provide version constants and helpers
- initialize the serial and AT base services when enabled

This component is not a required dependency for external `esp_*` libraries.
Libraries should depend on the specific core modules they use.

## Public API

- `ESP32LIBFUN_VERSION`
- `ESP32LIBFUN_VERSION_MAJOR`
- `ESP32LIBFUN_VERSION_MINOR`
- `ESP32LIBFUN_VERSION_PATCH`
- `ESP32LIBFUN_IDF_BASELINE`
- `ESP32LIBFUN_<MODULE>_VERSION` macros from each enabled module header
- `esp32libfun_version()`
- `esp32libfun_major()`
- `esp32libfun_minor()`
- `esp32libfun_patch()`
- `esp32libfun_init()`

## Usage

```cpp
#include "esp32libfun.hpp"

extern "C" void app_main(void)
{
    esp32libfun_init();

    serial.println(C "esp32libfun %s", esp32libfun_version());
}
```

## Examples

- `examples/basic/serial_at`
