# esp32libfun Agent Guide

- This repository is an ESP-IDF 6.0 based C++ framework.
- Use `IDF_TARGET` as the hardware baseline.
- Do not introduce an official board subsystem, pin registry, or pin map.
- Keep the core small, cohesive, and validated. Devices and higher-level features stay outside the core.
- Prefer thin wrappers over ESP-IDF instead of reimplementing drivers the SDK already handles well.
- Favor short object-oriented APIs and lowercase convenience objects when they match the framework style.
- Keep public headers under `include/`, use `#pragma once`, 4-space indentation, and `///` Doxygen comments for public APIs.
- Keep implementation code inside `namespace esp32libfun {}`.
- No exceptions, no RTTI, avoid heap usage in the core, and avoid STL in hot paths or ISR-sensitive code.
- For architecture decisions, read `docs/architecture.md`.
- For coding style and component layout, read `docs/style-guide.md`.
