#include "nvs_mock.hpp"
#include "nvs.h"
#include <cstring>
#include <cstdlib>
#include <new>

namespace bench_mock {

SceneMockSlot g_scene_slots[2];
unsigned g_scene_writes = 0;
unsigned g_scene_commits = 0;
esp_err_t g_scene_init_error = ESP_OK;
bool g_scene_fail_write = false;
bool g_scene_fail_commit = false;
bool g_scene_fail_readback = false;

PresetMockSlot (*g_preset_slots)[2] = nullptr;
unsigned g_preset_writes = 0;
unsigned g_preset_commits = 0;
unsigned g_preset_erases = 0;
esp_err_t g_preset_init_error = ESP_OK;
bool g_preset_fail_write = false;
bool g_preset_fail_commit = false;
bool g_preset_fail_readback = false;

OutputMockSlot g_output_slots[2];
unsigned g_output_writes = 0;
unsigned g_output_commits = 0;
bool g_output_fail_write = false;
bool g_output_fail_commit = false;
bool g_output_fail_readback = false;

void resetAll()
{
    if (g_preset_slots == nullptr) {
        g_preset_slots = new (std::nothrow) PresetMockSlot[kPresetMockSlots][2];
        if (g_preset_slots == nullptr) { std::abort(); }
    }
    for (auto &s : g_scene_slots) { s = SceneMockSlot{}; }
    g_scene_writes = 0;
    g_scene_commits = 0;
    g_scene_init_error = ESP_OK;
    g_scene_fail_write = false;
    g_scene_fail_commit = false;
    g_scene_fail_readback = false;

    for (unsigned i = 0; i < kPresetMockSlots; ++i) {
        for (auto &s : g_preset_slots[i]) { s = PresetMockSlot{}; }
    }
    g_preset_writes = 0;
    g_preset_commits = 0;
    g_preset_erases = 0;
    g_preset_init_error = ESP_OK;
    g_preset_fail_write = false;
    g_preset_fail_commit = false;
    g_preset_fail_readback = false;

    for (auto &s : g_output_slots) { s = OutputMockSlot{}; }
    g_output_writes = 0;
    g_output_commits = 0;
    g_output_fail_write = false;
    g_output_fail_commit = false;
    g_output_fail_readback = false;
}

void resetPresets()
{
    for (unsigned i = 0; i < kPresetMockSlots; ++i) {
        for (auto &s : g_preset_slots[i]) { s = PresetMockSlot{}; }
    }
    g_preset_writes = 0;
    g_preset_commits = 0;
    g_preset_erases = 0;
    g_preset_init_error = ESP_OK;
    g_preset_fail_write = false;
    g_preset_fail_commit = false;
    g_preset_fail_readback = false;
}

namespace {
// True, with `slot`/`ab` populated, iff `key` is exactly "p<NN><a|b>" (a
// PresetStore key) -- anything else (e.g. "scene_a"/"scene_b") is left to
// the scene mock path instead.
bool parsePresetKey(const char *key, unsigned &slot, unsigned &ab)
{
    if (key == nullptr || std::strlen(key) != 4 || key[0] != 'p') { return false; }
    if (key[1] < '0' || key[1] > '9' || key[2] < '0' || key[2] > '9') { return false; }
    if (key[3] != 'a' && key[3] != 'b') { return false; }
    const unsigned parsed_slot = static_cast<unsigned>((key[1] - '0') * 10 + (key[2] - '0'));
    if (parsed_slot >= kPresetMockSlots) { return false; }
    slot = parsed_slot;
    ab = (key[3] == 'a') ? 0u : 1u;
    return true;
}
} // namespace

} // namespace bench_mock

// Handles: 1 = SceneStore's own default-partition open (nvs_flash_init()/
// nvs_open("pixels_scene", ...)); 2 = the preset mock's named-partition open
// (nvs_flash_init_partition()/nvs_open_from_partition()); 3 = OutputStore's
// own default-partition open (nvs_open("pixels_outputs", ...) -- the SAME
// nvs_open() entry point SceneStore uses, distinguished here by namespace
// string so nvs_commit() (which carries no key of its own) can still tell
// all three apart. nvs_get_blob()/nvs_set_blob()/nvs_erase_key() dispatch
// purely by key shape instead, so they work regardless of which handle was
// passed.

extern "C" esp_err_t __wrap_nvs_flash_init() { return bench_mock::g_scene_init_error; }

extern "C" esp_err_t __wrap_nvs_open(const char *namespace_name, nvs_open_mode_t, nvs_handle_t *handle)
{
    *handle = (namespace_name != nullptr && std::strcmp(namespace_name, "pixels_outputs") == 0) ? 3 : 1;
    return ESP_OK;
}

extern "C" esp_err_t __wrap_nvs_flash_init_partition(const char *) { return bench_mock::g_preset_init_error; }

extern "C" esp_err_t __wrap_nvs_open_from_partition(const char *, const char *, nvs_open_mode_t,
                                                    nvs_handle_t *handle)
{
    *handle = 2;
    return ESP_OK;
}

namespace {
// True (with `ab` populated) iff `key` is exactly "outputs_a"/"outputs_b" --
// anything else is left to the scene mock path instead.
bool parseOutputKey(const char *key, unsigned &ab)
{
    if (key == nullptr) { return false; }
    if (std::strcmp(key, "outputs_a") == 0) { ab = 0; return true; }
    if (std::strcmp(key, "outputs_b") == 0) { ab = 1; return true; }
    return false;
}
} // namespace

extern "C" esp_err_t __wrap_nvs_get_blob(nvs_handle_t, const char *key, void *value, size_t *size)
{
    unsigned slot = 0;
    unsigned ab = 0;
    if (bench_mock::parsePresetKey(key, slot, ab)) {
        if (bench_mock::g_preset_fail_readback) { return ESP_FAIL; }
        const auto &r = bench_mock::g_preset_slots[slot][ab];
        if (r.size == 0) { return ESP_ERR_NVS_NOT_FOUND; }
        if (*size < r.size) { *size = r.size; return ESP_ERR_NVS_INVALID_LENGTH; }
        *size = r.size;
        std::memcpy(value, r.bytes, r.size);
        return ESP_OK;
    }
    if (parseOutputKey(key, ab)) {
        if (bench_mock::g_output_fail_readback) { return ESP_FAIL; }
        const auto &r = bench_mock::g_output_slots[ab];
        if (r.size == 0) { return ESP_ERR_NVS_NOT_FOUND; }
        if (*size < r.size) { *size = r.size; return ESP_ERR_NVS_INVALID_LENGTH; }
        *size = r.size;
        std::memcpy(value, r.bytes, r.size);
        return ESP_OK;
    }
    if (bench_mock::g_scene_fail_readback) { return ESP_FAIL; }
    const auto &r = bench_mock::g_scene_slots[std::strcmp(key, "scene_a") == 0 ? 0 : 1];
    if (r.size == 0) { return ESP_ERR_NVS_NOT_FOUND; }
    if (*size < r.size) { *size = r.size; return ESP_ERR_NVS_INVALID_LENGTH; }
    *size = r.size;
    std::memcpy(value, r.bytes, r.size);
    return ESP_OK;
}

extern "C" esp_err_t __wrap_nvs_set_blob(nvs_handle_t, const char *key, const void *value, size_t size)
{
    unsigned slot = 0;
    unsigned ab = 0;
    if (bench_mock::parsePresetKey(key, slot, ab)) {
        ++bench_mock::g_preset_writes;
        auto &r = bench_mock::g_preset_slots[slot][ab];
        if (size > sizeof(r.bytes)) { return ESP_ERR_INVALID_SIZE; }
        r.size = size;
        std::memcpy(r.bytes, value, size);
        if (bench_mock::g_preset_fail_write) { r.bytes[0] ^= 1; return ESP_FAIL; } // simulated torn write
        return ESP_OK;
    }
    if (parseOutputKey(key, ab)) {
        ++bench_mock::g_output_writes;
        auto &r = bench_mock::g_output_slots[ab];
        r.size = size;
        std::memcpy(r.bytes, value, size);
        if (bench_mock::g_output_fail_write) { r.bytes[0] ^= 1; return ESP_FAIL; } // simulated torn write
        return ESP_OK;
    }
    ++bench_mock::g_scene_writes;
    auto &r = bench_mock::g_scene_slots[std::strcmp(key, "scene_a") == 0 ? 0 : 1];
    r.size = size;
    std::memcpy(r.bytes, value, size);
    if (bench_mock::g_scene_fail_write) { r.bytes[0] ^= 1; return ESP_FAIL; } // simulated torn write
    return ESP_OK;
}

extern "C" esp_err_t __wrap_nvs_commit(nvs_handle_t handle)
{
    if (handle == 2) {
        ++bench_mock::g_preset_commits;
        return bench_mock::g_preset_fail_commit ? ESP_FAIL : ESP_OK;
    }
    if (handle == 3) {
        ++bench_mock::g_output_commits;
        return bench_mock::g_output_fail_commit ? ESP_FAIL : ESP_OK;
    }
    ++bench_mock::g_scene_commits;
    return bench_mock::g_scene_fail_commit ? ESP_FAIL : ESP_OK;
}

extern "C" esp_err_t __wrap_nvs_erase_key(nvs_handle_t, const char *key)
{
    unsigned slot = 0;
    unsigned ab = 0;
    if (!bench_mock::parsePresetKey(key, slot, ab)) { return ESP_ERR_NVS_NOT_FOUND; }
    ++bench_mock::g_preset_erases;
    auto &r = bench_mock::g_preset_slots[slot][ab];
    if (r.size == 0) { return ESP_ERR_NVS_NOT_FOUND; }
    r = bench_mock::PresetMockSlot{};
    return ESP_OK;
}
