# esp32libfun Style Guide

**Version:** Draft - 2026-03-31  
**Target:** ESP-IDF v6.0.0+

This guide defines the coding style, API rules, and documentation direction for
the `esp32libfun` project.

## 1. Goal

The project should produce APIs that are:

- short
- readable
- easy to remember
- easy for LLMs to infer from local context

The end result should feel good in real `main.cpp` code and in iterative
"vibe coding" sessions.

## 1.1 Readability Principle

Human readability and LLM readability are explicit quality goals in this
project.

This affects API design, naming, file layout, and examples.

A good component should be understandable with minimal context by:

- a human scanning quickly
- an LLM generating or editing code locally

## 1.2 Practical Implications

This principle should lead to concrete choices:

- use stable and literal names
- avoid unnecessary synonyms between modules
- keep public headers small
- keep examples exact and up to date
- prefer one obvious usage path in the common case
- avoid public APIs that require hidden project-specific assumptions

## 2. Component Families

The project has two component families:

- core modules in `framework/core/esp32libfun_xxx/`
- device and higher-level libraries in `framework/libs/esp_xxx/`

Dependency rule:

- `esp_*` libraries depend on the specific `esp32libfun_*` modules they use
- `esp_*` libraries do not depend on the `esp32libfun` aggregator
- the `esp32libfun` component is an application-facing convenience layer

Core examples:

- `esp32libfun_gpio`
- `esp32libfun_serial`
- `esp32libfun_i2c`

Higher-level example:

- `esp_pca9685`

## 3. Naming Rules

Folder name, CMake target, and main public header should match.

Examples:

- `framework/core/esp32libfun_i2c/`
- `framework/libs/esp_pca9685/`
- `include/esp32libfun_i2c.hpp`
- `include/esp_pca9685.hpp`

When a global object helps readability, expose it in lowercase.

Examples:

- `serial`
- `gpio`
- `i2c`

## 4. API Rules

The main rule is clarity in real user code.

Preferred traits:

- short method names
- explicit arguments
- stable naming across modules
- obvious state transitions
- minimal ceremony

Examples of good names:

- `begin`
- `end`
- `init`
- `start`
- `stop`
- `cfg`
- `read`
- `write`
- `high`
- `low`
- `toggle`
- `freq`
- `duty`

Core modules and higher-level libraries do not need identical naming if their
roles are different.

Guideline:

- `framework/core/esp32libfun_*` may keep pragmatic names that match the ESP-IDF concept closely
- `framework/libs/esp_*` should prefer a more explicit and user-facing lifecycle when they own richer behavior

## 5. Human and LLM Friendly Code

Code in this project should be easy to parse visually and easy to infer
programmatically.

That usually means:

- one public class per device or service
- one clear responsibility per component
- short headers
- public methods ordered by common usage
- examples that match the production API

A strong API for this project usually lets the reader understand usage from:

- the component name
- the class name
- a few method names
- a short example

## 6. Public Header Rules

Public headers should stay compact and focused.

Guidelines:

- use `#pragma once`
- include only what the public API really needs
- prefer forward declarations when practical
- keep private implementation details out of headers
- declare public APIs with `///` comments when the behavior is not obvious

This keeps the module easier to read and faster for LLMs to interpret.

## 7. C++ Rules

Project baseline:

- `gnu++26`
- no exceptions
- no RTTI

Preferred style:

- thin wrappers over ESP-IDF
- small classes
- explicit state
- light use of RAII when it improves resource safety

Core modules should stay conservative with heap use and keep hot paths simple.

## 8. Resource and State Management

When a component owns real resources, model that ownership clearly.

Good patterns:

- `begin()` acquires resources
- `end()` releases resources
- `ready()` reports availability
- `read()` and `write()` perform the expected transport action directly

For libraries in `framework/libs/esp_*` with optional runtime automation, use
this contract:

- `init(...)` configures the library in manual mode
- `start(...)` enables an internal task or background service
- `stop()` disables that task or service
- `end()` releases the full library state

Meaning:

- `init()` must not create hidden tasks
- `start()` is the explicit opt-in point for managed runtime
- `stop()` should preserve the configured library state when practical
- `end()` should return the instance to an uninitialized state

This rule exists to preserve predictability for advanced users while keeping
convenience available for rapid prototyping.

This is a recommended pattern for `esp_*` libraries, not a hard rule for the
core.

Core modules may use thinner and more domain-specific lifecycles such as:

- `begin()` / `end()`
- `connect()` / `disconnect()`
- `clean()`

if that produces a clearer wrapper over ESP-IDF.

For very simple peripherals, a global object with lightweight state is a good
fit.

## 9. Error Handling

Use `esp_err_t` consistently in public APIs.

Preferred direction:

- return `esp_err_t` for operations that can fail
- use `ESP_ERROR_CHECK()` in places that should stop immediately on failure
- keep success paths short
- keep failure paths easy to trace

## 10. Logging

Each component should define one fixed tag.

Examples:

- `ESP32LIBFUN_GPIO`
- `ESP_PCA9685`

Use standard ESP-IDF logging:

- `ESP_LOGI`
- `ESP_LOGD`
- `ESP_LOGW`
- `ESP_LOGE`

Messages should be short, actionable, and aligned with the public API.

## 11. Build Integration

Each component should define its dependencies clearly in `CMakeLists.txt`.

Use:

- `REQUIRES` for public dependency propagation
- `PRIV_REQUIRES` for implementation-only dependencies

For higher-level libraries:

- depend on `esp32libfun_i2c`, `esp32libfun_gpio`, `esp32libfun_serial`, or other specific core modules as needed
- do not depend on `esp32libfun` unless the library is intentionally an application-facing convenience wrapper

When a module is optional, connect it to:

- `Kconfig`
- `sdkconfig.defaults`
- conditional `CMakeLists.txt` logic

## 12. `main.cpp`

`main.cpp` should stay direct and readable.

Preferred qualities:

- small setup section
- clear hardware constants
- obvious control flow
- short validation or demo loops

For ESP-IDF applications written in C++, use:

```cpp
extern "C" void app_main(void)
{
}
```

## 13. Example Shape

Examples should look like real application code.

A good example:

- compiles as written
- uses the public API only
- uses realistic pins and addresses
- demonstrates one clear outcome

## 14. Evolution

Start each component with the smallest useful API.

Then grow by adding:

- validated features
- hardware-backed improvements
- new methods that keep the same naming logic

Growth should preserve the original feel of the module.

## 15. Author

Rodrigo Lange - 2026-03-31
