#pragma once

#include "output_config.hpp"
#include "output_store.hpp"
#include "scene_store.hpp"

namespace bench {

/// Registers (or unregisters) the AT+OUT?/AT+OUTSET/AT+OUTDEL/AT+OUTSAVE
/// commands under library "PIXELS_OUTPUTS" -- output pins, per-output pixel
/// counts and wire color order are application/device configuration,
/// entirely separate from effect segments (`AGENTS.md`: physical-pin/board
/// policy stays at the application level, out of the core and the
/// transport-neutral Composer). `AT+OUTSET`/`AT+OUTDEL` only ever edit a
/// staged candidate topology -- never the active one, and never the
/// current scene, in either direction. `AT+OUTSAVE` validates the WHOLE
/// staged candidate and persists it for the **next explicit reboot only**
/// -- this cut never live-replaces the running adapter/Composer topology,
/// and rejects a candidate incompatible with the durable nonempty current
/// scene. Explicitly clear/publish the scene before removing its topology.
/// Later scene saves are also checked against the saved next-boot topology.
/// `AT+OUT?` distinguishes active, saved and staged configurations.
///
/// @param active The topology actually driving the adapter right now
///        (read-only from here on; this library never changes it, and the
///        staged candidate starts as a copy of it).
/// @param store Already-usable `OutputStore` this library saves through.
/// @param scene_store Current-scene journal used to reject a destructive topology change.
/// @param enable true to register, false to unregister.
/// @return ESP_OK on success (idempotent while already bound to the same
///         store). ESP_ERR_INVALID_STATE if a different store already
///         holds the binding, or `esp32libfun::at` is not initialized. A
///         registration error otherwise, with every partial registration
///         rolled back before returning.
esp_err_t outputsAt(const Topology &active, OutputStore &store, SceneStore &scene_store, bool enable);

} // namespace bench
