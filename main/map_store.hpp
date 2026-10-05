#pragma once

#include "map_config.hpp"
#include "nvs.h"
#include <cstddef>
#include <cstdint>

namespace bench {

/// Application-owned A/B NVS journal for the device's own 2D canvas/ledmap
/// configuration (`MapConfig`) -- entirely separate storage from the
/// current-scene (`SceneStore`), preset (`PresetStore`), and output-topology
/// (`OutputStore`) journals, though it shares the same default "nvs"
/// partition `SceneStore`/`OutputStore` already use (its own record is tiny
/// -- at most ~27 bytes -- so no partition layout change is needed for it
/// either). Same versioned, CRC-protected, torn-write/newest-valid-survives
/// A/B discipline every other store here already has. Schema 1 row-wiring
/// records remain readable; new saves use schema 2, which adds the stable
/// column-serpentine wiring code without changing record size.
///
/// Saving here takes effect on the **next explicit reboot only** -- this
/// store never live-replaces the currently active `Composer::setLedmap()`
/// canvas; see `map_at.hpp`'s own `AT+MAPSAVE` doc. Single caller; keep off
/// task stacks.
class MapStore {
public:
    static constexpr size_t kRecordBytes = 4 /*magic*/ + 2 /*schema*/ + 2 /*size*/ + 8 /*generation*/ +
        2 /*width*/ + 2 /*height*/ + 2 /*first*/ + 1 /*wiring*/ + 4 /*crc*/;

    struct Record {
        uint8_t bytes[kRecordBytes]{};
        size_t size = 0;
    };

    /// Opens NVS (the same default partition `SceneStore`/`OutputStore`
    /// already use, a separate namespace/handle) without erasing it, then
    /// loads the newest valid slot.
    /// @param map Receives the saved map; unchanged if neither slot is valid.
    /// @return ESP_OK, ESP_ERR_NOT_FOUND for first boot (or when neither
    ///         slot holds a structurally valid record -- the caller falls
    ///         back to the identity `MapConfig{}`), or an NVS/data error.
    esp_err_t load(MapConfig &map);

    /// Saves to the inactive slot, commits and reads back before
    /// acknowledging. Identical configuration is a no-op. Never writes the
    /// last valid slot. Validates the WHOLE candidate before touching
    /// anything; the previous valid configuration is preserved on any
    /// failure. Bounds-checking a real matrix against the device's own
    /// total pixel count is this call's own caller's responsibility (via
    /// `MapConfig::valid(total_pixels)`) -- this store only re-validates
    /// structurally (`width==0`, or `height>0` -- it has no topology of its
    /// own to check `first+width*height` against).
    /// @param map Candidate map.
    /// @return ESP_OK only after verification. ESP_ERR_INVALID_ARG if the
    ///         candidate is structurally invalid. An NVS error on
    ///         write/commit/verify failure -- the prior record (if any) is
    ///         unchanged either way.
    esp_err_t save(const MapConfig &map);

    /// The most recently loaded or successfully saved map this session --
    /// the identity `MapConfig{}` until the first successful `load()`/
    /// `save()`. Lets another AT library (`outputsAt()`'s own `AT+OUTSAVE`
    /// guard) validate a candidate against whatever map is ACTUALLY durably
    /// saved for the next boot, without needing its own duplicate tracking
    /// or a second `load()` call (which this store could not even serve
    /// twice -- see `load()`'s own doc).
    /// @return The current best-known map (never touches storage).
    const MapConfig &current() const { return current_; }

private:
    nvs_handle_t handle_ = 0;
    esp_err_t init_error_ = ESP_ERR_INVALID_STATE;
    int active_slot_ = -1;
    uint64_t sequence_ = 0;
    MapConfig current_{};
    Record cached_;
    Record scratch_;
    Record verify_;
};

} // namespace bench
