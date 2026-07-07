# esp_example_reference

**Reference / example only. Not a core module. Not a maintained device
library. Not a dependency target for real applications.**

This component is `esp_component_template` renamed and instantiated exactly
once, so there is a concrete, buildable `esp_*` library in this repository to
read before writing a new one. It does not talk to any real hardware — `step()`
just increments a counter and calls an optional callback.

Treat it as documentation, not as a starting point to depend on or extend. Do
not add real device logic here. If you need a real `esp_*` library, copy
`framework/libs/esp_component_template` into its own repository instead — see
`framework/libs/esp_component_template/README.md`.

## What it shows

- the rename in practice: `esp_component_template` / `Template` / `templ` /
  `template_callback_t` become `esp_example_reference` / `Reference` /
  `reference` / `reference_callback_t`
- the same `init()/start()/stop()/end()` lifecycle contract, unchanged
- the same locking pattern for shared mutable state
- short `///` header comments, matching the core module convention

## Usage

```cpp
#include "esp32libfun.hpp"
#include "esp_example_reference.hpp"

static void onTick(Reference &instance)
{
    serial.println(O "[esp_example_reference]" C " tick %lu from %s",
                   static_cast<unsigned long>(instance.counter()),
                   instance.name());
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(reference.init("reference", 1000));
    ESP_ERROR_CHECK(reference.onTick(onTick));
    ESP_ERROR_CHECK(reference.start());

    while (true) {
        delay.s(1);
    }
}
```
