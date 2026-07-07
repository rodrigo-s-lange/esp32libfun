# esp32libfun RAG Guide

This guide defines the short context surface that humans and agents should use
when working inside `esp32libfun`.

## Read Order

For architecture or implementation work, read:

1. `AGENTS.md`
2. `README.md`
3. `docs/architecture.md`
4. `docs/style-guide.md`
5. `docs/vibe_coding.md`
6. The target component header under `framework/core/<component>/include/`
7. The target component `README.md`, when present

For AT sidecar work, also read:

1. `docs/at-integration.md`
2. `framework/core/esp32libfun_at/include/esp32libfun_at.hpp`
3. One existing sidecar with similar scope

For new `esp_*` library work, also read:

1. `framework/libs/esp_component_template/README.md`
2. `framework/libs/esp_component_template/include/esp_component_template.hpp`
3. `framework/libs/esp_example_reference/README.md`, an already-renamed
   instantiation of the template kept only as a reading reference — do not
   depend on it or extend it

## Core Terms

- **Core module**: one `framework/core/esp32libfun_*` component.
- **Direct-call wrapper**: a module where user code calls the API directly and keeps control of application flow.
- **Plugin framework layer**: the optional AT subsystem, where commands are registered and later dispatched by the AT console.
- **Aggregator**: `framework/core/esp32libfun`, which exposes enabled core headers.
- **Sidecar**: optional `*_at.cpp` integration file in a component that registers AT commands.
- **Device library**: an external `esp_*` repository that depends on specific core modules.
- **Example**: a buildable ESP-IDF project under `examples/basic/<name>`.
- **Component template**: `framework/libs/esp_component_template`, the starting
  point for a new `esp_*` library. Not itself usable as a dependency.
- **Reference example**: `framework/libs/esp_example_reference`, a single
  renamed instantiation of the template kept for reading only. Not core, not a
  real device library, not a dependency target.

## Layering Rules

- Keep `framework/core/esp32libfun_*` small and thin over ESP-IDF.
- Treat GPIO, I2C, SPI, ADC, GPTimer, TWAI, LEDC, PCNT, MCPWM, RMT, serial, and delay as direct-call HAL wrappers unless the code clearly says otherwise.
- Treat protocol logic on top of RMT (IR framing, WS2812/addressable LED timing, etc.) as `esp_*` library work, not core work.
- Treat `esp32libfun_at` and `*_at.cpp` files as the optional plugin-style framework layer.
- Treat `framework/libs/esp_example_reference` as a reference to read, never as
  a dependency or a base to extend. New `esp_*` libraries start from
  `esp_component_template`, not from the reference example.
- Keep device-specific behavior out of the core.
- Put sensors, displays, keypads, actuators, and product behavior in external
  `esp_*` repositories.
- Use `IDF_TARGET` as the hardware baseline.
- Do not add an official board subsystem, pin registry, or pin map.

## Documentation Rules

Each consolidated core module should have:

- one `README.md`
- a compact public header with `///` comments
- one buildable example when the module benefits from hardware validation
- sidecar command documentation when it exposes `at(true)`

README files should state:

- purpose
- scope
- Kconfig option
- public API summary
- sidecar commands, when available
- one short usage example
- known hardware notes or limits

## Example Rules

Examples live under:

```text
examples/basic/<example_name>
```

Each example should include:

- `CMakeLists.txt`
- `main/CMakeLists.txt`
- `main/main.cpp`
- `sdkconfig.defaults`
- `README.md`

Examples should compile as written and use the public API only.

## Agent Rules

Before editing:

- inspect the existing module first
- prefer existing APIs and naming
- keep changes scoped
- update docs when behavior or usage changes
- avoid editing local `sdkconfig`; prefer `sdkconfig.defaults`
- do not create board-specific abstractions

When adding a sidecar:

- keep the base module usable without AT
- put command registration in `*_at.cpp`
- expose `at(bool enable = true)` and `atEnabled()` from the module only when
  the sidecar is part of the public experience
- document the command syntax and expected responses
