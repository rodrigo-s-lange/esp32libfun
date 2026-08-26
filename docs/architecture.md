# esp32libfun Architecture

## Purpose

`esp32libfun` is a HAL-oriented C++ core toolkit on top of ESP-IDF 6.0 focused on:

- short code
- fast comprehension
- practical abstractions
- direct integration with real ESP-IDF components

Most core modules are direct-call wrappers over ESP-IDF peripherals. The
application calls the module APIs and keeps control of the flow. The optional
AT subsystem is the plugin-style framework layer: commands are registered ahead
of time and dispatched by the AT console when input arrives.

The project is designed to be pleasant for humans and fast for LLMs to read.
This matters because the expected workflow is highly iterative, conversational,
and often assisted by AI during prototyping and implementation.

## Developer Experience

The main experience target is:

- a technical developer who knows the SoC and the attached hardware
- quick iteration in `main.cpp`
- predictable code generation by LLMs
- low-friction migration back to raw ESP-IDF when needed

Good framework code should be:

- easy to scan in one pass
- obvious in naming
- explicit in state changes
- short in the common path

## Design Principle

`esp32libfun` treats human readability and LLM readability as first-class design
constraints.

This is part of the intended development model of the project:

- fast prototyping
- conversational iteration
- "vibe coding" workflows
- direct editing with AI assistance

This principle should appear in the structure of the codebase itself:

- stable names
- predictable component layout
- compact public headers
- examples that match the real API exactly
- low ambiguity in public interfaces

## Layering

The project is organized in layers with clear naming:

- core modules in this repository use the `esp32libfun_*` prefix
- external device libraries and higher-level components use the `esp_*` prefix
- most core modules are direct-call HAL wrappers
- `esp32libfun_at` provides the optional inversion-of-control/plugin layer

Examples:

- core: `esp32libfun_serial`
- core: `esp32libfun_i2c`
- core: `esp32libfun_at`
- device library: `esp_example_sensor`

This split keeps the framework core small and stable while allowing reusable
device libraries to grow in their own repositories.

## Framework Boundary

The word "framework" in this repository describes the project structure,
conventions, and optional AT/plugin system. It does not mean every core module
uses inversion of control.

Direct-call modules include:

- `esp32libfun_gpio`
- `esp32libfun_i2c`
- `esp32libfun_spi`
- `esp32libfun_adc`
- `esp32libfun_gptimer`
- `esp32libfun_ledc`
- `esp32libfun_pcnt`
- `esp32libfun_mcpwm`
- `esp32libfun_serial`
- `esp32libfun_delay`
- `esp32libfun_rmt`
- `esp32libfun_twai`

In these modules, user code calls the wrapper API directly and owns the
application flow.

The AT layer is different:

- components register command handlers
- the AT console owns the parsing loop
- handlers are called later when matching commands arrive

That makes `esp32libfun_at` and the sidecar command files the actual
plugin-style framework part of the core.

## Core Role

The core exists to make ESP-IDF transports and base services easier to use.

Typical core responsibilities:

- serial
- gpio
- ledc
- pcnt
- mcpwm
- delay
- i2c
- adc
- gptimer
- twai
- rmt
- other transport or system-facing layers that many libraries depend on

Practical split inside the core:

- `esp32libfun_gpio` owns direct digital IO, level reads/writes, and simple pin interrupts
- `esp32libfun_ledc` owns general-purpose PWM output
- `esp32libfun_pcnt` owns low-level pulse counting
- `esp32libfun_mcpwm` owns dedicated motor/control PWM use cases

This keeps `gpio` readable and avoids one giant module that mixes unrelated
hardware blocks under one name.

The convenience component `esp32libfun` is the public entry point for the base
framework. It should:

- expose the framework version
- aggregate the public headers of enabled core modules
- initialize only the modules that truly benefit from centralized setup

It is a convenience layer, not a required dependency for higher-level
libraries.

Public references:

- `ESP32LIBFUN_VERSION`
- `esp32libfun_version()`
- `esp32libfun_init()`

## Device Libraries

Libraries above the core build on core transports and keep their own logic
local to the device domain.

Examples:

- sensor helpers
- actuator helpers
- display helpers
- connectivity helpers

Each external library should feel native to the framework:

- short API
- consistent naming
- explicit setup
- small header
- low boilerplate

Dependency rule:

- `esp_*` libraries should depend on the specific core modules they use
- `esp_*` libraries should not depend on the `esp32libfun` aggregator
- applications may use the aggregator for convenience or use core modules directly

## API Direction

The preferred API style is object-oriented C++ with a strong bias toward short,
readable calls.

Examples:

- `serial.println(...)`
- `i2c.begin(...)`
- `pca9685.duty(...)`

Good APIs in this project usually have these properties:

- one obvious name per action
- minimal ceremony
- explicit arguments
- predictable return values
- small amount of hidden state

## Runtime Model

The framework values predictable runtime behavior.

External `esp_*` libraries should work in a direct manual mode by default and
may offer an optional managed runtime mode when that improves convenience.

Preferred direction:

- `init(...)` prepares the library without starting its own task
- manual operation remains available after `init(...)`
- `start(...)` may enable a managed task or background service
- `stop()` disables that managed runtime layer while keeping the library usable
- `end()` releases the library resources completely

This model gives two valid usage styles:

- explicit control for applications that care about scheduling and determinism
- convenience for applications that want a ready-to-run background behavior

When a library starts its own task, that choice should be explicit in the API
and easy to discover from the header alone.

Core modules in `framework/core/esp32libfun_*` do not need to follow this
contract rigidly.

The core is allowed to use simpler or more domain-specific names such as:

- `begin(...)`
- `connect(...)`
- `disconnect(...)`
- `clean(...)`

The rule for the core is pragmatism, not symmetry for its own sake.

## Human and LLM Readability

The framework should optimize for two readers at the same time:

- a human scanning code quickly
- an LLM generating or modifying code from local context

That means:

- stable naming conventions
- minimal ambiguity between modules
- examples that match the real API exactly
- headers that expose the smallest useful surface
- documentation that explains the intended path directly

A good module should allow an LLM to infer usage from:

- the class name
- a few method names
- one short example

In practice, this means the framework should prefer:

- one obvious public path
- small method vocabulary
- direct examples over abstract explanation
- consistency across modules over clever variation

## C++ Direction

C++ is the language foundation of the framework.

The style direction is:

- thin wrappers over ESP-IDF
- small classes
- straightforward ownership
- low-overhead abstractions

Technical baseline:

- ESP-IDF v6.0.0+
- `gnu++26`
- no exceptions
- no RTTI

## Configuration

The framework supports modular build-time configuration.

The preferred configuration flow is:

- `Kconfig` defines the visible options
- `sdkconfig.defaults` defines project defaults
- `CMakeLists.txt` enables sources and dependencies conditionally

This allows the framework to stay small while keeping optional features easy to
turn on when needed.

## Initialization and Adapters

Some modules are useful as direct libraries.
Some modules are useful as optional adapters.

A good example is AT integration:

- the base library should work on its own
- AT support can be added as an optional layer
- the aggregator may start shared services for convenience
- direct users can still initialize only the pieces they need

This keeps the device logic clean and reusable.

The same idea applies to internal runtime behavior in higher-level libraries:

- direct operation should stay available
- managed runtime should be optional
- background tasks should never be surprising

Core modules may expose thinner wrappers when the ESP-IDF concept already has a
natural lifecycle of its own.

## Boards and Pins

Board-specific choices stay at application level.

The integrator is expected to decide:

- GPIO assignment
- peripheral routing
- pin conflicts
- board power topology

The framework focuses on good APIs and reusable components rather than a fixed
board model.

## Logging

Logging follows standard ESP-IDF logging and should remain easy to understand at
a glance.

Desired qualities:

- fixed tag per module
- meaningful messages
- easy correlation between public API calls and runtime behavior

`esp32libfun_serial` is the textual base of the framework and supports the
human-facing console experience.

## Growth Strategy

The project grows by validating small, useful modules first.

A module is ready to be considered part of the official experience when it is:

- functional
- small
- coherent
- validated on hardware
- pleasant to use from real application code

## Planned

- **OTA (over-the-air firmware update).** Not implemented yet: no
  `esp32libfun_ota` module exists, and the partition table is currently
  single-app (no `ota_0`/`ota_1`/`otadata` slots). Whether OTA belongs in
  the core as a generic module or lives entirely in application code above
  it is still undecided; resolve that before starting implementation.

## Version Reference

- current version: `v0.1.2`
- next milestone: `v1.0.0`
- intended stable milestone: `v1.0.0`

## Rodrigo Lange 2026-03-31
