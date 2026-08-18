# esp32libfun Context Guide

This file routes humans and agents to the smallest useful context. Project
rules live in `AGENTS.md`; architecture and style remain authoritative in their
own documents.

## Baseline

Read once for architecture or implementation work:

1. `AGENTS.md`
2. `README.md`
3. `docs/architecture.md`
4. `docs/style-guide.md`
5. `docs/vibe_coding.md`
6. The target component's public header and `README.md`

## Task Routing

### Core module

- Inspect sibling core modules before adding code.
- Read the target header, source, `CMakeLists.txt`, `Kconfig`, and `README.md`.
- Keep the module a thin direct-call wrapper over ESP-IDF.

### AT or sidecar

Also read:

1. `docs/plugin-system.md`
2. `docs/at-integration.md`
3. `framework/core/esp32libfun_at/include/esp32libfun_at.hpp`
4. One existing sidecar with similar commands

The base module must remain usable without AT.

### New or incubating `esp_*` library

Also read:

1. `framework/libs/esp_component_template/README.md`
2. `framework/libs/esp_component_template/include/esp_component_template.hpp`
3. One graduated sibling with a matching transport or ownership model, when
   available

Start from `esp_component_template`. `esp_example_reference` is only a minimal
renamed example; do not extend it or depend on it.

### Application or example

- Keep board pins and product policy at application level.
- Reuse public APIs only.
- A buildable example belongs in `examples/basic/<name>` with its own
  `CMakeLists.txt`, `main/`, `sdkconfig.defaults`, and `README.md`.

## Layer Lookup

| Need | Layer |
|---|---|
| ESP-IDF peripheral or base transport wrapper | `framework/core/esp32libfun_*` |
| Optional command registration and dispatch | `esp32libfun_at` / `*_at.cpp` |
| Device, protocol, effect, or reusable higher-level behavior | external `esp_*` library |
| Pins, board wiring, and product orchestration | application |
| Convenience exposure of enabled core headers | `esp32libfun` aggregator |

Protocol logic above a transport (for example IR framing or WS2812 timing over
RMT) is library work, not core work. External libraries depend on the specific
core modules they use, never on the aggregator.

## Terms

- **Core module:** thin `esp32libfun_*` wrapper called directly by the app.
- **Aggregator:** `framework/core/esp32libfun`, a convenience entry point.
- **Sidecar:** optional `*_at.cpp` integration.
- **Device library:** external `esp_*` repository.
- **Incubated library:** external library temporarily validated under
  `framework/libs/` before graduation.
