#pragma once

#include "esp_err.h"
#include <cstddef>
#include <cstdint>

// Shared linker-wrapped NVS mock for this on-target test image
// (pixels_scene_test): every `__wrap_nvs_*` symbol is defined exactly once,
// in nvs_mock.cpp, since `-Wl,--wrap` substitutes it link-wide for the
// WHOLE binary -- test_scene.cpp (SceneStore, namespace "pixels_scene",
// keys scene_a/scene_b), test_presets.cpp (PresetStore, named "presets"
// partition, keys p00a..p99b), and test_outputs.cpp (OutputStore, namespace
// "pixels_outputs", keys outputs_a/outputs_b) all drive fault injection
// through this one shared mock instead of each defining its own wraps. No
// real NVS/partition access anywhere in this binary, regardless of which
// store is under test.

namespace bench_mock {

// --- current-scene mock (SceneStore) ---------------------------------

constexpr size_t kSceneMockBytes = 1200; // >= scene_codec.hpp's kSceneRecordBytes (1124)

struct SceneMockSlot {
    uint8_t bytes[kSceneMockBytes]{};
    size_t size = 0;
};

extern SceneMockSlot g_scene_slots[2]; // scene_a, scene_b
extern unsigned g_scene_writes;
extern unsigned g_scene_commits;
extern esp_err_t g_scene_init_error;
extern bool g_scene_fail_write;
extern bool g_scene_fail_commit;
extern bool g_scene_fail_readback;

// --- preset mock (PresetStore) -----------------------------------------
//
// 200 bytes/key is deliberately small: every PresetStore-level gate in
// test_presets.cpp uses a 0-2 segment scene (max ~153 bytes encoded); the
// maximum-size (32-segment) round trip is exercised directly against
// encodePreset()/decodePreset(), with no NVS/mock involved at all, the
// same way test_scene.cpp's own codecTests() already proves encodeScene()/
// decodeScene() without going through SceneStore. 100 x 2 x 200 = 40000
// bytes total, comfortably inside this test image's own static budget.

constexpr unsigned kPresetMockSlots = 100;
constexpr size_t kPresetMockBytes = 160;

struct PresetMockSlot {
    uint8_t bytes[kPresetMockBytes]{};
    size_t size = 0;
};

extern PresetMockSlot (*g_preset_slots)[2]; // Allocated once at test startup; [slot][A/B].
extern unsigned g_preset_writes;
extern unsigned g_preset_commits;
extern unsigned g_preset_erases;
extern esp_err_t g_preset_init_error;
extern bool g_preset_fail_write;
extern bool g_preset_fail_commit;
extern bool g_preset_fail_readback;

// --- output-topology mock (OutputStore) --------------------------------
//
// A separate namespace ("pixels_outputs") from SceneStore's own default
// partition ("pixels_scene"), but the SAME __wrap_nvs_open()/
// __wrap_nvs_flash_init() entry points (both call plain nvs_flash_init() +
// nvs_open(), not nvs_flash_init_partition()/nvs_open_from_partition() --
// only the preset store uses those). __wrap_nvs_open() is namespace-aware
// (handle=1 for "pixels_scene", handle=3 for "pixels_outputs") so
// nvs_commit(), which carries no key of its own, can still tell the two
// apart; get_blob()/set_blob()/erase_key() dispatch by key shape
// ("outputs_a"/"outputs_b"), same as the other two mocks. There is
// deliberately no separate `g_output_init_error`: __wrap_nvs_flash_init()
// itself carries no namespace/key either, so a `nvs_flash_init()` failure
// is injected jointly for SceneStore and OutputStore via `g_scene_init_error`
// -- the same limitation this shared entry point already has for those two.

constexpr size_t kOutputMockBytes = 64; // >= output_store.hpp's kRecordBytes (31 at kMaxOutputs==2)

struct OutputMockSlot {
    uint8_t bytes[kOutputMockBytes]{};
    size_t size = 0;
};

extern OutputMockSlot g_output_slots[2]; // outputs_a, outputs_b
extern unsigned g_output_writes;
extern unsigned g_output_commits;
extern bool g_output_fail_write;
extern bool g_output_fail_commit;
extern bool g_output_fail_readback;

/// Resets every counter/flag/slot above (all three mocks) to its initial state.
/// Called once, before any `run_*_tests()`, from this image's own
/// `app_main()` -- never mid-suite, so no runner needs to guess what an
/// earlier one left behind.
void resetAll();

/// Resets only the preset mock -- for a later, independent test function
/// (e.g. an AT-library integration test) that needs its own fresh 100
/// positions, without disturbing the scene mock's own state.
void resetPresets();

} // namespace bench_mock
