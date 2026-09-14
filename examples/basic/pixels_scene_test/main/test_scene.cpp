#include "scene_store.hpp"
#include "nvs_mock.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
namespace comp = esp_pixels_composer;
namespace mock = bench_mock;
using bench::OutputSpec;
using bench::SceneRecord;
using bench::Topology;
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
    const Topology topology = bench::defaultTopology(); // one output: schema-2 header is 31 bytes
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
    constexpr size_t kHeaderBytes = 31; // schema 2, one output: 4+2+2+8+1+5+1+1+1+2+4
    CHECK(bench::encodeScene(scene, topology, generation, record) == ESP_OK);
    CHECK(record.size == kHeaderBytes + comp::MAX_SEGMENTS * bench::kSceneSegmentBytes + 4);
    CHECK(record.bytes[0] == 'S' && record.bytes[1] == 'C' && record.bytes[2] == 'N' && record.bytes[3] == '1');
    CHECK(record.bytes[8] == 0xf0 && record.bytes[15] == 0x12);
    // Segment 0's own id(4)/start(2)/stop(2) LSBs, right after the header.
    CHECK(record.bytes[kHeaderBytes] == 100 && record.bytes[kHeaderBytes + 4] == 0 &&
          record.bytes[kHeaderBytes + 6] == 1);
    uint64_t found = 0;
    CHECK(bench::decodeScene(record, topology, decoded, found) == ESP_OK && found == generation);
    CHECK(bench::encodeScene(decoded, topology, found, reencoded) == ESP_OK);
    CHECK(record.size == reencoded.size && std::memcmp(record.bytes, reencoded.bytes, record.size) == 0);
    // A single damaged byte anywhere, including headers/CRC, is rejected transactionally.
    for (size_t i = 0; i < record.size; ++i) {
        record.bytes[i] ^= 0x80;
        decoded.count = 7; found = 123;
        CHECK(bench::decodeScene(record, topology, decoded, found) != ESP_OK && decoded.count == 7 && found == 123);
        record.bytes[i] ^= 0x80;
    }
    const size_t full_size = record.size;
    for (size_t size : {size_t(0), size_t(20), size_t(kHeaderBytes), full_size - 1, full_size + 1}) {
        record.size = size;
        CHECK(bench::decodeScene(record, topology, decoded, found) != ESP_OK);
    }
    record.size = full_size;
    // Semantically invalid but CRC-valid records, including a bad second entry.
    const size_t offsets[] = {4, 16, 17, 18, 20, 21, 22, 24, 26,
                              kHeaderBytes + 8, kHeaderBytes + 10, kHeaderBytes + 12, kHeaderBytes + 13,
                              kHeaderBytes + 29, kHeaderBytes + 30};
    for (size_t offset : offsets) {
        reencoded = record; reencoded.bytes[offset] = 255; renewCrc(reencoded);
        decoded.count = 7; found = 123;
        CHECK(bench::decodeScene(reencoded, topology, decoded, found) != ESP_OK && decoded.count == 7 && found == 123);
    }
    reencoded = record;
    std::memcpy(reencoded.bytes + kHeaderBytes + bench::kSceneSegmentBytes, reencoded.bytes + kHeaderBytes, 4);
    renewCrc(reencoded);
    CHECK(bench::decodeScene(reencoded, topology, decoded, found) != ESP_OK); // Duplicate IDs.
    scene.segments[1].id = scene.segments[0].id;
    CHECK(bench::encodeScene(scene, topology, 1, reencoded) != ESP_OK);
    scene.count = 1; scene.segments[0].is2D = true;
    CHECK(bench::encodeScene(scene, topology, 1, reencoded) != ESP_OK);
    scene.segments[0].is2D = false; scene.segments[0].stop = topology.totalPixels() + 1;
    CHECK(bench::encodeScene(scene, topology, 1, reencoded) != ESP_OK);
    scene.count = 0;
    CHECK(bench::encodeScene(scene, topology, 1, reencoded) == ESP_OK && reencoded.size == kHeaderBytes + 4);
    CHECK(bench::decodeScene(reencoded, topology, decoded, found) == ESP_OK && decoded.count == 0 && decoded.paused);
    CHECK(bench::encodeScene(scene, topology, 0, reencoded) != ESP_OK);
    CHECK(bench::encodeScene(scene, Topology{}, 1, reencoded) != ESP_OK); // count==0: invalid topology

    // Global logical indices across two outputs (Design v2 -- "no second
    // numbering convention in SEGSET"): counts 50 and 20 make output 0 the
    // range [0,50) and output 1 [50,70); demonstrate an independent segment
    // on each output AND one segment that CROSSES the boundary between
    // them, all validated only against totalPixels() == 70, never against
    // either output's own count individually.
    Topology two;
    two.count = 2;
    two.outputs[0] = OutputSpec{16, 50, 0};
    two.outputs[1] = OutputSpec{4, 20, 0};
    CHECK(two.totalPixels() == 70);

    scene = comp::AtScene{};
    scene.count = 3;
    scene.segments[0] = comp::SegmentSpec{}; // entirely within output 0
    scene.segments[0].id = 1; scene.segments[0].start = 0; scene.segments[0].stop = 50;
    scene.segments[1] = comp::SegmentSpec{}; // entirely within output 1
    scene.segments[1].id = 2; scene.segments[1].start = 50; scene.segments[1].stop = 70;
    scene.segments[2] = comp::SegmentSpec{}; // CROSSES the output 0/output 1 boundary at index 50
    scene.segments[2].id = 3; scene.segments[2].start = 45; scene.segments[2].stop = 55;
    CHECK(bench::encodeScene(scene, two, 1, reencoded) == ESP_OK);
    decoded = comp::AtScene{};
    CHECK(bench::decodeScene(reencoded, two, decoded, found) == ESP_OK && decoded.count == 3);
    CHECK(decoded.segments[0].start == 0 && decoded.segments[0].stop == 50);
    CHECK(decoded.segments[1].start == 50 && decoded.segments[1].stop == 70);
    CHECK(decoded.segments[2].start == 45 && decoded.segments[2].stop == 55); // the crossing segment, intact

    // One past the combined total (71) is out of range regardless of which
    // output it would fall on; exactly at the total (70) is the crossing
    // segment's own valid upper bound, already proven above.
    scene.count = 1;
    scene.segments[0] = comp::SegmentSpec{};
    scene.segments[0].id = 1; scene.segments[0].stop = 71;
    CHECK(bench::encodeScene(scene, two, 1, reencoded) != ESP_OK);
}

void journalTests()
{
    static comp::AtScene scene, restored;
    static bench::SceneStore store, reboot, torn_reboot, failed_init;
    const Topology topology = bench::defaultTopology();
    CHECK(store.load(topology, scene) == ESP_ERR_NOT_FOUND);
    CHECK(mock::g_scene_writes == 0 && mock::g_scene_commits == 0);
    scene.count = 1; scene.segments[0].id = 1; scene.segments[0].stop = 25;
    scene.segments[0].params.color[0] = {15, 25, 35};
    CHECK(store.save(topology, scene) == ESP_OK);
    CHECK(mock::g_scene_writes == 1 && mock::g_scene_commits == 1 &&
          mock::g_scene_slots[0].size != 0 && mock::g_scene_slots[1].size == 0);
    CHECK(store.save(topology, scene) == ESP_OK && mock::g_scene_writes == 1); // Unchanged config: no wear.
    scene.paused = true;
    CHECK(store.save(topology, scene) == ESP_OK && mock::g_scene_writes == 2 && mock::g_scene_slots[1].size != 0);
    CHECK(reboot.load(topology, restored) == ESP_OK && restored.paused && restored.count == 1);
    CHECK(restored.segments[0].params.color[0].g == 25);
    mock::g_scene_slots[1].bytes[40] ^= 1; // Latest slot damaged: previous complete slot survives.
    CHECK(torn_reboot.load(topology, restored) == ESP_OK && !restored.paused && restored.count == 1);
    const mock::SceneMockSlot previous = mock::g_scene_slots[0];
    mock::g_scene_fail_write = true;
    CHECK(torn_reboot.save(topology, scene) == ESP_FAIL);
    CHECK(mock::g_scene_slots[0].size == previous.size &&
          std::memcmp(mock::g_scene_slots[0].bytes, previous.bytes, previous.size) == 0);
    mock::g_scene_fail_write = false; mock::g_scene_fail_commit = true;
    CHECK(torn_reboot.save(topology, scene) == ESP_FAIL);
    CHECK(std::memcmp(mock::g_scene_slots[0].bytes, previous.bytes, previous.size) == 0);
    mock::g_scene_fail_commit = false; mock::g_scene_fail_readback = true;
    CHECK(torn_reboot.save(topology, scene) == ESP_FAIL);
    mock::g_scene_fail_readback = false;
    CHECK(torn_reboot.save(topology, scene) == ESP_OK); // Failure never poisons cache/retry.
    scene.count = 0;
    CHECK(torn_reboot.save(topology, scene) == ESP_OK); // Empty configuration is durable too.
    mock::g_scene_init_error = ESP_FAIL;
    CHECK(failed_init.load(topology, restored) == ESP_FAIL);
    CHECK(failed_init.save(topology, scene) == ESP_FAIL); // Never erase/reinitialize on failure.
    mock::g_scene_init_error = ESP_OK;

    // A real, non-empty single-output record, saved fresh for the
    // compatibility checks below (the sequence above ended on an empty
    // scene, which would not exercise "segments keep their meaning").
    // Reuses `torn_reboot` (already open and the last writer of the shared
    // mock NVS, so its own active_slot_/sequence_ stay self-consistent) and
    // the now-idle `scene` static, instead of spending another SceneStore
    // instance -- this whole compatibility section costs exactly one NEW
    // instance (`appended_reboot` below), the same one-extra-instance
    // discipline test_presets.cpp's own presetStoreCorruptRecoveryTest()
    // already established (DRAM on this target is tight -- see its comment).
    scene = comp::AtScene{};
    scene.count = 1;
    scene.segments[0].id = 7;
    scene.segments[0].stop = 25;
    scene.segments[0].params.color[0] = {9, 8, 7};
    CHECK(torn_reboot.save(topology, scene) == ESP_OK);

    // A second output appended: the original segment (bounded by the
    // original 50-pixel total) keeps its exact meaning -- isCompatiblePrefix()
    // lets a smaller record's own topology recall under a larger current one.
    // `load()` is call-once-per-instance (mirrors a real reboot), so this
    // genuinely needs a fresh instance -- the one extra this section spends.
    Topology appended = topology;
    appended.count = 2;
    appended.outputs[1] = bench::OutputSpec{4, 20, 0};
    static bench::SceneStore appended_reboot;
    static comp::AtScene appended_scene;
    CHECK(appended_reboot.load(appended, appended_scene) == ESP_OK);
    CHECK(appended_scene.count == 1 && appended_scene.segments[0].id == 7 &&
          appended_scene.segments[0].stop == 25 && appended_scene.segments[0].params.color[0].r == 9);

    // A CHANGED first output (different pin) is incompatible -- rejected.
    // Probed directly from the same mock bytes a real reboot would read
    // (decodeScene() is exactly what SceneStore::load() itself calls for
    // each slot), so this needs no further SceneStore instance; the stored
    // record itself is never modified (still recoverable under the
    // original topology, as `appended_reboot` above already proved).
    Topology changed = topology;
    changed.outputs[0].pin = 99;
    SceneRecord probe; // stack-local: this section's DRAM (.bss) budget is
                       // already spent on `appended_reboot` above; see its
                       // own comment.
    bool changed_checked = false;
    for (unsigned slot = 0; slot < 2 && !changed_checked; ++slot) {
        if (mock::g_scene_slots[slot].size == 0) { continue; }
        probe.size = mock::g_scene_slots[slot].size;
        std::memcpy(probe.bytes, mock::g_scene_slots[slot].bytes, probe.size);
        uint64_t seq = 0;
        restored = comp::AtScene{};
        if (bench::decodeScene(probe, topology, restored, seq) == ESP_OK && restored.count == 1 &&
            restored.segments[0].id == 7) {
            changed_checked = true;
            restored = comp::AtScene{};
            CHECK(bench::decodeScene(probe, changed, restored, seq) != ESP_OK && restored.count == 0);
        }
    }
    CHECK(changed_checked);
}
void recordBoundaryTests()
{
    static comp::AtScene original, decoded;
    static SceneRecord record, bad;
    original = comp::AtScene{};
    original.count = 1; original.segments[0].id = 1; original.segments[0].stop = 50;
    CHECK(bench::encodeScene(original, bench::defaultTopology(), 1, record) == ESP_OK);
    Topology appended = bench::defaultTopology();
    appended.count = 2; appended.outputs[1] = OutputSpec{17, 20, 0};
    bad = record;
    bad.bytes[31 + 6] = 51; // invalid on the stored topology even though the device now has 70 pixels
    renewCrc(bad);
    uint64_t generation = 999;
    decoded.count = 7;
    CHECK(bench::decodeScene(bad, appended, decoded, generation) == ESP_ERR_INVALID_ARG);
    CHECK(decoded.count == 7 && generation == 999);
    for (size_t size = 8; size < 35; ++size) {
        bad = record; bad.size = size;
        bad.bytes[6] = static_cast<uint8_t>(size); bad.bytes[7] = 0;
        renewCrc(bad);
        CHECK(bench::decodeScene(bad, appended, decoded, generation) != ESP_OK);
        CHECK(decoded.count == 7 && generation == 999);
    }
}
} // namespace

int run_scene_tests()
{
    checks = 0; failures = 0;
    codecTests(); journalTests(); recordBoundaryTests();
    std::printf("SCENE checks=%d failures=%d\n", checks, failures);
    return failures;
}
int scene_check_count() { return checks; }
