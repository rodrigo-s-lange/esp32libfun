#include "output_store.hpp"
#include "outputs_at.hpp"
#include "nvs_mock.hpp"
#include "esp32libfun_at.hpp"
#include <cstdio>
#include <cstring>

// Coverage for the application-owned output-topology journal (OutputStore)
// and its AT library (outputsAt, "PIXELS_OUTPUTS" -- AT+OUT?/AT+OUTSET/
// AT+OUTDEL/AT+OUTSAVE). Same discipline test_scene.cpp/test_presets.cpp
// already use: fault injection through the shared nvs_mock (see its own
// doc for the "outputs_a"/"outputs_b" keys/handle=3), assert state/storage
// effects, not only dispatch success.
//
// OutputStore::load() -- like SceneStore's/PresetStore's own -- can only be
// called once per instance (handle_ != 0 thereafter), mirroring a real
// device's own single boot; save() has no such limit. So every gate here
// that needs a genuinely fresh "reboot" view spends its own instance (the
// record itself is tiny, kRecordBytes <= 31 at kMaxOutputs==2, so this is
// still cheap), while a single continuous "device" instance is reused
// across an entire AT session's own repeated saves, the same way a real
// boot's own single OutputStore would be.

namespace {
namespace mock = bench_mock;
using bench::OutputSpec;
using bench::OutputStore;
using bench::Topology;
static bench::SceneStore scene_journal;
int checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    std::printf("OUTPUTS FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

Topology twoOutputs()
{
    Topology t;
    t.count = 2;
    t.outputs[0] = OutputSpec{16, 50, 0};
    t.outputs[1] = OutputSpec{4, 20, 0};
    return t;
}

void outputStoreJournalTests()
{
    static OutputStore store, torn_reboot, failed_init;
    Topology probe;
    CHECK(store.load(probe) == ESP_ERR_NOT_FOUND);
    CHECK(mock::g_output_writes == 0 && mock::g_output_commits == 0);

    const Topology single = bench::defaultTopology(); // {16,50,GRB}, count=1
    CHECK(store.save(single) == ESP_OK);
    CHECK(mock::g_output_writes == 1 && mock::g_output_commits == 1 &&
          mock::g_output_slots[0].size != 0 && mock::g_output_slots[1].size == 0);
    CHECK(store.save(single) == ESP_OK && mock::g_output_writes == 1); // unchanged config: no wear

    const Topology two = twoOutputs();
    CHECK(store.save(two) == ESP_OK && mock::g_output_writes == 2 && mock::g_output_slots[1].size != 0);
    // A fresh load() correctly seeing this 2-output save is proven at the AT
    // level below (outputsAtIntegrationTests' own Phase A) -- not repeated
    // here, to keep this file's own static instance count down (see the
    // file's own top comment).

    mock::g_output_slots[1].bytes[10] ^= 1; // latest slot damaged: previous complete slot survives
    Topology fallback;
    CHECK(torn_reboot.load(fallback) == ESP_OK && fallback.count == 1 && fallback.outputs[0].pin == 16);

    const mock::OutputMockSlot previous = mock::g_output_slots[0];
    mock::g_output_fail_write = true;
    CHECK(torn_reboot.save(two) == ESP_FAIL);
    CHECK(mock::g_output_slots[0].size == previous.size &&
          std::memcmp(mock::g_output_slots[0].bytes, previous.bytes, previous.size) == 0);
    mock::g_output_fail_write = false; mock::g_output_fail_commit = true;
    CHECK(torn_reboot.save(two) == ESP_FAIL);
    CHECK(std::memcmp(mock::g_output_slots[0].bytes, previous.bytes, previous.size) == 0);
    mock::g_output_fail_commit = false; mock::g_output_fail_readback = true;
    CHECK(torn_reboot.save(two) == ESP_FAIL);
    mock::g_output_fail_readback = false;
    CHECK(torn_reboot.save(two) == ESP_OK); // failure never poisons cache/retry

    // Invalid candidate (duplicate pin): rejected by encodeTopology() itself,
    // before any mutation -- the previously-saved valid record is untouched.
    Topology invalid;
    invalid.count = 2;
    invalid.outputs[0] = OutputSpec{16, 50, 0};
    invalid.outputs[1] = OutputSpec{16, 20, 0}; // duplicate pin
    const unsigned writes_before = mock::g_output_writes;
    CHECK(torn_reboot.save(invalid) == ESP_ERR_INVALID_ARG);
    CHECK(mock::g_output_writes == writes_before); // never even attempted a write

    for (uint8_t order = 0; order < bench::kOutputOrderCount; ++order) {
        Topology ordered = two;
        ordered.outputs[1].order = order;
        CHECK(torn_reboot.save(ordered) == ESP_OK);
        OutputStore reboot;
        Topology restored;
        CHECK(reboot.load(restored) == ESP_OK && restored.outputs[1].order == order);
    }
    invalid = two; invalid.outputs[1].order = bench::kOutputOrderCount;
    const unsigned order_writes = mock::g_output_writes;
    CHECK(torn_reboot.save(invalid) == ESP_ERR_INVALID_ARG && mock::g_output_writes == order_writes);

    // __wrap_nvs_flash_init() is the SAME shared entry point SceneStore uses
    // (see nvs_mock.hpp) -- g_scene_init_error is the only lever for it.
    mock::g_scene_init_error = ESP_FAIL;
    Topology never;
    CHECK(failed_init.load(never) == ESP_FAIL);
    CHECK(failed_init.save(single) == ESP_FAIL); // never erase/reinitialize on failure
    mock::g_scene_init_error = ESP_OK;
}

// One continuous AT session across three phases, reusing a single "device"
// OutputStore the way a real boot's own single instance would (load() once,
// save() repeatedly) -- deliberately NOT bench_mock::resetAll() between
// phases, so each phase's own persisted result is exactly what the PREVIOUS
// phase left behind, same as consecutive AT+OUTSAVE calls across one
// uptime. Each phase still spends its own fresh "reboot" instance for its
// own independent read-back proof (load() is call-once-per-instance).
void outputsAtIntegrationTests()
{
    mock::resetAll(); // clean slate from outputStoreJournalTests()'s own leftover mock state
    static esp_pixels_composer::AtScene confirmed;
    CHECK(scene_journal.load(bench::defaultTopology(), confirmed) == ESP_ERR_NOT_FOUND);
    confirmed.count = 1;
    confirmed.segments[0].id = 1;
    confirmed.segments[0].stop = 50;
    CHECK(scene_journal.save(bench::defaultTopology(), confirmed) == ESP_OK);
    static OutputStore device;
    Topology probe;
    CHECK(device.load(probe) == ESP_ERR_NOT_FOUND); // first boot: no saved configuration yet

    // --- Phase A: AT+OUTSET stages only, validates the WHOLE candidate,
    // and leaves it unchanged on any rejected edit; AT+OUTSAVE persists it.
    CHECK(bench::outputsAt(bench::defaultTopology(), device, scene_journal, true) == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+OUT?") == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+OUT?=1") == ESP_OK); // dispatched, rejected internally (extra arg)

    CHECK(esp32libfun::at.feedLine("AT+OUTSET=1,4,20,GRB") == ESP_OK); // valid: stages a second slot

    // Four different rejected shapes, each dispatched but rejected
    // internally -- the staged candidate must be completely unaffected by
    // every one of them: duplicate pin, out-of-range slot, zero count, and
    // a non-GRB order token.
    CHECK(esp32libfun::at.feedLine("AT+OUTSET=0,4,10,GRB") == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+OUTSET=2,5,10,GRB") == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+OUTSET=0,16,0,GRB") == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+OUTSET=0,16,50,RGBX") == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+OUTSET=1,99,10,GRB") == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+OUTSET=1,6,10,GRB") == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+OUTSET=1,34,10,GRB") == ESP_OK);
    CHECK(esp32libfun::at.feedLine("AT+OUTSET=1,4,2049,GRB") == ESP_OK);

    CHECK(esp32libfun::at.feedLine("AT+OUTSAVE") == ESP_OK);
    CHECK(bench::outputsAt(bench::defaultTopology(), device, scene_journal, false) == ESP_OK);

    {
        // If any of the four rejected edits above had actually taken effect,
        // this would not read back as exactly {16,50,GRB},{4,20,GRB}.
        static OutputStore reboot_a;
        Topology persisted;
        CHECK(reboot_a.load(persisted) == ESP_OK);
        CHECK(persisted.count == 2 && persisted.outputs[0].pin == 16 && persisted.outputs[0].count == 50 &&
              persisted.outputs[1].pin == 4 && persisted.outputs[1].count == 20);
    }

    // --- Phase B: AT+OUTDEL shifts later slots down to stay dense, and
    // rejects removing the last remaining slot.
    CHECK(bench::outputsAt(twoOutputs(), device, scene_journal, true) == ESP_OK); // active now matches Phase A's own result

    CHECK(esp32libfun::at.feedLine("AT+OUTDEL=5") == ESP_OK); // dispatched, rejected (out of range)
    CHECK(esp32libfun::at.feedLine("AT+OUTDEL=0") == ESP_OK); // valid: slot 1 shifts down to slot 0
    CHECK(esp32libfun::at.feedLine("AT+OUTDEL=0") == ESP_OK); // dispatched, rejected (would leave zero slots)

    const unsigned prior_writes = mock::g_output_writes;
    CHECK(esp32libfun::at.feedLine("AT+OUTSAVE") == ESP_OK);
    CHECK(mock::g_output_writes == prior_writes); // nonempty current scene must remain recoverable
    confirmed.count = 0; // explicit clear permits removal of active outputs
    CHECK(scene_journal.save(twoOutputs(), confirmed) == ESP_OK);

    CHECK(esp32libfun::at.feedLine("AT+OUTSAVE") == ESP_OK);
    CHECK(bench::outputsAt(twoOutputs(), device, scene_journal, false) == ESP_OK);

    {
        static OutputStore reboot_b;
        Topology persisted;
        CHECK(reboot_b.load(persisted) == ESP_OK);
        CHECK(persisted.count == 1 && persisted.outputs[0].pin == 4 && persisted.outputs[0].count == 20);
        confirmed.count = 1; confirmed.segments[0].stop = 50;
        const unsigned scene_writes = mock::g_scene_writes;
        CHECK(scene_journal.save(twoOutputs(), confirmed) == ESP_ERR_INVALID_STATE);
        CHECK(mock::g_scene_writes == scene_writes);
        confirmed.count = 0;
    }

    // --- Phase C: a save() failure must preserve the previous durable
    // configuration exactly -- staging a real change, then failing the
    // write, must not leave a torn/partial record behind.
    Topology after_phase_b;
    after_phase_b.count = 1;
    after_phase_b.outputs[0] = OutputSpec{4, 20, 0};
    CHECK(bench::outputsAt(after_phase_b, device, scene_journal, true) == ESP_OK);

    CHECK(esp32libfun::at.feedLine("AT+OUTSET=0,17,10,RGB") == ESP_OK); // valid pin and alternate order
    mock::g_output_fail_write = true;
    CHECK(esp32libfun::at.feedLine("AT+OUTSAVE") == ESP_OK); // dispatched, save() itself fails
    mock::g_output_fail_write = false;
    CHECK(bench::outputsAt(after_phase_b, device, scene_journal, false) == ESP_OK);

    {
        static OutputStore reboot_c;
        Topology persisted;
        CHECK(reboot_c.load(persisted) == ESP_OK);
        // Still Phase B's own durable result -- the failed save left no
        // trace at all.
        CHECK(persisted.count == 1 && persisted.outputs[0].pin == 4 && persisted.outputs[0].count == 20);
    }
}
} // namespace

int run_output_tests()
{
    checks = 0; failures = 0;
    outputStoreJournalTests();
    outputsAtIntegrationTests();
    mock::resetAll(); // leave the shared mock clean for whichever suite runs next
    std::printf("OUTPUTS checks=%d failures=%d\n", checks, failures);
    return failures;
}
int output_check_count() { return checks; }
