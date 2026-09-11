#pragma once

#include "esp_pixels_composer.hpp"
#include "preset_store.hpp"

namespace bench {

/// Registers (or unregisters) the AT+PRESETS?/AT+PRESETSAVE/AT+PRESETLOAD/
/// AT+PRESETDEL commands under library "PIXELS_PRESETS", bound to one
/// Composer instance and one PresetStore -- an explicit application-owned
/// AT library, separate from the Composer's own "PIXELS_COMPOSER" sidecar
/// (see `AGENTS.md`: application concerns, including preset naming/
/// capacity/storage, do not belong in the core or the Composer engine).
/// Mirrors `Composer::at(bool)`'s own single-binding-at-a-time convention:
/// only one (composer, store) pair may hold this binding.
///
/// `AT+PRESETSAVE=<name>` exports the Composer's own confirmed scene (see
/// `esp_pixels_composer::exportConfirmedScene()`) and saves it via `store`.
/// `AT+PRESETLOAD=<name>` loads and fully validates the named preset, then
/// applies it via `esp_pixels_composer::loadConfirmedScene()` -- a runtime
/// operation, never `attachAtPersistence()` (a startup-only helper).
/// `AT+PRESETS?` lists occupied positions; `AT+PRESETDEL=<name>` removes
/// one. Neither query nor delete ever changes the active scene; delete
/// never touches the independently retained current scene either.
///
/// @param composer Already-bound instance (`composer.at(true)` already
///        succeeded) whose confirmed scene this library saves/loads.
/// @param store Already-`begin()`'d preset store; must outlive the binding.
/// @param enable true to register, false to unregister.
/// @return ESP_OK on success (idempotent while already bound to the same
///         pair). ESP_ERR_INVALID_STATE if a different pair already holds
///         the binding, or `esp32libfun::at` is not initialized. A
///         registration error otherwise, with every partial registration
///         rolled back before returning.
esp_err_t presetsAt(esp_pixels_composer::Composer &composer, PresetStore &store, bool enable);

} // namespace bench
