#include "scene_store.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include <cstring>

namespace bench {
namespace {
constexpr const char *kKeys[] = {"scene_a", "scene_b"};
constexpr const char *kTag = "SCENE_NVS";
}

esp_err_t SceneStore::load(esp_pixels_composer::AtScene &scene)
{
    if (handle_ != 0) { return ESP_ERR_INVALID_STATE; }
    init_error_ = nvs_flash_init();
    // Deliberately no erase/retry on NO_FREE_PAGES or NEW_VERSION_FOUND.
    if (init_error_ != ESP_OK) { return init_error_; }
    init_error_ = nvs_open("pixels_scene", NVS_READWRITE, &handle_);
    if (init_error_ != ESP_OK) { return init_error_; }
    esp_err_t last_error = ESP_ERR_NOT_FOUND;
    for (int slot = 0; slot < 2; ++slot) {
        scratch_.size = sizeof(scratch_.bytes);
        esp_err_t err = nvs_get_blob(handle_, kKeys[slot], scratch_.bytes, &scratch_.size);
        if (err == ESP_ERR_NVS_NOT_FOUND) { continue; }
        if (err != ESP_OK) {
            last_error = err;
            ESP_LOGW(kTag, "slot %d unreadable: %s", slot, esp_err_to_name(err));
            continue;
        }
        // Only skip an older record after we have already recovered a valid one.
        uint64_t candidate_sequence = 0;
        if (scratch_.size >= kSceneHeaderBytes && scratch_.size <= sizeof(scratch_.bytes)) {
            for (unsigned i = 0; i < 8; ++i) {
                candidate_sequence |= static_cast<uint64_t>(scratch_.bytes[8 + i]) << (8 * i);
            }
        }
        if (active_slot_ >= 0 && candidate_sequence <= sequence_) { continue; }
        err = decodeScene(scratch_, scene, candidate_sequence);
        if (err != ESP_OK) {
            last_error = err;
            ESP_LOGW(kTag, "slot %d rejected: %s", slot, esp_err_to_name(err));
            continue;
        }
        sequence_ = candidate_sequence;
        active_slot_ = slot;
        cached_ = scratch_;
    }
    if (active_slot_ < 0) { return last_error; }
    ESP_LOGI(kTag, "restored slot=%d generation=%llu segments=%u paused=%u",
             active_slot_, static_cast<unsigned long long>(sequence_), scene.count, scene.paused);
    return ESP_OK;
}

esp_err_t SceneStore::save(const esp_pixels_composer::AtScene &scene)
{
    if (init_error_ != ESP_OK) { return init_error_; }
    if (handle_ == 0 || sequence_ == UINT64_MAX) { return ESP_ERR_INVALID_STATE; }
    esp_err_t err = encodeScene(scene, sequence_ + 1, scratch_);
    if (err != ESP_OK) { return err; }
    // Compare canonical bytes excluding generation and CRC. No padding/ABI bytes.
    if (active_slot_ >= 0 && cached_.size == scratch_.size &&
        std::memcmp(cached_.bytes, scratch_.bytes, 8) == 0 &&
        std::memcmp(cached_.bytes + 16, scratch_.bytes + 16, scratch_.size - 20) == 0) {
        return ESP_OK;
    }
    const int target = active_slot_ == 0 ? 1 : 0;
    err = nvs_set_blob(handle_, kKeys[target], scratch_.bytes, scratch_.size);
    if (err == ESP_OK) { err = nvs_commit(handle_); }
    if (err != ESP_OK) { return err; }
    verify_.size = sizeof(verify_.bytes);
    err = nvs_get_blob(handle_, kKeys[target], verify_.bytes, &verify_.size);
    if (err != ESP_OK) { return err; }
    if (verify_.size != scratch_.size || std::memcmp(verify_.bytes, scratch_.bytes, scratch_.size) != 0) {
        return ESP_ERR_INVALID_CRC;
    }
    cached_ = scratch_;
    active_slot_ = target;
    ++sequence_;
    ESP_LOGI(kTag, "saved slot=%d generation=%llu bytes=%u segments=%u",
             active_slot_, static_cast<unsigned long long>(sequence_),
             static_cast<unsigned>(cached_.size), scene.count);
    return ESP_OK;
}

esp_err_t SceneStore::saveCallback(const esp_pixels_composer::AtScene &scene, void *context)
{
    return context == nullptr ? ESP_ERR_INVALID_ARG : static_cast<SceneStore *>(context)->save(scene);
}
} // namespace bench
