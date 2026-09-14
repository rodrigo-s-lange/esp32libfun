#include "scene_store.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include <cstring>

namespace bench {
namespace {
constexpr const char *kKeys[] = {"scene_a", "scene_b"};
constexpr const char *kTag = "SCENE_NVS";
}

esp_err_t SceneStore::load(const Topology &topology, esp_pixels_composer::AtScene &scene)
{
    if (handle_ != 0) { return ESP_ERR_INVALID_STATE; }
    bound_topology_ = topology;
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
        if (scratch_.size >= 16 && scratch_.size <= sizeof(scratch_.bytes)) {
            for (unsigned i = 0; i < 8; ++i) {
                candidate_sequence |= static_cast<uint64_t>(scratch_.bytes[8 + i]) << (8 * i);
            }
        }
        if (active_slot_ >= 0 && candidate_sequence <= sequence_) { continue; }
        err = decodeScene(scratch_, topology, scene, candidate_sequence);
        if (err != ESP_OK) {
            last_error = err;
            ESP_LOGW(kTag, "slot %d rejected (incompatible or corrupt): %s", slot, esp_err_to_name(err));
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

esp_err_t SceneStore::save(const Topology &topology, const esp_pixels_composer::AtScene &scene)
{
    if (init_error_ != ESP_OK) { return init_error_; }
    if (handle_ == 0 || sequence_ == UINT64_MAX) { return ESP_ERR_INVALID_STATE; }
    if (next_topology_set_ && scene.count != 0 && !isCompatiblePrefix(topology, next_topology_)) {
        return ESP_ERR_INVALID_STATE; // apply the saved output change by rebooting before saving a new scene
    }
    bound_topology_ = topology;
    esp_err_t err = encodeScene(scene, topology, sequence_ + 1, scratch_);
    if (err != ESP_OK) { return err; }
    // Compare canonical bytes excluding generation and CRC: bytes [0:8)
    // (magic+schema+size -- a schema change, e.g. an old v1 record's next
    // save upgrading it to v2, always compares unequal here, exactly as it
    // should) and everything from [16:size-4) (skipping the 8-byte
    // generation and the trailing 4-byte CRC).
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
    if (context == nullptr) { return ESP_ERR_INVALID_ARG; }
    SceneStore *self = static_cast<SceneStore *>(context);
    return self->save(self->bound_topology_, scene);
}

esp_err_t SceneStore::canRestoreOn(const Topology &topology) const
{
    if (!topology.valid()) { return ESP_ERR_INVALID_ARG; }
    if (init_error_ != ESP_OK || handle_ == 0) { return ESP_ERR_INVALID_STATE; }
    if (cached_.size == 0) { return ESP_OK; }
    static esp_pixels_composer::AtScene decoded; // single control caller, off the AT stack
    uint64_t generation = 0;
    esp_err_t err = decodeStoredScene(cached_, decoded, generation);
    if (err != ESP_OK || decoded.count == 0) { return err; }
    return decodeScene(cached_, topology, decoded, generation);
}
} // namespace bench
