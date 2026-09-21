#pragma once

#include "map_config.hpp"
#include "map_store.hpp"
#include "output_config.hpp"
#include "output_store.hpp"
#include "scene_store.hpp"

namespace bench {

/// Registers (or unregisters) the AT+MAP?/AT+MAPSET/AT+MAPCLR/AT+MAPSAVE
/// commands under library "PIXELS_MAP" -- the device's own 2D canvas/ledmap
/// configuration is application/device configuration, entirely separate
/// from effect segments and from the transport-neutral Composer (which owns
/// `setLedmap()` itself, but has no AT command for it -- installing a real
/// ledmap is board-wiring policy, owned here, one layer up).
///
/// `AT+MAPSET`/`AT+MAPCLR` only ever edit a staged candidate map -- never
/// the active one, and never the current scene, in either direction. Both a
/// staged `AT+MAPSET` candidate's own bounds check and the final
/// `AT+MAPSAVE` both validate against whatever output topology is
/// CURRENTLY durably saved for the next boot (`output_store.current()`,
/// never an unsaved staged `AT+OUTSET` candidate) -- the topology that will
/// actually be active the next time this map takes effect. `AT+MAPSAVE`
/// additionally rejects a candidate incompatible with the durable nonempty
/// current scene, then persists the map for the **next explicit reboot
/// only** -- this library never live-replaces the running
/// `Composer::setLedmap()` canvas (which cannot change after segments exist
/// anyway, see its own doc). Explicitly clear/publish the scene before
/// removing its map. Later scene saves are also checked against the saved
/// next-boot topology AND map together (`SceneStore::nextBoot()`).
/// `AT+MAP?` distinguishes active, saved and staged configurations, plus
/// each one's own global logical range.
///
/// @param active The canvas/ledmap actually installed right now (read-only
///        from here on; this library never changes it, and the staged
///        candidate starts as a copy of it).
/// @param store Already-usable `MapStore` this library saves through.
/// @param scene_store Current-scene journal used to reject a destructive map change.
/// @param output_store Output-topology journal, read-only from here (via
///        `current()`) both for `AT+MAPSET`'s own bounds check and for the
///        combined-with-topology `AT+MAPSAVE` compatibility guard -- this
///        library never saves through it.
/// @param enable true to register, false to unregister.
/// @return ESP_OK on success (idempotent while already bound to the same
///         store). ESP_ERR_INVALID_STATE if a different store already
///         holds the binding, or `esp32libfun::at` is not initialized. A
///         registration error otherwise, with every partial registration
///         rolled back before returning.
esp_err_t mapAt(const MapConfig &active, MapStore &store, SceneStore &scene_store, OutputStore &output_store,
                bool enable);

} // namespace bench
