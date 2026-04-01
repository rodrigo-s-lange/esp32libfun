# esp_component_template

Reference component for new `esp_*` libraries.

What it demonstrates:

- `init()` for manual mode
- `start()` and `stop()` for optional managed runtime
- `end()` for full teardown
- fixed resources with `xTaskCreateStaticPinnedToCore()`
- short public header with predictable naming
- Doxygen-ready public API comments
- explicit `TODO rename` markers in the files that need attention first

Typical workflow to create a new library from this template:

1. Copy `framework/libs/esp_component_template` to `framework/libs/esp_yourlib`
2. Rename these items first:
   - folder name
   - `esp_component_template.hpp`
   - `esp_component_template.cpp`
   - `namespace esp_component_template`
   - `class Template`
   - `template_callback_t`
   - global object `templ`
3. Before changing transport logic, read one existing library that uses the same kind of core dependency
   - for I2C devices, read `framework/libs/esp_pca9685`
   - for button-like runtime behavior, read `framework/libs/esp_button`
4. Replace the example step/callback logic with your real device or service logic
5. Keep the lifecycle contract only if the library really benefits from it
6. Add optional `*_at.cpp` integration only if the library exposes AT commands

Files worth checking first:

- `include/esp_component_template.hpp`
- `esp_component_template.cpp`
- `framework/libs/esp_pca9685`
- `framework/libs/esp_button`
- `docs/examples/esp_component_template_manual.cpp`
- `docs/examples/esp_component_template_managed.cpp`
