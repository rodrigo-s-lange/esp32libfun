#pragma once

#include "preset_codec.hpp"
#include "nvs.h"

namespace bench {

constexpr unsigned kPresetSlotCount = 100;

/// One occupied position's lightweight metadata, as reported by `list()`.
struct PresetInfo {
    char name[kPresetNameMaxLen + 1] = {}; ///< nul-terminated
    uint8_t segment_count = 0;
};

/// Invoked once per occupied position, in slot order, by `list()`. Never
/// invoked reentrantly into `PresetStore` itself.
using PresetInfoVisitor = void (*)(void *context, const PresetInfo &info);

/// Application-owned, 100-position named preset journal (`PIXELS_PRESETS`
/// AT library) -- entirely separate storage from the current-scene A/B
/// journal (`SceneStore`) and its NVS partition: presets and the current
/// scene never share a key, a partition, or a failure. Each of the 100
/// logical positions has its own independent A/B key pair, the exact same
/// torn-write/newest-valid-survives discipline `SceneStore` already uses,
/// so replacing one position never risks another's data. Single caller;
/// keep instances off task stacks (`Slot slots_[100]` plus two
/// `PresetRecord` scratch buffers is several KB).
class PresetStore {
public:
    /// Opens the dedicated "presets" NVS partition/namespace and scans all
    /// 100 positions for each one's newest valid record, without erasing
    /// anything -- an empty/never-used partition is not an error, and a
    /// position with no valid record in either key is simply not occupied
    /// (an empty position need not have ever been written at all).
    /// Deliberately no erase/retry on a partition/version error, same
    /// policy `SceneStore::load()` already has.
    /// @return ESP_OK, or an NVS error opening the partition/namespace.
    esp_err_t begin();

    /// Invokes `fn(context, info)` once per occupied position, in slot
    /// order (never save order). Read-only; does not touch storage.
    /// @param fn Non-null visitor.
    /// @param context Opaque, passed through to every `fn` call.
    void list(PresetInfoVisitor fn, void *context) const;

    /// Saves `scene` under `name`, reusing `encodePreset()`. An existing
    /// name is replaced deliberately, at its own current slot, targeting
    /// the OTHER A/B key and only adopting it after a successful
    /// commit-then-read-back verification -- the prior valid record for
    /// that name is left completely untouched by any failure at or before
    /// that point. A name not already present claims the first free
    /// position (unoccupied slot). Never called from the render task.
    /// @param name Preset name.
    /// @param name_len Number of bytes in `name`.
    /// @param scene Confirmed scene to retain (see
    ///        `esp_pixels_composer::exportConfirmedScene()`).
    /// @return ESP_OK only once verified durable. ESP_ERR_INVALID_ARG for
    ///         an invalid name or scene. ESP_ERR_NO_MEM if all 100
    ///         positions are occupied by other names. An NVS error on
    ///         write/commit/verify failure -- the prior record (if any) is
    ///         unchanged either way.
    esp_err_t save(const char *name, size_t name_len, const esp_pixels_composer::AtScene &scene);

    /// Loads and fully validates the named preset directly from its active
    /// key; never mutates storage or this store's own cached metadata.
    /// @param name Preset name.
    /// @param name_len Number of bytes in `name`.
    /// @param scene Receives the decoded scene on success only.
    /// @return ESP_OK. ESP_ERR_NOT_FOUND if no occupied position matches
    ///         `name`. An invalid/corrupt/incompatible data error otherwise
    ///         (see `decodePreset()`) -- `scene` is left unchanged.
    esp_err_t load(const char *name, size_t name_len, esp_pixels_composer::AtScene &scene) const;

    /// Erases the named preset's position (both A/B keys) and marks it
    /// free. The independently retained current scene (`SceneStore`) is
    /// never touched by this call, in either direction.
    /// @param name Preset name.
    /// @param name_len Number of bytes in `name`.
    /// @return ESP_OK once erased. ESP_ERR_NOT_FOUND if no occupied
    ///         position matches `name` -- nothing changes. An NVS error if
    ///         erasing/committing fails; this store's own cached metadata
    ///         for that position is left unchanged so a concurrent `list()`/
    ///         `load()` never wrongly reports it as already gone.
    esp_err_t remove(const char *name, size_t name_len);

private:
    struct Slot {
        bool occupied = false;
        int active_ab = -1; ///< 0 or 1; -1 only while `!occupied`
        uint64_t sequence = 0;
        char name[kPresetNameMaxLen + 1] = {};
        uint8_t segment_count = 0;
    };

    nvs_handle_t handle_ = 0;
    esp_err_t init_error_ = ESP_ERR_INVALID_STATE;
    Slot slots_[kPresetSlotCount];
    // Reused across calls -- this store has exactly one caller, never the
    // render task, so no locking is needed for its own scratch buffers.
    // `mutable`: `load()` is logically read-only (never changes this
    // store's own state) but still needs a scratch decode buffer, the same
    // one `begin()`/`save()` already reuse.
    mutable PresetRecord scratch_;
    mutable PresetRecord verify_;

    int findByName(const char *name, size_t name_len) const;
    int findFreeSlot() const;
    static void slotKey(unsigned slot, unsigned ab, char out[6]);
};

} // namespace bench
