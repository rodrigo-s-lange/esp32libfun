# esp32libfun Architecture

## Purpose

`esp32libfun` is a framework on top of ESP-IDF 6.0 focused on:

- short code
- fast comprehension
- practical abstractions
- direct integration with real ESP-IDF components

The framework is designed to be pleasant for humans and fast for LLMs to read.
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

- core modules use the `esp32libfun_*` prefix
- device libraries and higher-level components use the `esp_*` prefix

Examples:

- core: `esp32libfun_serial`
- core: `esp32libfun_i2c`
- core: `esp32libfun_at`
- device library: `esp_pca9685`

This split keeps the framework core small and stable while allowing reusable
device libraries to grow on top of it.

## Core Role

The core exists to make ESP-IDF transports and base services easier to use.

Typical core responsibilities:

- serial
- gpio
- delay
- i2c
- other transport or system-facing layers that many libraries depend on

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

- `esp_pca9685`
- future sensor, actuator, display, and connectivity helpers

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

## Version Reference

- current version: `v0.0.0`
- next milestone: `v0.1.0`
- intended stable milestone: `v1.0.0`

## Rodrigo Lange 2026-03-31
