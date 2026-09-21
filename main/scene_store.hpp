#pragma once

#include "scene_codec.hpp"
#include "nvs.h"

namespace bench {

/// Application-owned A/B NVS journal. Single caller; keep off task stacks.
class SceneStore {
public:
    /// Checks whether the durable current scene can survive an output
    /// configuration and/or canvas/map change together.
    /// @param topology Proposed boot topology.
    /// @param map Proposed boot canvas/ledmap.
    /// @return ESP_OK for a compatible scene or an explicitly empty/absent scene; error otherwise.
    esp_err_t canRestoreOn(const Topology &topology, const MapConfig &map) const;
    /// Guards subsequent scene saves against an output/map change already
    /// saved for reboot -- called with the FULL combined pair every time
    /// EITHER dimension changes (e.g. `outputsAt()`'s own `AT+OUTSAVE`
    /// passes its own new topology alongside whatever `MapStore::current()`
    /// already reports, and vice versa for `mapAt()`'s `AT+MAPSAVE`) so
    /// this store's own guard always reflects the true combined next-boot
    /// state, regardless of which command last changed which half.
    /// @param topology Durably saved next-boot output configuration.
    /// @param map Durably saved next-boot canvas/ledmap.
    void nextBoot(const Topology &topology, const MapConfig &map)
    {
        next_topology_ = topology;
        next_map_ = map;
        next_set_ = true;
    }
    /// Opens NVS without erasing it, then loads the newest COMPATIBLE valid
    /// slot against `topology`/`map` (see `decodeScene()`'s own
    /// compatibility rule) -- an incompatible-but-structurally-valid record
    /// is skipped, same as a corrupt one, falling back to the other slot if
    /// it qualifies; the stored record itself is never modified either way.
    /// @param topology The device's own active topology right now.
    /// @param map The device's own active canvas/ledmap right now.
    /// @param scene Receives the saved scene; unchanged if neither slot
    ///        qualifies.
    /// @return ESP_OK, ESP_ERR_NOT_FOUND for first boot (or when neither
    ///         slot is compatible with `topology`/`map`), or an NVS/data error.
    esp_err_t load(const Topology &topology, const MapConfig &map, esp_pixels_composer::AtScene &scene);

    /// Saves to the inactive slot, commits and reads back before
    /// acknowledging. Identical configuration is a no-op. Never writes the
    /// last valid slot.
    /// @param topology The active topology to save this scene against (see
    ///        `encodeScene()`'s own doc) -- must match the topology `load()`
    ///        most recently succeeded or was called with; saving against a
    ///        DIFFERENT topology than the one currently active is the
    ///        caller's own responsibility to avoid (this store does not
    ///        cache or re-check it independently).
    /// @param map The active canvas/ledmap to save this scene against.
    /// @param scene Confirmed configuration to retain.
    /// @return ESP_OK only after verification, or a storage/validation
    ///         error. ESP_ERR_INVALID_STATE if a next-boot output/map
    ///         change has been saved (`nextBoot()`) and `scene` is
    ///         non-empty but would not survive it -- reboot to apply the
    ///         saved change first, or explicitly clear/publish the scene.
    esp_err_t save(const Topology &topology, const MapConfig &map, const esp_pixels_composer::AtScene &scene);

    /// Adapter for attachAtPersistence(): saves against whichever
    /// topology/map `bindTopology()`/`bindMap()` last recorded.
    /// @param scene Confirmed scene from the sidecar.
    /// @param context Non-null SceneStore pointer.
    /// @return The store's save result.
    static esp_err_t saveCallback(const esp_pixels_composer::AtScene &scene, void *context);

    /// Records the topology `saveCallback()` should save against from here
    /// on -- call once, right after `load()`, before attaching the AT
    /// persistence callback (main.cpp's own boot sequence). Never touches
    /// storage itself.
    /// @param topology The now-active topology.
    void bindTopology(const Topology &topology) { bound_topology_ = topology; }
    /// Records the canvas/ledmap `saveCallback()` should save against from
    /// here on -- call once, right after `load()`, alongside
    /// `bindTopology()`. Never touches storage itself.
    /// @param map The now-active canvas/ledmap.
    void bindMap(const MapConfig &map) { bound_map_ = map; }

private:
    nvs_handle_t handle_ = 0;
    esp_err_t init_error_ = ESP_ERR_INVALID_STATE;
    int active_slot_ = -1;
    uint64_t sequence_ = 0;
    Topology bound_topology_ = defaultTopology();
    MapConfig bound_map_{};
    Topology next_topology_ = defaultTopology();
    MapConfig next_map_{};
    bool next_set_ = false;
    SceneRecord cached_;
    SceneRecord scratch_;
    SceneRecord verify_;
};

} // namespace bench
