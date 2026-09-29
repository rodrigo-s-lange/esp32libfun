// On-target gates for the application's named-preset feature: preset_codec.*
// (direct, no NVS), PresetStore (through nvs_mock.hpp's linker-wrapped
// mock, matching test_scene.cpp's own convention), and the "PIXELS_PRESETS"
// AT library (presets_at.*) wired to a real Composer + FakeSink. No real
// NVS/partition access and no physical pixel adapter anywhere in this file.
//
// Every gate below except the dedicated reboot-recovery one and the
// capacity one shares ONE `PresetStore` instance (each is ~7 KB: a 100-
// entry metadata cache plus two maximum-size scratch buffers) -- creating
// one per gate would not fit this image's own static RAM budget. Shared
// state means every gate uses its own globally distinct preset name and
// never relies on a name any other gate might reuse or remove; the
// capacity gate (which must fill every remaining position) runs last, and
// nothing after it ever saves a NEW name.

#include "preset_codec.hpp"
#include "preset_store.hpp"
#include <new>
#include "presets_at.hpp"
#include "nvs_mock.hpp"
#include "esp_pixels_composer.hpp"
#include "esp_pixels_fx_ids.hpp"
#include "esp32libfun_at.hpp"
#include "fake_sink.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
namespace comp = esp_pixels_composer;
namespace mock = bench_mock;
int checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    std::printf("PRESET FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

// --- preset_codec.* direct gates (no store, no NVS) ------------------------

void renewPresetCrc(bench::PresetRecord &r)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < r.size - 4; ++i) {
        crc ^= r.bytes[i];
        for (unsigned j = 0; j < 8; ++j) { crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0); }
    }
    crc = ~crc;
    for (unsigned j = 0; j < 4; ++j) { r.bytes[r.size - 4 + j] = crc >> (8 * j); }
}

void presetCodecTests()
{
    static comp::AtScene scene, decoded;
    static bench::PresetRecord record, reencoded;
    // Two outputs: hits kPresetRecordBytes' own worst case exactly (32
    // segments, two configured outputs).
    bench::Topology topology2;
    topology2.count = 2;
    topology2.outputs[0] = bench::OutputSpec{bench::kDefaultPin, bench::kDefaultCount, bench::kDefaultOrder};
    topology2.outputs[1] = bench::OutputSpec{4, 20, 0};
    scene.count = comp::MAX_SEGMENTS;
    scene.brightness = 60;
    scene.paused = true;
    constexpr uint8_t effects1D[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11}; // XYGradient (4) requires 2D geometry.
    constexpr unsigned kEffects1DCount = sizeof(effects1D) / sizeof(effects1D[0]);
    for (unsigned i = 0; i < scene.count; ++i) {
        auto &s = scene.segments[i];
        s.id = 200 + i; s.start = i; s.stop = i + 1; s.effect_id = effects1D[i % kEffects1DCount];
        s.mirror = (i % 3) == 0; // Task 7G: schema-4 flags byte covered at the preset (opaque-blob) level too
        s.params.color[0] = {static_cast<uint8_t>(i), static_cast<uint8_t>(10 + i), static_cast<uint8_t>(20 + i)};
        s.params.custom[0] = static_cast<uint8_t>(i); // Task 7H: custom bytes covered at the preset level too
        s.params.custom[1] = static_cast<uint8_t>(30 + i);
        s.params.custom[2] = static_cast<uint8_t>(220 - i);
    }
    constexpr uint64_t generation = 0xabcdef0123456789ULL;
    static char name31[bench::kPresetNameMaxLen + 1];
    for (size_t i = 0; i < bench::kPresetNameMaxLen; ++i) { name31[i] = static_cast<char>('A' + (i % 26)); }
    name31[bench::kPresetNameMaxLen] = '\0';

    const esp_err_t encoded = bench::encodePreset(name31, bench::kPresetNameMaxLen, scene,
                                                 topology2, bench::MapConfig{}, generation, record);
    CHECK(encoded == ESP_OK);
    if (encoded != ESP_OK) { return; } // Do not corrupt/CRC an uninitialized record after a failed fixture.
    CHECK(record.size == bench::kPresetRecordBytes);
    CHECK(record.bytes[0] == 'P' && record.bytes[1] == 'S' && record.bytes[2] == 'T' && record.bytes[3] == '1');

    char decoded_name[bench::kPresetNameMaxLen + 1];
    size_t decoded_name_len = 0;
    uint64_t found = 0;
    CHECK(bench::decodePreset(record, topology2, bench::MapConfig{}, decoded_name, decoded_name_len, decoded, found) == ESP_OK);
    CHECK(found == generation);
    CHECK(decoded_name_len == bench::kPresetNameMaxLen &&
          std::memcmp(decoded_name, name31, bench::kPresetNameMaxLen) == 0);
    CHECK(decoded.count == comp::MAX_SEGMENTS && decoded.brightness == 60 && decoded.paused);
    for (unsigned i = 0; i < comp::MAX_SEGMENTS; ++i) {
        CHECK(decoded.segments[i].mirror == ((i % 3) == 0)); // survives the full preset (schema-4 inner scene) round trip
        CHECK(decoded.segments[i].effect_id == effects1D[i % kEffects1DCount]);
        CHECK(decoded.segments[i].params.custom[0] == static_cast<uint8_t>(i));
        CHECK(decoded.segments[i].params.custom[1] == static_cast<uint8_t>(30 + i));
        CHECK(decoded.segments[i].params.custom[2] == static_cast<uint8_t>(220 - i));
    }

    CHECK(bench::encodePreset(decoded_name, decoded_name_len, decoded, topology2, bench::MapConfig{}, found, reencoded) == ESP_OK);
    CHECK(record.size == reencoded.size && std::memcmp(record.bytes, reencoded.bytes, record.size) == 0);

    // Exhaustive per-byte corruption, same discipline scene_codec.hpp's own
    // codecTests() (test_scene.cpp) already established.
    for (size_t i = 0; i < record.size; ++i) {
        record.bytes[i] ^= 0x80;
        decoded.count = 7; found = 123;
        CHECK(bench::decodePreset(record, topology2, bench::MapConfig{}, decoded_name, decoded_name_len, decoded, found) != ESP_OK &&
              decoded.count == 7 && found == 123);
        record.bytes[i] ^= 0x80;
    }

    const size_t full_size = record.size;
    for (size_t size : {size_t(0), size_t(80), full_size - 1, full_size + 1}) {
        record.size = size;
        CHECK(bench::decodePreset(record, topology2, bench::MapConfig{}, decoded_name, decoded_name_len, decoded, found) != ESP_OK);
    }
    record.size = full_size;

    // Header layout: magic[0:4) schema[4:6) size[6:8) generation[8:16)
    // name_len[16] name[17:49) scene[49:49+N) crc[size-4:size).
    reencoded = record;
    reencoded.bytes[16] = 0; // name_len == 0
    renewPresetCrc(reencoded);
    CHECK(bench::decodePreset(reencoded, topology2, bench::MapConfig{}, decoded_name, decoded_name_len, decoded, found) != ESP_OK);

    reencoded = record;
    reencoded.bytes[16] = static_cast<uint8_t>(bench::kPresetNameMaxLen + 1); // name_len too large
    renewPresetCrc(reencoded);
    CHECK(bench::decodePreset(reencoded, topology2, bench::MapConfig{}, decoded_name, decoded_name_len, decoded, found) != ESP_OK);

    reencoded = record;
    reencoded.bytes[16] = 5; // shrinks the declared name; bytes 5..31 of the
                             // 32-byte field are still the original letters,
                             // so this also exercises "padding must be zero".
    renewPresetCrc(reencoded);
    CHECK(bench::decodePreset(reencoded, topology2, bench::MapConfig{}, decoded_name, decoded_name_len, decoded, found) != ESP_OK);

    // Outer generation changed independently of the embedded scene's own
    // (unchanged) generation -- the cross-check must catch it.
    reencoded = record;
    reencoded.bytes[9] ^= 0xFF;
    renewPresetCrc(reencoded);
    decoded.count = 7;
    decoded.brightness = 219;
    found = 123;
    std::strcpy(decoded_name, "unchanged");
    decoded_name_len = 9;
    CHECK(bench::decodePreset(reencoded, topology2, bench::MapConfig{}, decoded_name, decoded_name_len, decoded, found) != ESP_OK);
    CHECK(decoded.count == 7 && decoded.brightness == 219 && found == 123);
    CHECK(decoded_name_len == 9 && std::strcmp(decoded_name, "unchanged") == 0);

    CHECK(bench::encodePreset(name31, bench::kPresetNameMaxLen, scene, topology2, bench::MapConfig{}, 0, reencoded) != ESP_OK); // sequence == 0

    CHECK(bench::validPresetName(name31, bench::kPresetNameMaxLen));
    CHECK(!bench::validPresetName(name31, 0));
    CHECK(!bench::validPresetName(name31, bench::kPresetNameMaxLen + 1));
    CHECK(!bench::validPresetName("bad\nname", 8));
    CHECK(!bench::validPresetName(nullptr, 3));

    // An empty scene still encodes/decodes to a small, valid record --
    // single output this time, so the inner scene record itself is smaller.
    const bench::Topology topology1 = bench::defaultTopology();
    scene = comp::AtScene{};
    CHECK(bench::encodePreset("Empty", 5, scene, topology1, bench::MapConfig{}, 1, reencoded) == ESP_OK);
    CHECK(reencoded.size == bench::kPresetHeaderBytes + 38 + 4 + 4); // schema 3, one output, identity map; inner and outer CRC32
    decoded.count = 7;
    CHECK(bench::decodePreset(reencoded, topology1, bench::MapConfig{}, decoded_name, decoded_name_len, decoded, found) == ESP_OK &&
          decoded.count == 0);
}

// --- PresetStore gates (through the linker-wrapped mock) -------------------

comp::AtScene makeScene(uint32_t id, uint16_t stop, uint8_t r)
{
    comp::AtScene s{};
    s.count = 1;
    s.segments[0].id = id;
    s.segments[0].stop = stop;
    s.segments[0].params.color[0] = {r, 0, 0};
    return s;
}

struct NameCollector {
    unsigned count = 0;
    char names[bench::kPresetSlotCount][bench::kPresetNameMaxLen + 1]{};
};

void collectNames(void *context, const bench::PresetInfo &info)
{
    auto *c = static_cast<NameCollector *>(context);
    if (c->count < bench::kPresetSlotCount) { std::strcpy(c->names[c->count], info.name); }
    ++c->count;
}

void presetStoreTwoNamesReplaceDeleteTest(bench::PresetStore &store)
{
    static comp::AtScene a, b, loaded;
    a = makeScene(1, 10, 11);
    b = makeScene(2, 20, 22);
    CHECK(store.save("Alpha", 5, a) == ESP_OK);
    CHECK(store.save("Beta", 4, b) == ESP_OK);
    CHECK(mock::g_preset_writes == 2 && mock::g_preset_commits == 2);

    loaded = comp::AtScene{};
    CHECK(store.load("Alpha", 5, loaded) == ESP_OK && loaded.segments[0].id == 1 &&
          loaded.segments[0].params.color[0].r == 11);
    loaded = comp::AtScene{};
    CHECK(store.load("Beta", 4, loaded) == ESP_OK && loaded.segments[0].id == 2 &&
          loaded.segments[0].params.color[0].r == 22);

    static NameCollector list1;
    store.list(collectNames, &list1);
    CHECK(list1.count == 2 && std::strcmp(list1.names[0], "Alpha") == 0 &&
          std::strcmp(list1.names[1], "Beta") == 0);

    // Replace: same name, new content, same position (the OTHER A/B key).
    static comp::AtScene a2;
    a2 = makeScene(9, 5, 99);
    CHECK(store.save("Alpha", 5, a2) == ESP_OK);
    loaded = comp::AtScene{};
    CHECK(store.load("Alpha", 5, loaded) == ESP_OK && loaded.segments[0].id == 9 &&
          loaded.segments[0].params.color[0].r == 99);
    static NameCollector list2;
    store.list(collectNames, &list2);
    CHECK(list2.count == 2); // replace never grows occupancy

    // Delete "Beta" -- "Alpha" survives for later gates.
    CHECK(store.remove("Beta", 4) == ESP_OK);
    loaded = comp::AtScene{};
    CHECK(store.load("Beta", 4, loaded) == ESP_ERR_NOT_FOUND);
    CHECK(store.remove("Beta", 4) == ESP_ERR_NOT_FOUND); // already gone
    CHECK(store.remove("NoSuchName", 10) == ESP_ERR_NOT_FOUND);
    static NameCollector list3;
    store.list(collectNames, &list3);
    CHECK(list3.count == 1 && std::strcmp(list3.names[0], "Alpha") == 0);

    // Invalid names never save.
    CHECK(store.save("", 0, a) == ESP_ERR_INVALID_ARG);
    CHECK(store.save("bad\tname", 8, a) == ESP_ERR_INVALID_ARG);
    static char too_long[40];
    std::memset(too_long, 'x', sizeof(too_long));
    CHECK(store.save(too_long, 32, a) == ESP_ERR_INVALID_ARG); // > kPresetNameMaxLen
    static NameCollector list4;
    store.list(collectNames, &list4);
    CHECK(list4.count == 1); // none of the rejected names were saved
}

// Corrupt-record recovery needs a genuinely SEPARATE store instance (a
// fresh begin()-time scan, simulating reboot) -- the one extra ~7 KB
// instance this file spends beyond the one shared everywhere else.
void presetStoreCorruptRecoveryTest(bench::PresetStore &store)
{
    static comp::AtScene v1, v2, restored;
    v1 = makeScene(1, 4, 10);
    CHECK(store.save("Torn", 4, v1) == ESP_OK); // this name's own first write: key 'a'
    v2 = makeScene(2, 4, 20);
    CHECK(store.save("Torn", 4, v2) == ESP_OK); // same position, key 'b' (newer)

    // Find "Torn"'s own slot by probing every key pair for a size match --
    // this file has no slot-index introspection API, so it locates the
    // just-written record directly in the mock instead of assuming one.
    int torn_slot = -1;
    for (unsigned s = 0; s < mock::kPresetMockSlots && torn_slot < 0; ++s) {
        if (mock::g_preset_slots[s][1].size != 0) {
            char name_out[bench::kPresetNameMaxLen + 1];
            size_t name_len = 0;
            comp::AtScene probe_scene;
            uint64_t seq = 0;
            bench::PresetRecord probe_record;
            probe_record.size = mock::g_preset_slots[s][1].size;
            std::memcpy(probe_record.bytes, mock::g_preset_slots[s][1].bytes, probe_record.size);
            if (bench::decodePreset(probe_record, bench::defaultTopology(), bench::MapConfig{}, name_out, name_len, probe_scene, seq) ==
                    ESP_OK &&
                name_len == 4 && std::memcmp(name_out, "Torn", 4) == 0) {
                torn_slot = static_cast<int>(s);
            }
        }
    }
    CHECK(torn_slot >= 0);
    if (torn_slot < 0) { return; }

    // Damage the newer key, then re-scan from scratch (a fresh store
    // instance, simulating reboot) -- the older, still-valid key must be
    // recovered instead, same discipline SceneStore already has.
    mock::g_preset_slots[static_cast<unsigned>(torn_slot)][1].bytes[60] ^= 1;
    static bench::PresetStore reboot;
    CHECK(reboot.begin(bench::defaultTopology(), bench::MapConfig{}) == ESP_OK);
    restored = comp::AtScene{};
    CHECK(reboot.load("Torn", 4, restored) == ESP_OK && restored.segments[0].id == 1);

    // A runtime corruption AFTER begin() (not at boot) is caught by load()
    // itself directly, without needing a fresh scan.
    mock::g_preset_slots[static_cast<unsigned>(torn_slot)][0].bytes[60] ^= 1; // now both keys are damaged
    restored = comp::AtScene{};
    CHECK(reboot.load("Torn", 4, restored) != ESP_OK);
}

void presetStoreFailureRetryTest(bench::PresetStore &store)
{
    static comp::AtScene original, attempt, check;
    original = makeScene(1, 4, 10);
    CHECK(store.save("Flaky", 5, original) == ESP_OK);
    attempt = makeScene(2, 4, 20);

    mock::g_preset_fail_write = true;
    CHECK(store.save("Flaky", 5, attempt) == ESP_FAIL);
    check = comp::AtScene{};
    CHECK(store.load("Flaky", 5, check) == ESP_OK && check.segments[0].id == 1);
    mock::g_preset_fail_write = false;

    mock::g_preset_fail_commit = true;
    CHECK(store.save("Flaky", 5, attempt) == ESP_FAIL);
    check = comp::AtScene{};
    CHECK(store.load("Flaky", 5, check) == ESP_OK && check.segments[0].id == 1);
    mock::g_preset_fail_commit = false;

    mock::g_preset_fail_readback = true;
    CHECK(store.save("Flaky", 5, attempt) == ESP_FAIL);
    // The injected read fault affects every key, not only save's readback.
    // Restore reads before checking that the prior active record survived.
    mock::g_preset_fail_readback = false;
    check = comp::AtScene{};
    CHECK(store.load("Flaky", 5, check) == ESP_OK && check.segments[0].id == 1);

    CHECK(store.save("Flaky", 5, attempt) == ESP_OK); // retry really invokes storage
    check = comp::AtScene{};
    CHECK(store.load("Flaky", 5, check) == ESP_OK && check.segments[0].id == 2);
}

// --- "PIXELS_PRESETS" AT library integration gates --------------------------

struct SavedScene {
    comp::AtScene scene;
    unsigned calls = 0;
};

esp_err_t captureSave(const comp::AtScene &scene, void *context)
{
    auto *s = static_cast<SavedScene *>(context);
    s->scene = scene;
    ++s->calls;
    return ESP_OK;
}

void presetsAtExcludesStagedEditsOnSave(bench::PresetStore &store)
{
    FakeSink sink;
    sink.configure(1, 8, 8);
    static comp::Composer c;
    if (c.build(sink) != ESP_OK) { CHECK(false); return; }
    if (c.at(true) != ESP_OK) { CHECK(false); c.end(); return; }

    static comp::AtScene current;
    current = comp::AtScene{};
    static SavedScene probe;
    probe = SavedScene{};
    if (comp::attachAtPersistence(c, current, captureSave, &probe) != ESP_OK) {
        CHECK(false); c.at(false); c.end(); return;
    }
    if (bench::presetsAt(c, store, true) != ESP_OK) { CHECK(false); c.at(false); c.end(); return; }

    CHECK(esp32libfun::at.feedLine("AT+SEGSET=1,0,4,0") == ESP_OK);
    CHECK(c.start() == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+SEGPUB") == ESP_OK);
    CHECK(c.stop() == ESP_OK);
    CHECK(probe.calls == 1);

    // A pending, unpublished staged edit -- must be excluded from the save.
    CHECK(esp32libfun::at.feedLine("AT+SEGSET=2,4,8,0") == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+PRESETSAVE=Confirmed") == ESP_OK);

    static comp::AtScene saved;
    saved = comp::AtScene{};
    CHECK(store.load("Confirmed", 9, saved) == ESP_OK);
    CHECK(saved.count == 1 && saved.segments[0].id == 1); // id=2 (staged only) excluded

    bench::presetsAt(c, store, false);
    c.at(false);
    c.end();
}

void presetsAtLoadAppliesAndPersists(bench::PresetStore &store)
{
    FakeSink sink;
    sink.configure(1, 8, 8);
    static comp::Composer c;
    if (c.build(sink) != ESP_OK) { CHECK(false); return; }
    if (c.at(true) != ESP_OK) { CHECK(false); c.end(); return; }

    static comp::AtScene current;
    current = comp::AtScene{};
    static SavedScene probe;
    probe = SavedScene{};
    if (comp::attachAtPersistence(c, current, captureSave, &probe) != ESP_OK) {
        CHECK(false); c.at(false); c.end(); return;
    }

    static comp::AtScene preset_scene;
    preset_scene = comp::AtScene{};
    preset_scene.count = 1;
    preset_scene.segments[0].id = 5;
    preset_scene.segments[0].stop = 4;
    preset_scene.segments[0].params.color[0] = {12, 34, 56};
    preset_scene.brightness = 128;
    preset_scene.paused = true;
    CHECK(store.save("Loadable", 8, preset_scene) == ESP_OK);

    if (bench::presetsAt(c, store, true) != ESP_OK) { CHECK(false); c.at(false); c.end(); return; }

    CHECK(c.start() == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+PRESETLOAD=Loadable") == ESP_OK);
    CHECK(c.stop() == ESP_OK);

    CHECK(c.segmentCount() == 1);
    CHECK(c.brightness() == 128);
    CHECK(c.paused());
    CHECK(probe.calls == 1); // "save the resulting current scene for reboot" fired
    CHECK(current.count == 1 && current.segments[0].id == 5);

    bench::presetsAt(c, store, false);
    c.at(false);
    c.end();
}

void presetsAtLoadRejectsMissingOrCorrupt(bench::PresetStore &store)
{
    FakeSink sink;
    sink.configure(1, 8, 8);
    static comp::Composer c;
    if (c.build(sink) != ESP_OK) { CHECK(false); return; }

    esp_pixels_fx::FxParams params{};
    params.color[0] = {7, 8, 9};
    if (c.addSegment(0, 8, esp_pixels_fx::FX_SOLID, params) != ESP_OK) { CHECK(false); c.end(); return; }
    CHECK(c.renderFrame(0) == ESP_OK);
    FakeSink::CRGB before[8];
    for (uint16_t i = 0; i < 8; ++i) { before[i] = sink.lastSubmitPixel(i); }

    if (c.at(true) != ESP_OK) { CHECK(false); c.end(); return; }
    if (bench::presetsAt(c, store, true) != ESP_OK) { CHECK(false); c.at(false); c.end(); return; }

    CHECK(esp32libfun::at.feedLine("AT+PRESETLOAD=Nonexistent") == ESP_OK); // dispatched, rejected internally
    CHECK(c.segmentCount() == 1);
    CHECK(c.renderFrame(0) == ESP_OK);
    for (uint16_t i = 0; i < 8; ++i) {
        CHECK(sink.lastSubmitPixel(i).r == before[i].r && sink.lastSubmitPixel(i).g == before[i].g &&
              sink.lastSubmitPixel(i).b == before[i].b);
    }

    static comp::AtScene tiny;
    tiny = comp::AtScene{};
    tiny.count = 1; tiny.segments[0].id = 1; tiny.segments[0].stop = 1;
    CHECK(store.save("Corrupt", 7, tiny) == ESP_OK);
    // Locate "Corrupt"'s own key the same probing way presetStoreCorruptRecoveryTest() does.
    int slot = -1;
    for (unsigned s = 0; s < mock::kPresetMockSlots && slot < 0; ++s) {
        for (unsigned ab = 0; ab < 2 && slot < 0; ++ab) {
            if (mock::g_preset_slots[s][ab].size == 0) { continue; }
            char name_out[bench::kPresetNameMaxLen + 1];
            size_t name_len = 0;
            comp::AtScene probe_scene;
            uint64_t seq = 0;
            bench::PresetRecord probe_record;
            probe_record.size = mock::g_preset_slots[s][ab].size;
            std::memcpy(probe_record.bytes, mock::g_preset_slots[s][ab].bytes, probe_record.size);
            if (bench::decodePreset(probe_record, bench::defaultTopology(), bench::MapConfig{}, name_out, name_len, probe_scene, seq) ==
                    ESP_OK &&
                name_len == 7 && std::memcmp(name_out, "Corrupt", 7) == 0) {
                slot = static_cast<int>(s);
            }
        }
    }
    CHECK(slot >= 0);
    if (slot >= 0) {
        mock::g_preset_slots[static_cast<unsigned>(slot)][0].bytes[60] ^= 1;
        mock::g_preset_slots[static_cast<unsigned>(slot)][1].bytes[60] ^= 1;
    }
    CHECK(esp32libfun::at.feedLine("AT+PRESETLOAD=Corrupt") == ESP_OK); // dispatched, rejected internally
    CHECK(c.segmentCount() == 1);
    CHECK(c.renderFrame(0) == ESP_OK);
    for (uint16_t i = 0; i < 8; ++i) {
        CHECK(sink.lastSubmitPixel(i).r == before[i].r);
    }

    bench::presetsAt(c, store, false);
    c.at(false);
    c.end();
}

void presetsAtListAndDelete(bench::PresetStore &store)
{
    FakeSink sink;
    sink.configure(1, 8, 8);
    static comp::Composer c;
    if (c.build(sink) != ESP_OK) { CHECK(false); return; }
    if (c.at(true) != ESP_OK) { CHECK(false); c.end(); return; }
    if (bench::presetsAt(c, store, true) != ESP_OK) { CHECK(false); c.at(false); c.end(); return; }

    static comp::AtScene s;
    s = comp::AtScene{}; s.count = 1; s.segments[0].id = 1; s.segments[0].stop = 1;
    CHECK(store.save("IntegOne", 8, s) == ESP_OK);
    CHECK(store.save("IntegTwo", 8, s) == ESP_OK);

    CHECK(esp32libfun::at.feedLine("AT+PRESETS?") == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+PRESETDEL=IntegOne") == ESP_OK);
    static comp::AtScene gone, still_there;
    gone = comp::AtScene{};
    CHECK(store.load("IntegOne", 8, gone) == ESP_ERR_NOT_FOUND);
    still_there = comp::AtScene{};
    CHECK(store.load("IntegTwo", 8, still_there) == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+PRESETDEL=Unknown") == ESP_OK); // dispatched, rejected internally

    bench::presetsAt(c, store, false);
    c.at(false);
    c.end();
}

// Runs last: fills every remaining position (whatever the earlier gates
// above already occupied), so nothing after this may save a new name.
void presetStoreCapacityTest(bench::PresetStore &store)
{
    static NameCollector before;
    before = NameCollector{};
    store.list(collectNames, &before);
    const unsigned already = before.count;
    CHECK(already < bench::kPresetSlotCount);

    static comp::AtScene scene;
    char name[8];
    for (unsigned i = already; i < bench::kPresetSlotCount; ++i) {
        scene = comp::AtScene{};
        scene.count = 1;
        scene.segments[0].id = static_cast<uint32_t>(i + 1);
        scene.segments[0].stop = 1;
        scene.segments[0].params.color[0] = {static_cast<uint8_t>(i), 0, 0};
        std::snprintf(name, sizeof(name), "Q%03u", i); // distinct from every earlier gate's own names
        CHECK(store.save(name, std::strlen(name), scene) == ESP_OK);
    }
    static NameCollector full;
    store.list(collectNames, &full);
    CHECK(full.count == bench::kPresetSlotCount);

    // A new, 101st DISTINCT name is rejected; every existing position stays
    // intact (spot-checked below), no data loss.
    scene = comp::AtScene{};
    scene.count = 1; scene.segments[0].id = 999; scene.segments[0].stop = 1;
    CHECK(store.save("Overflow", 8, scene) == ESP_ERR_NO_MEM);
    static NameCollector still_full;
    store.list(collectNames, &still_full);
    CHECK(still_full.count == bench::kPresetSlotCount);

    static comp::AtScene loaded;
    loaded = comp::AtScene{};
    CHECK(store.load("Alpha", 5, loaded) == ESP_OK); // an early gate's own name, still intact

    // Replacement while full: an EXISTING name still succeeds (no new
    // position needed); occupancy stays at exactly 100.
    scene = comp::AtScene{};
    scene.count = 1; scene.segments[0].id = 555; scene.segments[0].stop = 1;
    scene.segments[0].params.color[0] = {201, 0, 0};
    CHECK(store.save("Alpha", 5, scene) == ESP_OK);
    loaded = comp::AtScene{};
    CHECK(store.load("Alpha", 5, loaded) == ESP_OK && loaded.segments[0].id == 555);
    static NameCollector after_replace;
    store.list(collectNames, &after_replace);
    CHECK(after_replace.count == bench::kPresetSlotCount);
}

void presetTopologyCatalogTests()
{
    mock::resetPresets();
    auto *writer = new (std::nothrow) bench::PresetStore;
    CHECK(writer != nullptr);
    if (writer == nullptr) { return; }
    static comp::AtScene scene;
    scene = comp::AtScene{};
    scene.count = 1; scene.segments[0].id = 1; scene.segments[0].stop = 1;
    CHECK(writer->begin(bench::defaultTopology(), bench::MapConfig{}) == ESP_OK);
    CHECK(writer->save("Q000", 4, scene) == ESP_OK);
    delete writer;

    bench::Topology two = bench::defaultTopology();
    two.count = 2; two.outputs[1] = bench::OutputSpec{17, 20, 0};
    writer = new (std::nothrow) bench::PresetStore;
    CHECK(writer != nullptr);
    if (writer == nullptr) { return; }
    CHECK(writer->begin(two, bench::MapConfig{}) == ESP_OK);
    scene.segments[0].id = 2;
    CHECK(writer->save("Q000", 4, scene) == ESP_OK); // newer incompatible B must beat older compatible A
    char name[8];
    for (unsigned i = 1; i < 100; ++i) {
        std::snprintf(name, sizeof(name), "Q%03u", i);
        CHECK(writer->save(name, 4, scene) == ESP_OK);
    }
    delete writer;
    auto *reader = new (std::nothrow) bench::PresetStore;
    CHECK(reader != nullptr);
    if (reader == nullptr) { return; }
    CHECK(reader->begin(bench::defaultTopology(), bench::MapConfig{}) == ESP_OK);
    unsigned occupied = 0;
    reader->list([](void *ctx, const bench::PresetInfo &) { ++*static_cast<unsigned *>(ctx); }, &occupied);
    CHECK(occupied == 100);
    scene.segments[0].id = 999;
    CHECK(reader->load("Q000", 4, scene) == ESP_ERR_NOT_SUPPORTED);
    CHECK(scene.segments[0].id == 999);
    const unsigned writes = mock::g_preset_writes;
    CHECK(reader->save("NewName", 7, scene) == ESP_ERR_NO_MEM);
    CHECK(mock::g_preset_writes == writes);
    delete reader;
    reader = new (std::nothrow) bench::PresetStore;
    CHECK(reader != nullptr);
    if (reader == nullptr) { return; }
    CHECK(reader->begin(two, bench::MapConfig{}) == ESP_OK);
    CHECK(reader->load("Q000", 4, scene) == ESP_OK && scene.segments[0].id == 2);
    delete reader;
}

} // namespace

int run_preset_tests()
{
    checks = 0; failures = 0;
    presetCodecTests();

    static bench::PresetStore store; // shared by every gate below except capacity's own fill loop
    if (store.begin(bench::defaultTopology(), bench::MapConfig{}) != ESP_OK) { CHECK(false); }
    presetStoreTwoNamesReplaceDeleteTest(store);
    presetStoreCorruptRecoveryTest(store);
    presetStoreFailureRetryTest(store);
    presetsAtExcludesStagedEditsOnSave(store);
    presetsAtLoadAppliesAndPersists(store);
    presetsAtLoadRejectsMissingOrCorrupt(store);
    presetsAtListAndDelete(store);
    presetStoreCapacityTest(store); // must run last -- see its own comment
    presetTopologyCatalogTests(); // independent store instances, all old checks already completed

    std::printf("PRESET checks=%d failures=%d\n", checks, failures);
    return failures;
}
int preset_check_count() { return checks; }
