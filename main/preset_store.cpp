#include "preset_store.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include <cstring>

namespace bench {
namespace {
constexpr const char *kPartitionLabel = "presets";
constexpr const char *kNamespace = "pixels_presets";
constexpr const char *kTag = "PRESET_NVS";
} // namespace

void PresetStore::slotKey(unsigned slot, unsigned ab, char out[6])
{
    out[0] = 'p';
    out[1] = static_cast<char>('0' + (slot / 10) % 10);
    out[2] = static_cast<char>('0' + slot % 10);
    out[3] = (ab == 0) ? 'a' : 'b';
    out[4] = '\0';
}

int PresetStore::findByName(const char *name, size_t name_len) const
{
    if (name == nullptr || name_len == 0 || name_len > kPresetNameMaxLen) { return -1; }
    for (unsigned slot = 0; slot < kPresetSlotCount; ++slot) {
        const Slot &s = slots_[slot];
        if (s.occupied && std::strlen(s.name) == name_len && std::memcmp(s.name, name, name_len) == 0) {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

int PresetStore::findFreeSlot() const
{
    for (unsigned slot = 0; slot < kPresetSlotCount; ++slot) {
        if (!slots_[slot].occupied) { return static_cast<int>(slot); }
    }
    return -1;
}

esp_err_t PresetStore::begin(const Topology &topology)
{
    if (handle_ != 0) { return ESP_ERR_INVALID_STATE; }
    topology_ = topology;
    // Deliberately no erase/retry on NO_FREE_PAGES or NEW_VERSION_FOUND,
    // same policy SceneStore::load() already has -- a genuinely blank
    // partition (the expected first-flash state) is formatted by
    // nvs_flash_init_partition() itself; anything else is reported, never
    // silently erased.
    init_error_ = nvs_flash_init_partition(kPartitionLabel);
    if (init_error_ != ESP_OK) { return init_error_; }
    init_error_ = nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle_);
    if (init_error_ != ESP_OK) { return init_error_; }

    unsigned occupied_count = 0;
    for (unsigned slot = 0; slot < kPresetSlotCount; ++slot) {
        Slot &s = slots_[slot];
        s = Slot{};
        for (unsigned ab = 0; ab < 2; ++ab) {
            char key[6];
            slotKey(slot, ab, key);
            scratch_.size = sizeof(scratch_.bytes);
            esp_err_t err = nvs_get_blob(handle_, key, scratch_.bytes, &scratch_.size);
            if (err != ESP_OK) { continue; } // not found, or unreadable -- skip this key
            char name[kPresetNameMaxLen + 1];
            size_t name_len = 0;
            esp_pixels_composer::AtScene decoded;
            uint64_t sequence = 0;
            err = decodeStoredPreset(scratch_, name, name_len, decoded, sequence);
            if (err != ESP_OK) {
                ESP_LOGW(kTag, "slot %u key %c rejected: %s", slot, ab == 0 ? 'a' : 'b', esp_err_to_name(err));
                continue;
            }
            if (s.occupied && sequence <= s.sequence) { continue; } // keep the newer of the two keys
            s.occupied = true;
            s.active_ab = static_cast<int>(ab);
            s.sequence = sequence;
            std::memcpy(s.name, name, name_len + 1);
            s.segment_count = decoded.count;
        }
        if (s.occupied) { ++occupied_count; }
    }
    ESP_LOGI(kTag, "opened: occupied=%u/%u", occupied_count, kPresetSlotCount);
    return ESP_OK;
}

void PresetStore::list(PresetInfoVisitor fn, void *context) const
{
    if (fn == nullptr) { return; }
    for (unsigned slot = 0; slot < kPresetSlotCount; ++slot) {
        const Slot &s = slots_[slot];
        if (!s.occupied) { continue; }
        PresetInfo info{};
        std::memcpy(info.name, s.name, sizeof(info.name));
        info.segment_count = s.segment_count;
        fn(context, info);
    }
}

esp_err_t PresetStore::save(const char *name, size_t name_len, const esp_pixels_composer::AtScene &scene)
{
    if (init_error_ != ESP_OK || handle_ == 0) { return ESP_ERR_INVALID_STATE; }
    if (!validPresetName(name, name_len)) { return ESP_ERR_INVALID_ARG; }

    int slot = findByName(name, name_len);
    if (slot < 0) {
        slot = findFreeSlot();
        if (slot < 0) { return ESP_ERR_NO_MEM; }
    }
    Slot &s = slots_[static_cast<unsigned>(slot)];
    const uint64_t next_sequence = s.occupied ? s.sequence + 1 : 1;
    const esp_err_t encode_err = encodePreset(name, name_len, scene, topology_, next_sequence, scratch_);
    if (encode_err != ESP_OK) { return encode_err; }

    // Targets the OTHER A/B key (or 'a' for a never-occupied slot, same
    // ternary SceneStore::save() already uses: active_ab starts at -1, so
    // -1==0 is false and target is 0 either way) -- the prior valid key is
    // never overwritten by this call, only read back for comparison never.
    const int target_ab = (s.active_ab == 0) ? 1 : 0;
    char key[6];
    slotKey(static_cast<unsigned>(slot), static_cast<unsigned>(target_ab), key);
    esp_err_t err = nvs_set_blob(handle_, key, scratch_.bytes, scratch_.size);
    if (err == ESP_OK) { err = nvs_commit(handle_); }
    if (err != ESP_OK) { return err; }
    verify_.size = sizeof(verify_.bytes);
    err = nvs_get_blob(handle_, key, verify_.bytes, &verify_.size);
    if (err != ESP_OK) { return err; }
    if (verify_.size != scratch_.size || std::memcmp(verify_.bytes, scratch_.bytes, scratch_.size) != 0) {
        return ESP_ERR_INVALID_CRC;
    }

    s.occupied = true;
    s.active_ab = target_ab;
    s.sequence = next_sequence;
    std::memcpy(s.name, name, name_len);
    s.name[name_len] = '\0';
    s.segment_count = scene.count;
    ESP_LOGI(kTag, "saved slot=%d key=%s generation=%llu bytes=%u segments=%u", slot, key,
             static_cast<unsigned long long>(next_sequence), static_cast<unsigned>(scratch_.size), scene.count);
    return ESP_OK;
}

esp_err_t PresetStore::load(const char *name, size_t name_len, esp_pixels_composer::AtScene &scene) const
{
    if (init_error_ != ESP_OK || handle_ == 0) { return ESP_ERR_INVALID_STATE; }
    const int slot = findByName(name, name_len);
    if (slot < 0) { return ESP_ERR_NOT_FOUND; }
    const Slot &s = slots_[static_cast<unsigned>(slot)];
    char key[6];
    slotKey(static_cast<unsigned>(slot), static_cast<unsigned>(s.active_ab), key);
    scratch_.size = sizeof(scratch_.bytes);
    esp_err_t err = nvs_get_blob(handle_, key, scratch_.bytes, &scratch_.size);
    if (err != ESP_OK) { return err; }
    char decoded_name[kPresetNameMaxLen + 1];
    size_t decoded_name_len = 0;
    uint64_t sequence = 0;
    return decodePreset(scratch_, topology_, decoded_name, decoded_name_len, scene, sequence);
}

esp_err_t PresetStore::remove(const char *name, size_t name_len)
{
    if (init_error_ != ESP_OK || handle_ == 0) { return ESP_ERR_INVALID_STATE; }
    const int slot = findByName(name, name_len);
    if (slot < 0) { return ESP_ERR_NOT_FOUND; }
    char key_a[6];
    char key_b[6];
    slotKey(static_cast<unsigned>(slot), 0, key_a);
    slotKey(static_cast<unsigned>(slot), 1, key_b);
    const esp_err_t erase_a = nvs_erase_key(handle_, key_a);
    const esp_err_t erase_b = nvs_erase_key(handle_, key_b);
    const esp_err_t commit_err = nvs_commit(handle_);
    const bool a_ok = erase_a == ESP_OK || erase_a == ESP_ERR_NVS_NOT_FOUND;
    const bool b_ok = erase_b == ESP_OK || erase_b == ESP_ERR_NVS_NOT_FOUND;
    if (a_ok && b_ok && commit_err == ESP_OK) {
        slots_[static_cast<unsigned>(slot)] = Slot{};
        ESP_LOGI(kTag, "deleted slot=%d", slot);
        return ESP_OK;
    }
    // This store's own cached metadata for the slot is left exactly as it
    // was on any failure path -- a concurrent list()/load() must not report
    // it as already gone when the underlying data may still be intact.
    if (commit_err != ESP_OK) { return commit_err; }
    return !a_ok ? erase_a : erase_b;
}
} // namespace bench
