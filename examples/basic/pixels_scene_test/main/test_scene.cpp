#include "scene_store.hpp"
#include "nvs_mock.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>

namespace {
namespace comp = esp_pixels_composer;
namespace mock = bench_mock;
using bench::MapConfig;
using bench::MapWiring;
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
    const Topology topology = bench::defaultTopology(); // one output
    const MapConfig identity{}; // schema 3, one output, identity map: 38-byte header
    scene.count = comp::MAX_SEGMENTS;
    scene.brightness = 71;
    scene.paused = true;
    constexpr uint8_t effects1D[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 18, 19}; // 2D-only ids are exercised below.
    constexpr unsigned kEffects1DCount = sizeof(effects1D) / sizeof(effects1D[0]);
    for (unsigned i = 0; i < scene.count; ++i) {
        auto &s = scene.segments[i];
        s.id = 100 + i; s.start = i; s.stop = i + 1; s.effect_id = effects1D[i % kEffects1DCount];
        s.brightness = 200 - i; s.blend = static_cast<comp::BlendMode>(i % 3);
        s.opacity = 71 + i; s.reversed = (i % 2) != 0; s.mirror = (i % 3) == 0; // flags byte covers all 4 combos
        s.state = static_cast<comp::SegmentState>(i % 3); s.restart_generation = 0xffff0000U + i;
        for (unsigned c = 0; c < 3; ++c) { s.params.color[c] = {uint8_t(i+c), uint8_t(53+i+c), uint8_t(201-i-c)}; }
        s.params.speed = i + 7; s.params.intensity = 231 - i; s.params.palette = i % 10;
        s.params.color_source = static_cast<esp_pixels_fx::FxColorSource>(i % 2);
        s.params.custom[0] = i; s.params.custom[1] = 65 + i; s.params.custom[2] = 250 - i;
    }
    constexpr uint64_t generation = 0x123456789abcdef0ULL;
    // Schema 5, one output, identity map: magic(4)+schema(2)+
    // size(2)+generation(8)+output_count(1)+1*(pin2+count2+order1=5)+
    // map(width2+height2+first2+wiring1=7)+scene.count(1)+brightness(1)+
    // paused(1)+reserved(2)+fx_catalog(4) = 38 -- byte-identical to schema
    // 3's own header (kSceneSegmentBytesV3 covers both), only the per-
    // segment flags byte's own MEANING differs; see scene_codec.hpp's own
    // doc.
    constexpr size_t kHeaderBytes = 38;
    const esp_err_t encoded = bench::encodeScene(scene, topology, identity, generation, record);
    CHECK(encoded == ESP_OK);
    if (encoded != ESP_OK) { return; } // Do not corrupt/CRC an uninitialized record after a failed fixture.
    CHECK(record.size == kHeaderBytes + comp::MAX_SEGMENTS * bench::kSceneSegmentBytesV3 + 4);
    CHECK(record.bytes[0] == 'S' && record.bytes[1] == 'C' && record.bytes[2] == 'N' && record.bytes[3] == '1');
    CHECK(record.bytes[4] == 5 && record.bytes[5] == 0); // schema field: encodeScene() now writes schema 5
    CHECK(record.bytes[8] == 0xf0 && record.bytes[15] == 0x12);
    // Segment 0's own id(4)/start(2)/stop(2) LSBs, right after the header.
    CHECK(record.bytes[kHeaderBytes] == 100 && record.bytes[kHeaderBytes + 4] == 0 &&
          record.bytes[kHeaderBytes + 6] == 1);
    uint64_t found = 0;
    CHECK(bench::decodeScene(record, topology, identity, decoded, found) == ESP_OK && found == generation);
    // Task 7G: mirror survives the round trip exactly (i%3==0 above), for
    // every one of the 32 segments, not just one sampled byte.
    for (unsigned i = 0; i < comp::MAX_SEGMENTS; ++i) {
        CHECK(decoded.segments[i].mirror == ((i % 3) == 0));
        CHECK(decoded.segments[i].reversed == ((i % 2) != 0)); // unaffected by the flags-byte reinterpretation
    }
    CHECK(bench::encodeScene(decoded, topology, identity, found, reencoded) == ESP_OK);
    CHECK(record.size == reencoded.size && std::memcmp(record.bytes, reencoded.bytes, record.size) == 0);

    // Schema 4 remains readable with the same layout and flags semantics.
    reencoded = record;
    reencoded.bytes[4] = 4;
    renewCrc(reencoded);
    decoded = comp::AtScene{}; found = 0;
    CHECK(bench::decodeScene(reencoded, topology, identity, decoded, found) == ESP_OK &&
          found == generation && decoded.segments[0].mirror);

    // Task 7G: the SAME bytes, with ONLY the schema field changed to 3,
    // prove schema 3 keeps its own strict 0/1 reversed-byte validation --
    // never reinterpreting a schema-4 mirror bit as a valid schema-3 value.
    // Segment 0 itself (i=0: reversed=false, mirror=true) has flags==2,
    // deliberately not a valid strict-0/1 schema-3 byte.
    reencoded = record;
    reencoded.bytes[4] = 3; // schema 3, same bytes otherwise
    renewCrc(reencoded);
    decoded.count = 7; found = 123;
    CHECK(bench::decodeScene(reencoded, topology, identity, decoded, found) == ESP_ERR_INVALID_ARG &&
          decoded.count == 7 && found == 123); // segment 0's own flags==2 is not a valid schema-3 reversed byte
    // Strip bit1 (mirror) from every segment's own flags byte so the SAME
    // record becomes a genuinely valid schema-3 one (strict 0/1 reversed,
    // mirror always false) -- proves schema 3 decodes correctly on its own
    // terms too, not just that schema 4's flags byte is rejected by it.
    for (unsigned i = 0; i < comp::MAX_SEGMENTS; ++i) {
        reencoded.bytes[kHeaderBytes + i * bench::kSceneSegmentBytesV3 + 12] &= 1U;
    }
    renewCrc(reencoded);
    decoded = comp::AtScene{};
    found = 0;
    CHECK(bench::decodeScene(reencoded, topology, identity, decoded, found) == ESP_OK && found == generation);
    for (unsigned i = 0; i < comp::MAX_SEGMENTS; ++i) {
        CHECK(!decoded.segments[i].mirror);                       // schema 3 never sets mirror
        CHECK(decoded.segments[i].reversed == ((i % 2) != 0));    // the original bit0 value survived the strip
    }

    // A single damaged byte anywhere, including headers/CRC, is rejected transactionally.
    for (size_t i = 0; i < record.size; ++i) {
        record.bytes[i] ^= 0x80;
        decoded.count = 7; found = 123;
        CHECK(bench::decodeScene(record, topology, identity, decoded, found) != ESP_OK && decoded.count == 7 &&
              found == 123);
        record.bytes[i] ^= 0x80;
    }
    const size_t full_size = record.size;
    for (size_t size : {size_t(0), size_t(20), size_t(kHeaderBytes), full_size - 1, full_size + 1}) {
        record.size = size;
        CHECK(bench::decodeScene(record, topology, identity, decoded, found) != ESP_OK);
    }
    record.size = full_size;
    // Semantically invalid but CRC-valid records: header fields (schema,
    // output_count, pin, count, map wiring, scene.count, paused, reserved)
    // and segment fields (effect_id, blend, flags [255 has bits beyond
    // reversed/mirror set -- Task 7G's own "reject unknown flags bits"
    // path], state, palette, color_source, is2D).
    const size_t offsets[] = {4, 16, 17, 18, 20, 21, 28, 29, 31, 33,
                              kHeaderBytes + 8, kHeaderBytes + 10, kHeaderBytes + 12, kHeaderBytes + 13,
                              kHeaderBytes + 29, kHeaderBytes + 30, kHeaderBytes + 34};
    for (size_t offset : offsets) {
        reencoded = record; reencoded.bytes[offset] = 255; renewCrc(reencoded);
        decoded.count = 7; found = 123;
        CHECK(bench::decodeScene(reencoded, topology, identity, decoded, found) != ESP_OK && decoded.count == 7 &&
              found == 123);
    }
    reencoded = record;
    std::memcpy(reencoded.bytes + kHeaderBytes + bench::kSceneSegmentBytesV3, reencoded.bytes + kHeaderBytes, 4);
    renewCrc(reencoded);
    CHECK(bench::decodeScene(reencoded, topology, identity, decoded, found) != ESP_OK); // Duplicate IDs.
    scene.segments[1].id = scene.segments[0].id;
    CHECK(bench::encodeScene(scene, topology, identity, 1, reencoded) != ESP_OK);
    scene.count = 1; scene.segments[0].is2D = true; scene.segments[0].w = 1; scene.segments[0].h = 1;
    scene.segments[0].reversed = true; // 2D segments must never carry reversed==true
    CHECK(bench::encodeScene(scene, topology, identity, 1, reencoded) != ESP_OK);
    scene.segments[0].reversed = false;
    scene.segments[0].mirror = true; // Task 7G: nor mirror==true, even at h==1
    CHECK(bench::encodeScene(scene, topology, identity, 1, reencoded) != ESP_OK);
    scene.segments[0].mirror = false;
    // Task 7G: a raw, hand-crafted CRC-valid record with is2D==1 AND the
    // mirror bit set (a combination no legitimate encoder ever produces,
    // since encodeScene() itself already rejects it above) must still be
    // rejected on DECODE, before any output is mutated -- valid()'s own
    // is2D branch runs inside decodeSegmentV4() exactly as it does inside
    // encodeScene()'s own validation, so a corrupted/foreign record can
    // never sneak a mirrored-2D segment through either path.
    reencoded = record;
    reencoded.bytes[kHeaderBytes + 34] = 1;      // segment 0: is2D = true
    reencoded.bytes[kHeaderBytes + 12] |= 2U;    // segment 0: flags bit1 (mirror) set
    renewCrc(reencoded);
    decoded.count = 7; found = 123;
    CHECK(bench::decodeScene(reencoded, topology, identity, decoded, found) != ESP_OK && decoded.count == 7 &&
          found == 123);
    scene.segments[0].is2D = false; scene.segments[0].stop = topology.totalPixels() + 1;
    CHECK(bench::encodeScene(scene, topology, identity, 1, reencoded) != ESP_OK);
    scene.count = 0;
    CHECK(bench::encodeScene(scene, topology, identity, 1, reencoded) == ESP_OK && reencoded.size == kHeaderBytes + 4);
    CHECK(bench::decodeScene(reencoded, topology, identity, decoded, found) == ESP_OK && decoded.count == 0 &&
          decoded.paused);
    CHECK(bench::encodeScene(scene, topology, identity, 0, reencoded) != ESP_OK);
    CHECK(bench::encodeScene(scene, Topology{}, identity, 1, reencoded) != ESP_OK); // count==0: invalid topology
    CHECK(bench::encodeScene(scene, topology, MapConfig{1, 0, 0, MapWiring::Progressive}, 1, reencoded) !=
          ESP_OK); // height==0, not identity: invalid map

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
    CHECK(bench::encodeScene(scene, two, identity, 1, reencoded) == ESP_OK);
    decoded = comp::AtScene{};
    CHECK(bench::decodeScene(reencoded, two, identity, decoded, found) == ESP_OK && decoded.count == 3);
    CHECK(decoded.segments[0].start == 0 && decoded.segments[0].stop == 50);
    CHECK(decoded.segments[1].start == 50 && decoded.segments[1].stop == 70);
    CHECK(decoded.segments[2].start == 45 && decoded.segments[2].stop == 55); // the crossing segment, intact

    // One past the combined total (71) is out of range regardless of which
    // output it would fall on; exactly at the total (70) is the crossing
    // segment's own valid upper bound, already proven above.
    scene.count = 1;
    scene.segments[0] = comp::SegmentSpec{};
    scene.segments[0].id = 1; scene.segments[0].stop = 71;
    CHECK(bench::encodeScene(scene, two, identity, 1, reencoded) != ESP_OK);

    // A real (non-identity) 5x14 matrix over the same 70-pixel two-output
    // topology: one 2D segment filling the whole canvas, one 1D segment
    // addressing the flat row-major canvas space directly (not the
    // physical/logical space) -- both validated against the CANVAS
    // (width*height), not totalPixels() directly (identical here, 70==70,
    // but the two are conceptually distinct spaces per Design v2 SS7).
    const MapConfig matrix{5, 14, 0, MapWiring::Serpentine};
    CHECK(matrix.valid(two.totalPixels()));
    scene = comp::AtScene{};
    scene.count = 2;
    scene.segments[0].is2D = true;
    scene.segments[0].id = 1; scene.segments[0].x = 0; scene.segments[0].y = 0;
    scene.segments[0].w = 5; scene.segments[0].h = 14;
    scene.segments[1].id = 2; scene.segments[1].start = 0; scene.segments[1].stop = 70; // flat canvas range
    CHECK(bench::encodeScene(scene, two, matrix, 1, reencoded) == ESP_OK);
    decoded = comp::AtScene{};
    CHECK(bench::decodeScene(reencoded, two, matrix, decoded, found) == ESP_OK && decoded.count == 2);
    CHECK(decoded.segments[0].is2D && decoded.segments[0].w == 5 && decoded.segments[0].h == 14);
    CHECK(!decoded.segments[1].is2D && decoded.segments[1].stop == 70);
    // 2D pixel effects batch 1: effect_id is an opaque byte at the codec
    // level (verified against the actual code, not assumed -- see this
    // file's own "Persistence" note), but IDs 12..16 are genuinely 2D-only
    // at the FX registry level, so this reuses the SAME real h>1 matrix
    // geometry above (never the 1D-only effects1D[] fixture) to prove each
    // one specifically survives a 2D-segment round trip.
    for (uint8_t id2d : {uint8_t{12}, uint8_t{13}, uint8_t{14}, uint8_t{15}, uint8_t{16}}) {
        scene.segments[0].effect_id = id2d;
        CHECK(bench::encodeScene(scene, two, matrix, 1, reencoded) == ESP_OK);
        decoded = comp::AtScene{};
        CHECK(bench::decodeScene(reencoded, two, matrix, decoded, found) == ESP_OK);
        CHECK(decoded.segments[0].is2D && decoded.segments[0].effect_id == id2d &&
              decoded.segments[0].w == 5 && decoded.segments[0].h == 14);
    }
    // The SAME bytes rejected under the identity canvas (record embeds its
    // own real map, isCompatibleMap() requires an EXACT match).
    CHECK(bench::decodeScene(reencoded, two, identity, decoded, found) == ESP_ERR_NOT_SUPPORTED);

    // Schema 5 persists column-serpentine as stable wiring code 2 without
    // changing record size. Schema 4 keeps its historical 0..1 validation.
    const MapConfig column_matrix{5, 14, 0, MapWiring::ColumnSerpentine};
    CHECK(bench::encodeScene(scene, two, column_matrix, 1, reencoded) == ESP_OK);
    const size_t map_wiring_offset = 23 + static_cast<size_t>(two.count) * 5;
    CHECK(reencoded.bytes[4] == 5 && reencoded.bytes[5] == 0 &&
          reencoded.bytes[map_wiring_offset] == 2);
    decoded = comp::AtScene{};
    CHECK(bench::decodeScene(reencoded, two, column_matrix, decoded, found) == ESP_OK && decoded.count == 2);
    reencoded.bytes[4] = 4;
    renewCrc(reencoded);
    CHECK(bench::decodeStoredScene(reencoded, decoded, found) == ESP_ERR_INVALID_ARG);

    // A 2D segment one column past the matrix width is rejected.
    scene.segments[0].w = 6;
    CHECK(bench::encodeScene(scene, two, matrix, 1, reencoded) != ESP_OK);
}

void journalTests()
{
    static comp::AtScene scene, restored;
    static bench::SceneStore store, reboot, torn_reboot, failed_init;
    const Topology topology = bench::defaultTopology();
    const MapConfig identity{};
    CHECK(store.load(topology, identity, scene) == ESP_ERR_NOT_FOUND);
    CHECK(mock::g_scene_writes == 0 && mock::g_scene_commits == 0);
    scene.count = 1; scene.segments[0].id = 1; scene.segments[0].stop = 25;
    scene.segments[0].params.color[0] = {15, 25, 35};
    CHECK(store.save(topology, identity, scene) == ESP_OK);
    CHECK(mock::g_scene_writes == 1 && mock::g_scene_commits == 1 &&
          mock::g_scene_slots[0].size != 0 && mock::g_scene_slots[1].size == 0);
    CHECK(store.save(topology, identity, scene) == ESP_OK && mock::g_scene_writes == 1); // Unchanged config: no wear.
    scene.paused = true;
    CHECK(store.save(topology, identity, scene) == ESP_OK && mock::g_scene_writes == 2 &&
          mock::g_scene_slots[1].size != 0);
    CHECK(reboot.load(topology, identity, restored) == ESP_OK && restored.paused && restored.count == 1);
    CHECK(restored.segments[0].params.color[0].g == 25);
    mock::g_scene_slots[1].bytes[40] ^= 1; // Latest slot damaged: previous complete slot survives.
    CHECK(torn_reboot.load(topology, identity, restored) == ESP_OK && !restored.paused && restored.count == 1);
    const mock::SceneMockSlot previous = mock::g_scene_slots[0];
    mock::g_scene_fail_write = true;
    CHECK(torn_reboot.save(topology, identity, scene) == ESP_FAIL);
    CHECK(mock::g_scene_slots[0].size == previous.size &&
          std::memcmp(mock::g_scene_slots[0].bytes, previous.bytes, previous.size) == 0);
    mock::g_scene_fail_write = false; mock::g_scene_fail_commit = true;
    CHECK(torn_reboot.save(topology, identity, scene) == ESP_FAIL);
    CHECK(std::memcmp(mock::g_scene_slots[0].bytes, previous.bytes, previous.size) == 0);
    mock::g_scene_fail_commit = false; mock::g_scene_fail_readback = true;
    CHECK(torn_reboot.save(topology, identity, scene) == ESP_FAIL);
    mock::g_scene_fail_readback = false;
    CHECK(torn_reboot.save(topology, identity, scene) == ESP_OK); // Failure never poisons cache/retry.
    scene.count = 0;
    CHECK(torn_reboot.save(topology, identity, scene) == ESP_OK); // Empty configuration is durable too.
    mock::g_scene_init_error = ESP_FAIL;
    CHECK(failed_init.load(topology, identity, restored) == ESP_FAIL);
    CHECK(failed_init.save(topology, identity, scene) == ESP_FAIL); // Never erase/reinitialize on failure.
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
    CHECK(torn_reboot.save(topology, identity, scene) == ESP_OK);

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
    CHECK(appended_reboot.load(appended, identity, appended_scene) == ESP_OK);
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
        if (bench::decodeScene(probe, topology, identity, restored, seq) == ESP_OK && restored.count == 1 &&
            restored.segments[0].id == 7) {
            changed_checked = true;
            restored = comp::AtScene{};
            CHECK(bench::decodeScene(probe, changed, identity, restored, seq) != ESP_OK && restored.count == 0);
        }
    }
    CHECK(changed_checked);

    // A zero-offset progressive matrix preserves this all-1D record's flat
    // domain. Serpentine changes that mapping and must reject the same bytes.
    const MapConfig later_matrix{5, 10, 0, MapWiring::Progressive};
    restored = comp::AtScene{};
    uint64_t map_seq = 0;
    CHECK(bench::decodeScene(probe, topology, later_matrix, restored, map_seq) == ESP_OK && restored.count == 1);
    const MapConfig remapped{5, 10, 0, MapWiring::Serpentine};
    CHECK(bench::decodeScene(probe, topology, remapped, restored, map_seq) == ESP_ERR_NOT_SUPPORTED);
}

void legacySchemaTests()
{
    std::unique_ptr<comp::AtScene> decoded(new (std::nothrow) comp::AtScene{});
    if (!decoded) { CHECK(false); return; }
    SceneRecord record{};
    const Topology topology = bench::defaultTopology();
    for (unsigned schema : {1U, 2U}) {
        record = SceneRecord{};
        const size_t header = schema == 1 ? 32 : 31;
        record.size = header + 34 + 4;
        uint8_t *b = record.bytes;
        b[0] = 'S'; b[1] = 'C'; b[2] = 'N'; b[3] = '1';
        b[4] = schema; b[6] = record.size; b[8] = 7;
        if (schema == 1) { b[16] = 16; b[18] = 50; b[20] = 1; b[30] = 2; }
        else { b[16] = 1; b[17] = 16; b[19] = 50; b[29] = 2; }
        b[22] = 1; b[23] = 96;
        uint8_t *s = b + header;
        s[0] = 7; s[4] = 3; s[6] = 4; s[9] = 255; s[11] = 255;
        s[18] = 255; s[27] = 128; s[28] = 128;
        s[30] = esp_pixels_fx::FX_COLOR_SOURCE_COLORS;
        s[31] = 11; s[32] = 22; s[33] = 33;
        renewCrc(record);
        uint64_t generation = 0;
        CHECK(bench::decodeScene(record, topology, MapConfig{}, *decoded, generation) == ESP_OK);
        CHECK(generation == 7 && decoded->count == 1 && decoded->brightness == 96);
        CHECK(!decoded->segments[0].is2D && decoded->segments[0].id == 7 &&
              decoded->segments[0].start == 3 && decoded->segments[0].stop == 4);
        CHECK(decoded->segments[0].params.color[0].r == 255 &&
              decoded->segments[0].params.custom[2] == 33);
        CHECK(!decoded->segments[0].mirror); // Task 7G: schemas 1/2 predate the field entirely -- always false
        CHECK(bench::decodeScene(record, topology, MapConfig{5,10,0,MapWiring::Progressive},
                                *decoded, generation) == ESP_OK);
        decoded->count = 9; generation = 99;
        CHECK(bench::decodeScene(record, topology, MapConfig{5,10,0,MapWiring::Serpentine},
                                *decoded, generation) == ESP_ERR_NOT_SUPPORTED);
        CHECK(decoded->count == 9 && generation == 99);
    }
}

void recordBoundaryTests()
{
    static comp::AtScene original, decoded;
    static SceneRecord record, bad;
    const MapConfig identity{};
    original = comp::AtScene{};
    original.count = 1; original.segments[0].id = 1; original.segments[0].stop = 50;
    CHECK(bench::encodeScene(original, bench::defaultTopology(), identity, 1, record) == ESP_OK);
    Topology appended = bench::defaultTopology();
    appended.count = 2; appended.outputs[1] = OutputSpec{17, 20, 0};
    constexpr size_t kHeaderBytes = 38; // one output, identity map -- see codecTests()'s own comment
    bad = record;
    bad.bytes[kHeaderBytes + 6] = 51; // invalid on the stored topology even though the device now has 70 pixels
    renewCrc(bad);
    uint64_t generation = 999;
    decoded.count = 7;
    CHECK(bench::decodeScene(bad, appended, identity, decoded, generation) == ESP_ERR_INVALID_ARG);
    CHECK(decoded.count == 7 && generation == 999);
    for (size_t size = 8; size < kHeaderBytes + 4; ++size) {
        bad = record; bad.size = size;
        bad.bytes[6] = static_cast<uint8_t>(size); bad.bytes[7] = 0;
        renewCrc(bad);
        CHECK(bench::decodeScene(bad, appended, identity, decoded, generation) != ESP_OK);
        CHECK(decoded.count == 7 && generation == 999);
    }
}
} // namespace

int run_scene_tests()
{
    checks = 0; failures = 0;
    codecTests(); journalTests(); recordBoundaryTests(); legacySchemaTests();
    std::printf("SCENE checks=%d failures=%d\n", checks, failures);
    return failures;
}
int scene_check_count() { return checks; }
