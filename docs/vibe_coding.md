# esp32libfun Vibe Coding

This document exists to make AI-assisted work inside `esp32libfun` more
predictable.

The goal is not to replace the project docs. The goal is to give a fast prompt
surface for humans and LLMs that need to start useful work quickly.

Read these files first:

- `README.md`
- `docs/architecture.md`
- `docs/style-guide.md`
- `AGENTS.md`

## When To Use This Doc

Use this document when you want to:

- ask an AI to create an application inside this repository
- ask an AI to add a new `esp_*` library
- ask an AI to refactor code without breaking the project architecture

## Project Mental Model

Keep this model explicit in the prompt:

- `framework/core/esp32libfun_*` = thin core modules over ESP-IDF
- `framework/libs/esp_*` = device and higher-level libraries
- the core stays small and pragmatic
- `esp_*` libraries build on the core, not on the `esp32libfun` aggregator
- `esp_component_template` is the starting point for new `esp_*` libraries

Important rules:

- prefer short, readable wrappers
- do not invent large subsystems without strong justification
- preserve the naming and structure already present in the repo
- check the existing core modules and libraries before creating new code
- reuse an existing library as a behavioral reference when the transport matches
- ESP-IDF 6.0 is the baseline
- no exceptions
- no RTTI
- avoid heap in the core
- do not edit `sdkconfig` as the first choice
- prefer `sdkconfig.defaults` when changing repository defaults

## Prompt Template: Create An Application

Use this when the goal is to build or modify application code in `main/`.

```text
You are working inside the esp32libfun repository.

Before proposing or editing code, read these files:
- README.md
- docs/architecture.md
- docs/style-guide.md
- AGENTS.md

Project model:
- `framework/core/esp32libfun_*` = thin core modules over ESP-IDF
- `framework/libs/esp_*` = device and higher-level libraries
- prefer short, pragmatic wrappers
- preserve the style and naming already used in the project
- ESP-IDF 6.0 is the baseline
- no exceptions, no RTTI
- avoid heap in the core

Architecture rules:
- use the core directly when the need is simple
- use `esp_*` when device behavior deserves its own library
- `esp_*` must not depend on the `esp32libfun` aggregator as a required dependency
- for predictable behavior, prefer manual control before hidden automation
- before adding code, inspect the existing core modules and prefer reusing them
- do not patch `sdkconfig` unless the task explicitly requires a local machine-specific change

Task:
[describe the application here]

Hardware:
- target: [example: ESP32-C3]
- pins: [list]
- connected peripherals: [list]
- transport: [GPIO/I2C/SPI/UART/etc]

Expected result:
- [describe the final behavior]
- [describe logs, callbacks, endpoints, commands, etc]

Restrictions:
- [example: edit main/main.cpp only]
- [example: no Wi-Fi]
- [example: no internal task]
- [example: use esp_button]
- [example: use manual polling]

When answering:
- first summarize the architecture you understood from the repository
- then propose the smallest correct path
- follow existing project patterns instead of inventing new ones
```

## Prompt Template: Create A New Library

Use this when the goal is to create a new component in `framework/libs/`.

```text
You are working inside the esp32libfun repository.

Before proposing or editing code, read these files:
- README.md
- docs/architecture.md
- docs/style-guide.md
- AGENTS.md
- framework/libs/esp_component_template/README.md
- framework/libs/esp_component_template/include/esp_component_template.hpp
- framework/libs/esp_component_template/esp_component_template.cpp
- at least one existing library that uses the same transport or runtime pattern

Project model:
- `framework/core/esp32libfun_*` = thin core modules over ESP-IDF
- `framework/libs/esp_*` = device and higher-level libraries
- new libraries belong in `framework/libs/esp_*`
- the core must stay small and stable

Rules for the new library:
- start from `esp_component_template`
- inspect an existing library that uses the same core dependency before designing transport ownership
- rename namespace, class, header, source, callback alias, and global object correctly
- keep the public header small and documented with Doxygen comments
- depend only on the specific core modules that the library actually uses
- do not depend on the `esp32libfun` aggregator as a required dependency
- do not make the library own shared transports unless the existing project pattern already does that
- if the library has manual and managed modes, prefer `init()/start()/stop()/end()`
- if managed runtime does not help, keep the API direct and pragmatic
- AT support must stay optional
- do not edit `sdkconfig` as part of normal library creation

New library:
- name: [example: esp_bmp280]
- purpose: [describe]
- hardware: [describe]
- transport: [I2C/SPI/UART/GPIO/etc]
- core dependencies: [list]

Desired API:
- [list the methods you want]
- [say whether you want a global object]
- [say whether you want callbacks, polling, optional task, etc]

Expected result:
- component compiling
- correct CMake integration
- coherent header and source
- one small usage example
- no architecture regression

When answering:
- first say which layer the library belongs to and why
- then propose the public API
- then implement it from the existing template
- if there is doubt between core and `esp_*`, prefer `esp_*`
```

## Prompt Template: Refactor Without Breaking Architecture

Use this when the repository already has code, but the structure or naming needs
to improve.

```text
You are working inside the esp32libfun repository.

Before proposing or editing code, read these files:
- README.md
- docs/architecture.md
- docs/style-guide.md
- AGENTS.md

Task:
[describe the refactor here]

Constraints:
- preserve the project architecture
- preserve public behavior unless the change explicitly updates the API
- do not move code into the core unless it clearly belongs there
- prefer smaller and more predictable naming
- keep examples and docs aligned with the real API

When answering:
- first explain what layer the code belongs to now
- then explain the minimal safe refactor
- then update docs if the change affects project conventions
```

## Good Inputs For Any AI

A good prompt should always include:

- target chip
- pins
- connected hardware
- expected behavior
- runtime constraints
- whether the change belongs in `main/`, the core, or `framework/libs/`

Good prompts reduce wrong assumptions and make the generated code closer to the
real project style.
