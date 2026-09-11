#include "scene_store.hpp"
#include "nvs_mock.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
namespace comp = esp_pixels_composer;
namespace mock = bench_mock;
using bench::SceneRecord;
int checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    std::printf("SCENE FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

void renewCrc(SceneRecord &r)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < r.size - 4; ++i) {
        crc ^= r.bytes[i];
        for (unsigned j = 0; j < 8; ++j) { crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0); }
    }
    crc = ~crc;
    for (unsigned j = 0; j < 4; ++j) { r.bytes[r.size - 4 + j] = crc >> (8 * j); }
}

void codecTests()
{
    static comp::AtScene scene, decoded;
    static SceneRecord record, reencoded;
    scene.count = comp::MAX_SEGMENTS;
    scene.brightness = 71;
    scene.paused = true;
    for (unsigned i = 0; i < scene.count; ++i) {
        auto &s = scene.segments[i];
        s.id = 100 + i; s.start = i; s.stop = i + 1; s.effect_id = i % 4;
        s.brightness = 200 - i; s.blend = static_cast<comp::BlendMode>(i % 3);
        s.opacity = 71 + i; s.reversed = (i % 2) != 0;
        s.state = static_cast<comp::SegmentState>(i % 3); s.restart_generation = 0xffff0000U + i;
        for (unsigned c = 0; c < 3; ++c) { s.params.color[c] = {uint8_t(i+c), uint8_t(53+i+c), uint8_t(201-i-c)}; }
        s.params.speed = i + 7; s.params.intensity = 231 - i; s.params.palette = i % 10;
        s.params.color_source = static_cast<esp_pixels_fx::FxColorSource>(i % 2);
        s.params.custom[0] = i; s.params.custom[1] = 65 + i; s.params.custom[2] = 250 - i;
    }
    constexpr uint64_t generation = 0x123456789abcdef0ULL;
    CHECK(bench::encodeScene(scene, generation, record) == ESP_OK);
    CHECK(record.size == bench::kSceneRecordBytes);
    CHECK(record.bytes[0] == 'S' && record.bytes[1] == 'C' && record.bytes[2] == 'N' && record.bytes[3] == '1');
    CHECK(record.bytes[8] == 0xf0 && record.bytes[15] == 0x12);
    CHECK(record.bytes[32] == 100 && record.bytes[36] == 0 && record.bytes[38] == 1);
    uint64_t found = 0;
    CHECK(bench::decodeScene(record, decoded, found) == ESP_OK && found == generation);
    CHECK(bench::encodeScene(decoded, found, reencoded) == ESP_OK);
    CHECK(record.size == reencoded.size && std::memcmp(record.bytes, reencoded.bytes, record.size) == 0);
    // A single damaged byte anywhere, including headers/CRC, is rejected transactionally.
    for (size_t i = 0; i < record.size; ++i) {
        record.bytes[i] ^= 0x80;
        decoded.count = 7; found = 123;
        CHECK(bench::decodeScene(record, decoded, found) != ESP_OK && decoded.count == 7 && found == 123);
        record.bytes[i] ^= 0x80;
    }
    const size_t full_size = record.size;
    for (size_t size : {size_t(0), size_t(31), size_t(36), full_size - 1, full_size + 1}) {
        record.size = size;
        CHECK(bench::decodeScene(record, decoded, found) != ESP_OK);
    }
    record.size = full_size;
    // Semantically invalid but CRC-valid records, including a bad second entry.
    const size_t offsets[] = {4, 16, 18, 20, 21, 22, 24, 25, 28, 32+8, 32+10, 32+12, 32+13, 32+29, 32+30};
    for (size_t offset : offsets) {
        reencoded = record; reencoded.bytes[offset] = 255; renewCrc(reencoded);
        decoded.count = 7; found = 123;
        CHECK(bench::decodeScene(reencoded, decoded, found) != ESP_OK && decoded.count == 7 && found == 123);
    }
    reencoded = record;
    std::memcpy(reencoded.bytes + 32 + bench::kSceneSegmentBytes, reencoded.bytes + 32, 4);
    renewCrc(reencoded);
    CHECK(bench::decodeScene(reencoded, decoded, found) != ESP_OK); // Duplicate IDs.
    scene.segments[1].id = scene.segments[0].id;
    CHECK(bench::encodeScene(scene, 1, reencoded) != ESP_OK);
    scene.count = 1; scene.segments[0].is2D = true;
    CHECK(bench::encodeScene(scene, 1, reencoded) != ESP_OK);
    scene.segments[0].is2D = false; scene.segments[0].stop = bench::kPixelCount + 1;
    CHECK(bench::encodeScene(scene, 1, reencoded) != ESP_OK);
    scene.count = 0;
    CHECK(bench::encodeScene(scene, 1, reencoded) == ESP_OK && reencoded.size == 36);
    CHECK(bench::decodeScene(reencoded, decoded, found) == ESP_OK && decoded.count == 0 && decoded.paused);
    CHECK(bench::encodeScene(scene, 0, reencoded) != ESP_OK);
}

void journalTests()
{
    static comp::AtScene scene, restored;
    static bench::SceneStore store, reboot, torn_reboot, failed_init;
    CHECK(store.load(scene) == ESP_ERR_NOT_FOUND);
    CHECK(mock::g_scene_writes == 0 && mock::g_scene_commits == 0);
    scene.count = 1; scene.segments[0].id = 1; scene.segments[0].stop = 25;
    scene.segments[0].params.color[0] = {15, 25, 35};
    CHECK(store.save(scene) == ESP_OK);
    CHECK(mock::g_scene_writes == 1 && mock::g_scene_commits == 1 &&
          mock::g_scene_slots[0].size != 0 && mock::g_scene_slots[1].size == 0);
    CHECK(store.save(scene) == ESP_OK && mock::g_scene_writes == 1); // Unchanged config: no wear.
    scene.paused = true;
    CHECK(store.save(scene) == ESP_OK && mock::g_scene_writes == 2 && mock::g_scene_slots[1].size != 0);
    CHECK(reboot.load(restored) == ESP_OK && restored.paused && restored.count == 1);
    CHECK(restored.segments[0].params.color[0].g == 25);
    mock::g_scene_slots[1].bytes[40] ^= 1; // Latest slot damaged: previous complete slot survives.
    CHECK(torn_reboot.load(restored) == ESP_OK && !restored.paused && restored.count == 1);
    const mock::SceneMockSlot previous = mock::g_scene_slots[0];
    mock::g_scene_fail_write = true;
    CHECK(torn_reboot.save(scene) == ESP_FAIL);
    CHECK(mock::g_scene_slots[0].size == previous.size &&
          std::memcmp(mock::g_scene_slots[0].bytes, previous.bytes, previous.size) == 0);
    mock::g_scene_fail_write = false; mock::g_scene_fail_commit = true;
    CHECK(torn_reboot.save(scene) == ESP_FAIL);
    CHECK(std::memcmp(mock::g_scene_slots[0].bytes, previous.bytes, previous.size) == 0);
    mock::g_scene_fail_commit = false; mock::g_scene_fail_readback = true;
    CHECK(torn_reboot.save(scene) == ESP_FAIL);
    mock::g_scene_fail_readback = false;
    CHECK(torn_reboot.save(scene) == ESP_OK); // Failure never poisons cache/retry.
    scene.count = 0;
    CHECK(torn_reboot.save(scene) == ESP_OK); // Empty configuration is durable too.
    mock::g_scene_init_error = ESP_FAIL;
    CHECK(failed_init.load(restored) == ESP_FAIL);
    CHECK(failed_init.save(scene) == ESP_FAIL); // Never erase/reinitialize on failure.
    mock::g_scene_init_error = ESP_OK;
}
} // namespace

int run_scene_tests()
{
    checks = 0; failures = 0;
    codecTests(); journalTests();
    std::printf("SCENE checks=%d failures=%d\n", checks, failures);
    return failures;
}
int scene_check_count() { return checks; }
