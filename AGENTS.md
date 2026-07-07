# AGENTS.md

Instructions for AI agents working in the `esp32libfun` repository.

## Read First

1. `README.md` — project scope and core feature list
2. `docs/architecture.md` — layering, framework boundary, runtime model
3. `docs/style-guide.md` — naming, API, and documentation rules
4. `docs/rag-guide.md` — core terms, read order for specific tasks, agent rules

Read the target component's header and `README.md` before editing it.

## Project Model

- `framework/core/esp32libfun_*` — thin, direct-call HAL wrappers over ESP-IDF.
  User code calls the API directly and keeps control of application flow.
- `esp32libfun_at` — the one core module that is an actual plugin-style
  framework layer: components register commands ahead of time, the AT console
  dispatches them later. See `docs/plugin-system.md` and
  `docs/at-integration.md`.
- `framework/libs/esp_component_template` — starting point for new `esp_*`
  libraries. Not itself a usable library.
- `framework/libs/esp_example_reference` — a minimal library built from the
  template, kept only as a working reference. Not core, not a real device
  library. Do not extend it as if it were part of the official framework.
- External `esp_*` device libraries (sensors, displays, actuators, etc.) live
  in their own repositories, outside this one.

## Rules

- Check existing core modules before adding new code. Do not invent a new
  module for something `esp32libfun_gpio`, `_ledc`, `_pcnt`, `_mcpwm`, `_i2c`,
  `_spi`, or `_serial` already covers.
- Keep the core small, thin, and pragmatic. Do not add large subsystems to
  `framework/core/`.
- `esp_*` libraries depend on the specific core modules they use, never on the
  `esp32libfun` aggregator.
- New `esp_*` libraries start from `esp_component_template`, not from
  `esp_example_reference` or from scratch.
- Preserve existing naming and structure. Do not introduce a second naming
  convention for the same kind of thing.
- ESP-IDF 6.0 is the baseline. No exceptions. No RTTI.
- Avoid heap allocation in the core.
- Treat shared core objects (`serial`, `gpio`, `i2c`, ...) as concurrently
  accessed unless the code proves otherwise; do not remove existing locking.
- Public headers document every method with a `///` brief plus `@param` for
  each parameter and `@return` for non-void results. This is read directly by
  IDE language servers (clangd/cpptools) for autocomplete and signature help,
  independent of any Doxygen generation pipeline.
- Do not edit `sdkconfig`. Use `sdkconfig.defaults` or
  `sdkconfig.defaults.<target>` for repository-wide or per-target defaults.

## When Proposing Changes

1. State which layer the change belongs to (core, aggregator, AT sidecar,
   external `esp_*` library, or application) and why.
2. Propose the smallest change consistent with the existing pattern.
3. Update the relevant `README.md` or doc when public behavior or usage
   changes.

## Author

Rodrigo Lange
