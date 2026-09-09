#pragma once

#include "scene_codec.hpp"
#include "nvs.h"

namespace bench {

/// Application-owned A/B NVS journal. Single caller; keep off task stacks.
class SceneStore {
public:
    /// Opens NVS without erasing it, then loads the newest compatible valid slot.
    /// @param scene Receives the saved scene; unchanged if neither slot is valid.
    /// @return ESP_OK, ESP_ERR_NOT_FOUND for first boot, or an NVS/data error.
    esp_err_t load(esp_pixels_composer::AtScene &scene);

    /// Saves to the inactive slot, commits and reads back before acknowledging.
    /// Identical configuration is a no-op. Never writes the last valid slot.
    /// @param scene Confirmed configuration to retain.
    /// @return ESP_OK only after verification, or a storage/validation error.
    esp_err_t save(const esp_pixels_composer::AtScene &scene);

    /// Adapter for attachAtPersistence().
    /// @param scene Confirmed scene from the sidecar.
    /// @param context Non-null SceneStore pointer.
    /// @return The store's save result.
    static esp_err_t saveCallback(const esp_pixels_composer::AtScene &scene, void *context);

private:
    nvs_handle_t handle_ = 0;
    esp_err_t init_error_ = ESP_ERR_INVALID_STATE;
    int active_slot_ = -1;
    uint64_t sequence_ = 0;
    SceneRecord cached_;
    SceneRecord scratch_;
    SceneRecord verify_;
};

} // namespace bench
