#pragma once

#include "output_config.hpp"
#include "nvs.h"
#include <cstddef>
#include <cstdint>

namespace bench {

/// Application-owned A/B NVS journal for the device's own output topology
/// (pins/counts/order) -- entirely separate storage from the current-scene
/// (`SceneStore`) and preset (`PresetStore`) journals, though it shares the
/// same default "nvs" partition `SceneStore` already uses (its own record
/// is tiny -- at most ~31 bytes -- so no partition layout change is needed
/// for it). Same versioned, CRC-protected, torn-write/newest-valid-survives
/// A/B discipline every other store here already has.
///
/// Saving here takes effect on the **next explicit reboot only** -- this
/// store never live-replaces the currently active adapter/topology; see
/// `outputs_at.hpp`'s own `AT+OUTSAVE` doc. Single caller; keep off task
/// stacks.
class OutputStore {
public:
    static constexpr size_t kRecordBytes = 4 /*magic*/ + 2 /*schema*/ + 2 /*size*/ + 8 /*generation*/ +
        1 /*count*/ + kMaxOutputs * 5 /*pin+count+order per slot*/ + 4 /*crc*/;

    struct Record {
        uint8_t bytes[kRecordBytes]{};
        size_t size = 0;
    };

    /// Opens NVS (the same default partition `SceneStore` already uses, a
    /// separate namespace/handle) without erasing it, then loads the newest
    /// valid slot.
    /// @param topology Receives the saved topology; unchanged if neither
    ///        slot is valid.
    /// @return ESP_OK, ESP_ERR_NOT_FOUND for first boot (or when neither
    ///         slot holds a structurally valid record -- the caller falls
    ///         back to `defaultTopology()`), or an NVS/data error.
    esp_err_t load(Topology &topology);

    /// Saves to the inactive slot, commits and reads back before
    /// acknowledging. Identical configuration is a no-op. Never writes the
    /// last valid slot. Validates the WHOLE candidate before touching
    /// anything; the previous valid configuration is preserved on any
    /// failure.
    /// @param topology Candidate topology; must satisfy `topology.valid()`.
    /// @return ESP_OK only after verification. ESP_ERR_INVALID_ARG if
    ///         `!topology.valid()`. An NVS error on write/commit/verify
    ///         failure -- the prior record (if any) is unchanged either way.
    esp_err_t save(const Topology &topology);

    /// The most recently loaded or successfully saved topology this
    /// session -- `defaultTopology()` until the first successful `load()`/
    /// `save()`. Lets another AT library (`mapAt()`'s own `AT+MAPSAVE`
    /// guard) validate a candidate against whatever output topology is
    /// ACTUALLY durably saved for the next boot, without needing its own
    /// duplicate tracking or a second `load()` call (which this store
    /// could not even serve twice -- see `load()`'s own doc).
    /// @return The current best-known topology (never touches storage).
    const Topology &current() const { return current_; }

private:
    nvs_handle_t handle_ = 0;
    esp_err_t init_error_ = ESP_ERR_INVALID_STATE;
    int active_slot_ = -1;
    uint64_t sequence_ = 0;
    Topology current_ = defaultTopology();
    Record cached_;
    Record scratch_;
    Record verify_;
};

} // namespace bench
