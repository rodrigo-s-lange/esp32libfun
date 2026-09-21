#include "map_at.hpp"
#include "map_store.hpp"
#include "output_store.hpp"
#include "outputs_at.hpp"
#include "nvs_mock.hpp"
#include "esp32libfun_at.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

// Coverage for the application-owned canvas/ledmap journal (MapStore) and
// its AT library (mapAt, "PIXELS_MAP" -- AT+MAP?/AT+MAPSET/AT+MAPCLR/
// AT+MAPSAVE). Same discipline test_outputs.cpp already uses: fault
// injection through the shared nvs_mock (see its own doc for the
// "map_a"/"map_b" keys/handle=4), assert state/storage effects, not only
// dispatch success. MapStore's own record is tiny (kRecordBytes == 27), so
// like OutputStore this file spends a fresh instance per gate freely.

namespace {
namespace mock = bench_mock;
using bench::MapConfig;
using bench::MapStore;
using bench::MapWiring;
using bench::OutputStore;
using bench::Topology;
int checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    std::printf("MAP FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

void mapValidationTests()
{
    const MapConfig progressive{3,4,45,MapWiring::Progressive};
    const MapConfig serpentine{3,4,45,MapWiring::Serpentine};
    int32_t entries[12];
    CHECK(progressive.valid(62) && serpentine.valid(62));
    CHECK(bench::generateLedmap(progressive, entries, 12));
    for (int i = 0; i < 12; ++i) { CHECK(entries[i] == 45 + i); }
    CHECK(bench::generateLedmap(serpentine, entries, 12));
    const int32_t expected[] = {45,46,47,50,49,48,51,52,53,56,55,54};
    for (int i = 0; i < 12; ++i) { CHECK(entries[i] == expected[i]); }
    CHECK(!bench::generateLedmap(serpentine, entries, 11));
    CHECK(!(MapConfig{65535,65535,65535,MapWiring::Progressive}.valid(62)));
    CHECK(!(MapConfig{3,4,51,MapWiring::Progressive}.valid(62)));
    CHECK(!(MapConfig{3,4,0,static_cast<MapWiring>(2)}.valid(62)));
    CHECK(!(MapConfig{0,0,0,MapWiring::Progressive}.valid(62)));
}

void mapStoreJournalTests()
{
    static MapStore store, torn_reboot, failed_init;
    const uint16_t total_pixels = bench::defaultTopology().totalPixels(); // {16,50,GRB} -> 50

    MapConfig probe;
    CHECK(store.load(probe) == ESP_ERR_NOT_FOUND);
    CHECK(mock::g_map_writes == 0 && mock::g_map_commits == 0);

    MapConfig identity{};
    CHECK(store.save(identity) == ESP_OK);
    CHECK(mock::g_map_writes == 1 && mock::g_map_commits == 1 &&
          mock::g_map_slots[0].size != 0 && mock::g_map_slots[1].size == 0);
    CHECK(store.save(identity) == ESP_OK && mock::g_map_writes == 1); // unchanged config: no wear

    MapConfig matrix; // 5x10 == 50 cells, exactly totalPixels() at first=0 -- no waste
    matrix.width = 5; matrix.height = 10; matrix.first = 0; matrix.wiring = MapWiring::Serpentine;
    CHECK(matrix.valid(total_pixels));
    CHECK(store.save(matrix) == ESP_OK && mock::g_map_writes == 2 && mock::g_map_slots[1].size != 0);

    mock::g_map_slots[1].bytes[10] ^= 1; // latest slot damaged: previous complete (identity) slot survives
    MapConfig fallback;
    CHECK(torn_reboot.load(fallback) == ESP_OK && fallback.isIdentity());

    const mock::MapMockSlot previous = mock::g_map_slots[0];
    mock::g_map_fail_write = true;
    CHECK(torn_reboot.save(matrix) == ESP_FAIL);
    CHECK(mock::g_map_slots[0].size == previous.size &&
          std::memcmp(mock::g_map_slots[0].bytes, previous.bytes, previous.size) == 0);
    mock::g_map_fail_write = false; mock::g_map_fail_commit = true;
    CHECK(torn_reboot.save(matrix) == ESP_FAIL);
    mock::g_map_fail_commit = false; mock::g_map_fail_readback = true;
    CHECK(torn_reboot.save(matrix) == ESP_FAIL);
    mock::g_map_fail_readback = false;
    CHECK(torn_reboot.save(matrix) == ESP_OK); // failure never poisons cache/retry

    // Structurally invalid candidate (height == 0, not identity): rejected
    // by encodeMap() itself, before any mutation.
    MapConfig invalid;
    invalid.width = 5; invalid.height = 0; invalid.first = 0;
    const unsigned writes_before = mock::g_map_writes;
    CHECK(torn_reboot.save(invalid) == ESP_ERR_INVALID_ARG);
    CHECK(mock::g_map_writes == writes_before);
    invalid = MapConfig{3,4,0,static_cast<MapWiring>(2)};
    CHECK(torn_reboot.save(invalid) == ESP_ERR_INVALID_ARG);
    CHECK(mock::g_map_writes == writes_before);

    mock::g_scene_init_error = ESP_FAIL; // shared nvs_flash_init() entry point -- see nvs_mock.hpp's own doc
    MapConfig never;
    CHECK(failed_init.load(never) == ESP_FAIL);
    CHECK(failed_init.save(identity) == ESP_FAIL); // never erase/reinitialize on failure
    mock::g_scene_init_error = ESP_OK;
}

// One continuous AT session, mirroring outputsAtIntegrationTests()'s own
// two-phase pattern: a single "device" MapStore reused across saves (load()
// is call-once-per-instance), each phase's own persisted result
// independently re-verified through its own fresh instance.
void mapAtIntegrationTests()
{
    mock::resetAll(); // clean slate from mapStoreJournalTests()'s own leftover mock state
    static MapStore device;
    static OutputStore output_store;
    MapConfig probe;
    CHECK(device.load(probe) == ESP_ERR_NOT_FOUND); // first boot: no saved map yet
    Topology topology;
    CHECK(output_store.load(topology) == ESP_ERR_NOT_FOUND); // first boot: no saved topology either
    // output_store.current() now reflects defaultTopology() (never
    // loaded/saved successfully) -- exactly the real boot fallback.

    static bench::SceneStore scene_store; // durable-scene guard target; never saved to in this file
    static esp_pixels_composer::AtScene unused_scene;
    // load() must be called once before canRestoreOn() will do anything but
    // ESP_ERR_INVALID_STATE (see its own doc) -- an empty mock has nothing
    // saved, so this always returns ESP_ERR_NOT_FOUND, same as the real
    // boot's own first-flash case; canRestoreOn() then sees cached_.size==0
    // and always reports ESP_OK ("compatible": no scene to break).
    CHECK(scene_store.load(bench::defaultTopology(), MapConfig{}, unused_scene) == ESP_ERR_NOT_FOUND);

    // --- Phase A: AT+MAPSET stages a real matrix bounded by the CURRENTLY
    // saved (here: default, 50-pixel) output topology, rejects an
    // out-of-bounds candidate, then AT+MAPSAVE persists it.
    CHECK(bench::mapAt(MapConfig{}, device, scene_store, output_store, true) == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+MAP?") == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+MAP?=1") == ESP_OK); // dispatched, rejected internally (extra arg)

    CHECK(esp32libfun::at.feedLine("AT+MAPSET=5,10,0,SERPENTINE") == ESP_OK); // 5*10==50==totalPixels(): fits exactly
    CHECK(esp32libfun::at.feedLine("AT+MAPSET=5,11,0,SERPENTINE") ==
          ESP_OK); // dispatched, rejected (5*11==55 > 50) -- staged candidate must be unaffected by this

    CHECK(esp32libfun::at.feedLine("AT+MAPSAVE") == ESP_OK);
    CHECK(bench::mapAt(MapConfig{}, device, scene_store, output_store, false) == ESP_OK);

    {
        // If the rejected 5x11 edit had actually taken effect, this would
        // not read back as exactly the 5x10 serpentine matrix.
        static MapStore reboot_a;
        MapConfig persisted;
        CHECK(reboot_a.load(persisted) == ESP_OK);
        CHECK(!persisted.isIdentity() && persisted.width == 5 && persisted.height == 10 &&
              persisted.first == 0 && persisted.wiring == MapWiring::Serpentine);
    }

    // --- Phase B: AT+MAPCLR stages the identity map (no storage/scene
    // touch of its own), then AT+MAPSAVE persists that.
    MapConfig after_phase_a;
    after_phase_a.width = 5; after_phase_a.height = 10; after_phase_a.first = 0;
    after_phase_a.wiring = MapWiring::Serpentine;
    CHECK(bench::mapAt(after_phase_a, device, scene_store, output_store, true) == ESP_OK);

    CHECK(esp32libfun::at.feedLine("AT+MAPCLR") == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+MAPSAVE") == ESP_OK);
    CHECK(bench::mapAt(after_phase_a, device, scene_store, output_store, false) == ESP_OK);

    {
        static MapStore reboot_b;
        MapConfig persisted;
        CHECK(reboot_b.load(persisted) == ESP_OK);
        CHECK(persisted.isIdentity());
    }

    // --- Phase C: a save() failure must preserve the previous durable
    // configuration exactly.
    CHECK(bench::mapAt(MapConfig{}, device, scene_store, output_store, true) == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+MAPSET=2,5,0,PROGRESSIVE") == ESP_OK); // valid on its own: stages a real change
    mock::g_map_fail_write = true;
    CHECK(esp32libfun::at.feedLine("AT+MAPSAVE") == ESP_OK); // dispatched, save() itself fails
    mock::g_map_fail_write = false;
    CHECK(bench::mapAt(MapConfig{}, device, scene_store, output_store, false) == ESP_OK);

    {
        static MapStore reboot_c;
        MapConfig persisted;
        CHECK(reboot_c.load(persisted) == ESP_OK);
        CHECK(persisted.isIdentity()); // still Phase B's own durable result
    }
}
void combinedSaveGuards()
{
    mock::resetAll();
    struct Fixture {
        MapStore map;
        OutputStore output;
        bench::SceneStore scene;
        esp_pixels_composer::AtScene saved;
    };
    std::unique_ptr<Fixture> f(new (std::nothrow) Fixture{});
    if (!f) { CHECK(false); return; }
    Topology topology = bench::defaultTopology();
    MapConfig identity;
    CHECK(f->output.load(topology) == ESP_ERR_NOT_FOUND);
    topology.count = 2; topology.outputs[1] = bench::OutputSpec{17,12,0};
    CHECK(f->output.save(topology) == ESP_OK);
    CHECK(f->map.load(identity) == ESP_ERR_NOT_FOUND);
    CHECK(f->scene.load(topology, identity, f->saved) == ESP_ERR_NOT_FOUND);
    f->saved.count = 1; f->saved.segments[0].id = 1; f->saved.segments[0].stop = 50;
    CHECK(f->scene.save(topology, identity, f->saved) == ESP_OK);
    CHECK(bench::outputsAt(topology, f->output, f->scene, f->map, true) == ESP_OK);
    CHECK(bench::mapAt(identity, f->map, f->scene, f->output, true) == ESP_OK);
    const auto feed = [](const char *line) { CHECK(esp32libfun::at.feedLine(line) == ESP_OK); };
    feed("AT+MAPSET=3,4,50,SERPENTINE"); feed("AT+MAPSAVE");
    CHECK(mock::g_map_writes == 0); // current nonempty scene incompatible
    f->saved.count = 0;
    CHECK(f->scene.save(topology, identity, f->saved) == ESP_OK);
    feed("AT+MAPSAVE");
    CHECK(f->map.current().width == 3 && mock::g_map_writes == 1);
    f->saved.count = 1;
    const unsigned scene_writes = mock::g_scene_writes;
    CHECK(f->scene.save(topology, identity, f->saved) == ESP_ERR_INVALID_STATE);
    CHECK(mock::g_scene_writes == scene_writes);
    const unsigned output_writes = mock::g_output_writes;
    feed("AT+OUTSET=1,17,11,GRB"); feed("AT+OUTSAVE");
    CHECK(mock::g_output_writes == output_writes && f->output.current().outputs[1].count == 12);
    feed("AT+MAPCLR"); feed("AT+OUTSAVE"); // unsaved map edit cannot bypass guard
    CHECK(mock::g_output_writes == output_writes);
    feed("AT+MAPSAVE"); feed("AT+OUTSAVE"); // map-first shrink
    CHECK(f->map.current().isIdentity() && f->output.current().outputs[1].count == 11);
    feed("AT+OUTSET=1,17,12,GRB");
    feed("AT+MAPSET=3,4,50,SERPENTINE"); feed("AT+MAPSAVE");
    CHECK(f->map.current().isIdentity()); // unsaved output growth still too short
    feed("AT+OUTSAVE"); feed("AT+MAPSET=3,4,50,SERPENTINE"); feed("AT+MAPSAVE");
    CHECK(f->map.current().first == 50 && f->map.current().width == 3);
    CHECK(f->output.current().outputs[1].count == 12);
    CHECK(bench::mapAt(identity, f->map, f->scene, f->output, false) == ESP_OK);
    CHECK(bench::outputsAt(topology, f->output, f->scene, f->map, false) == ESP_OK);
    const MapConfig matrix = f->map.current();
    f.reset();
    std::unique_ptr<Fixture> reboot(new (std::nothrow) Fixture{});
    if (!reboot) { CHECK(false); return; }
    MapConfig loaded;
    CHECK(reboot->output.load(topology) == ESP_OK);
    CHECK(reboot->map.load(loaded) == ESP_OK && loaded == matrix);
    CHECK(reboot->scene.load(topology, loaded, reboot->saved) == ESP_ERR_NOT_SUPPORTED);
    reboot->saved.count = 1;
    auto &s = reboot->saved.segments[0];
    s.id = 7; s.is2D = true; s.w = 3; s.h = 4;
    CHECK(reboot->scene.save(topology, loaded, reboot->saved) == ESP_OK);
    reboot.reset();
    std::unique_ptr<Fixture> restored(new (std::nothrow) Fixture{});
    if (!restored) { CHECK(false); return; }
    CHECK(restored->scene.load(topology, loaded, restored->saved) == ESP_OK);
    CHECK(restored->saved.count == 1 && restored->saved.segments[0].is2D &&
          restored->saved.segments[0].w == 3 && restored->saved.segments[0].h == 4);
}
} // namespace

int run_map_tests()
{
    checks = 0; failures = 0;
    mapValidationTests();
    mapStoreJournalTests();
    mapAtIntegrationTests();
    combinedSaveGuards();
    mock::resetAll(); // leave the shared mock clean for whichever suite runs next
    std::printf("MAP checks=%d failures=%d\n", checks, failures);
    return failures;
}
int map_check_count() { return checks; }
