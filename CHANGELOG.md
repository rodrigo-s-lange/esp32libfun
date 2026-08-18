# Changelog

All notable changes to this framework are recorded here.

**Before `v1.0.0` the framework stays on the `v0.1.x` line and only the patch
number advances.** Semantic Versioning treats `0.x` as explicitly unstable —
the public API carries no compatibility promise until the first stable
release — so the minor number is reserved rather than spent. `v1.0.0` is the
intended stable milestone (see `docs/architecture.md`); normal semver rules
begin there.

The framework releases as one unit: `ESP32LIBFUN_VERSION` and every
component macro (`ESP32LIBFUN_<MODULE>_VERSION`) carry the same version and
move together on each release, so a build can be identified from any single
header without cross-referencing which module changed.

## [v0.1.2] - 2026-08-18

The AT console gains public API. Nothing existing changed shape, so upgrading
from v0.1.1 requires no code changes.

### Added

- `esp32libfun_at`: semantic response helpers alongside the existing
  `writeLine` and `writeError` — `writeOk` (green, defaults to `"OK"`),
  `writeWarn` (yellow, a non-fatal caveat) and `writeAlert` (orange: accepted,
  but on the verge of being a mistake, such as a value that yields a
  degenerate result). Command handlers can now express outcome by intent
  rather than by hand-colouring each line.

### Fixed

- `esp32libfun_adc`: clock source selection now compiles across SoCs.
  `adc_oneshot_clk_src_t` aliases a different enum depending on whether the
  chip drives ADC oneshot through the RTC controller (ESP32/S2/S3) or only
  through the digital controller (ESP32-C3/C6), and each variant declares only
  its own `*_CLK_SRC_DEFAULT`. The selection is now guarded by
  `SOC_ADC_RTC_CTRL_SUPPORTED`.

### Changed

- Agent-facing documentation consolidated: `docs/vibe_coding.md` becomes
  required reading in `AGENTS.md` and `docs/rag-guide.md` instead of an
  optional template, and both documents were shortened.
- `.gitignore` now excludes locally validated `esp_*` libraries under
  `framework/libs/` and lab applications under `examples/basic/`, keeping the
  component template and the reference example tracked. Each `esp_*` library
  keeps its own repository as source of truth.

Validated on hardware: ESP32-C3 with AT enabled, and classic ESP32 in an
isolated environment.

## [v0.1.1] - 2026-08-14

### Fixed

- `esp32libfun_mcpwm`: `REQUIRES` no longer depends on Kconfig.

  ESP-IDF resolves `REQUIRES` in an early pass, before Kconfig is loaded, so
  conditioning it on `CONFIG_ESP32LIBFUN_MCPWM` silently dropped
  `esp_driver_gpio` and `esp_driver_mcpwm` whenever the real implementation was
  selected. The failure was invisible on ESP32-C3, where MCPWM is unsupported
  and the stub is always taken, and a hard `driver/gpio.h: No such file` on
  classic ESP32, where MCPWM is supported and the real source compiles.

  Only `SRCS`, evaluated later once Kconfig is available, is safe to make
  conditional. This is the same class of bug previously fixed in
  `esp32libfun_at`.

  Verified on hardware: ESP32 (WROOM-32) builds, and the `esp_pixels` hardware
  gates pass on that target. ESP32-C3 is unaffected — it still takes the stub
  branch.

**Anyone targeting classic ESP32 should use this release.** `v0.1.0` does not
build on that target.

## [v0.1.0] - 2026-07-07

First tagged release: core HAL components, the optional AT layer, examples and
architecture documentation. Version metadata established across the framework
and its components.

[v0.1.2]: https://github.com/rodrigo-s-lange/esp32libfun/releases/tag/v0.1.2
[v0.1.1]: https://github.com/rodrigo-s-lange/esp32libfun/releases/tag/v0.1.1
[v0.1.0]: https://github.com/rodrigo-s-lange/esp32libfun/releases/tag/v0.1.0
