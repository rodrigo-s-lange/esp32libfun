# Changelog

All notable changes to this framework are recorded here. Versions follow
[Semantic Versioning](https://semver.org/). Component-level version macros
(`ESP32LIBFUN_<MODULE>_VERSION`) are bumped only for the modules a release
actually changes; the aggregate `ESP32LIBFUN_VERSION` tracks the framework
release itself.

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

[v0.1.1]: https://github.com/rodrigo-s-lange/esp32libfun/releases/tag/v0.1.1
[v0.1.0]: https://github.com/rodrigo-s-lange/esp32libfun/releases/tag/v0.1.0
