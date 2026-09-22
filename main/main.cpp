#include "esp32libfun_at.hpp"
#include "esp32libfun_serial.hpp"
#include "esp_pixels.hpp"
#include "esp_pixels_composer.hpp"
#include "esp_pixels_composer_adapter_esp_pixels.hpp"
#include "esp_log.h"
#include "map_at.hpp"
#include "map_config.hpp"
#include "map_store.hpp"
#include "output_config.hpp"
#include "output_store.hpp"
#include "outputs_at.hpp"
#include "preset_store.hpp"
#include "presets_at.hpp"
#include "scene_store.hpp"

namespace composer = esp_pixels_composer;
namespace adapter = esp_pixels_composer_adapter_esp_pixels;

// This application's own kMaxOutputs (output_config.hpp) must never drift
// from the adapter's own delivered limit -- see output_config.hpp's own
// comment on why the two are not the same #include's constant.
static_assert(bench::kMaxOutputs == adapter::EspPixelsSink::kMaxOutputs,
              "bench::kMaxOutputs must match EspPixelsSink::kMaxOutputs");

// A second real Pixels instance, for a second output slot -- entirely
// unused (never beginRaw()'d) unless the saved/active topology actually
// configures a second output; see below.
static Pixels pixels2;
static constexpr PixelOrder kOrders[] = {
    PixelOrder::GRB, PixelOrder::RGB, PixelOrder::RBG,
    PixelOrder::GBR, PixelOrder::BRG, PixelOrder::BGR
};

// Generates and installs the real ledmap table for `map` on `comp` (a
// freshly `build()`-ed instance, per `setLedmap()`'s own one-shot-per-cycle
// precondition) -- a no-op when `map` is the identity bypass, leaving
// `comp`'s own default identity canvas from `build()` untouched. The table
// itself is static (never returns/escapes this translation unit): its
// bytes are copied into Composer's own owned buffer by `setLedmap()`
// before this function returns, so reusing one buffer across both the
// primary and the restore-retry `build()` cycle in `app_main()` is safe.
// Sized for the worst case this application ever validates
// (`MapConfig::valid()`): `first + width*height <= totalPixels() <=
// kMaxOutputs * kMaxPixelsPerOutput`.
static void installLedmap(composer::Composer &comp, const bench::MapConfig &map)
{
    if (map.isIdentity()) {
        return;
    }
    constexpr size_t kMaxCanvasCells = static_cast<size_t>(bench::kMaxOutputs) * bench::kMaxPixelsPerOutput;
    static int32_t ledmap_table[kMaxCanvasCells];
    const size_t cells = static_cast<size_t>(map.width) * map.height;
    ESP_ERROR_CHECK(bench::generateLedmap(map, ledmap_table, cells) ? ESP_OK : ESP_ERR_INVALID_ARG);
    ESP_ERROR_CHECK(comp.setLedmap(map.width, map.height, ledmap_table, static_cast<uint32_t>(cells)));
}

extern "C" void app_main(void)
{
    // These instances outlive app_main(); the adapter owns every transport.
    static adapter::EspPixelsSink sink;
    static composer::Composer comp;
    static bench::SceneStore scene_store;
    static bench::OutputStore output_store;
    static bench::MapStore map_store;
    static composer::AtScene scene;

    // Output topology: the saved one if valid, else this application's own
    // fixed default (one GPIO16/50/GRB output, unchanged since before this
    // delivery). Reading this NEVER touches the adapter/Composer -- it only
    // decides how many outputs to configure below.
    bench::Topology topology = bench::defaultTopology();
    const esp_err_t topology_load_err = output_store.load(topology);
    if (topology_load_err != ESP_OK && topology_load_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW("PIXELS_AT", "saved output config unavailable (%s); booting with the default output",
                 esp_err_to_name(topology_load_err));
        topology = bench::defaultTopology();
    }

    // Canvas/ledmap: the saved one if structurally valid, else this
    // application's own fixed default (the identity bypass -- unchanged
    // since before this delivery). Reading this NEVER touches Composer --
    // it only decides what to pass to setLedmap() below, once built.
    bench::MapConfig map{};
    const esp_err_t map_load_err = map_store.load(map);
    if (map_load_err != ESP_OK && map_load_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW("PIXELS_AT", "saved canvas/ledmap unavailable (%s); booting with the identity canvas",
                 esp_err_to_name(map_load_err));
        map = bench::MapConfig{};
    }
    if (!map.valid(topology.totalPixels())) {
        ESP_LOGW("PIXELS_AT", "saved canvas/ledmap does not fit the active output topology; "
                 "booting with the identity canvas");
        map = bench::MapConfig{};
    }

    // Configure every output slot the topology calls for, slot 0 through
    // init(), any further slot through addOutput() -- preserving init()'s
    // own single-output signature/behavior when topology.count == 1.
    ESP_ERROR_CHECK(sink.init(pixels, topology.outputs[0].pin, topology.outputs[0].count,
                              kOrders[topology.outputs[0].order], false));
    for (uint8_t slot = 1; slot < topology.count; ++slot) {
        // Only slot 1 has a second real Pixels instance to bind to in this
        // cut (kMaxOutputs == 2); a saved topology can never exceed that.
        ESP_ERROR_CHECK(sink.addOutput(pixels2, topology.outputs[slot].pin, topology.outputs[slot].count,
                                       kOrders[topology.outputs[slot].order], false));
    }

    // Establish a known physical frame on every configured output before
    // scanning NVS. No render task exists yet, so this startup frame has
    // exclusive access to the sink. One shared zeroed buffer, reused for
    // every slot's own submit() (all-black needs no per-slot content);
    // sized well above any real strip this bench targets.
    constexpr uint16_t kMaxBlackoutPixels = bench::kMaxPixelsPerOutput;
    static const composer::CRGB black[kMaxBlackoutPixels] = {};
    ESP_ERROR_CHECK(sink.beginFrame());
    for (uint8_t i = 0; i < topology.count; ++i) {
        ESP_ERROR_CHECK(topology.outputs[i].count <= kMaxBlackoutPixels ? ESP_OK : ESP_ERR_INVALID_SIZE);
        ESP_ERROR_CHECK(sink.submit(i, black, topology.outputs[i].count));
    }
    ESP_ERROR_CHECK(sink.commit());
    ESP_ERROR_CHECK(sink.wait());
    ESP_LOGI("PIXELS_AT", "startup blackout sent: outputs=%u total=%u", static_cast<unsigned>(topology.count),
             static_cast<unsigned>(topology.totalPixels()));

    ESP_ERROR_CHECK(esp32libfun::serial.init());
    ESP_ERROR_CHECK(esp32libfun::at.init());
    scene_store.bindTopology(topology);
    scene_store.bindMap(map);
    const esp_err_t load_err = scene_store.load(topology, map, scene);
    if (load_err != ESP_OK && load_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW("PIXELS_AT", "saved scene unavailable (%s); booting empty; NVS was NOT erased",
                 esp_err_to_name(load_err));
    }
    ESP_ERROR_CHECK(comp.build(sink));
    installLedmap(comp, map);
    ESP_ERROR_CHECK(comp.at(true));
    // Restore and synchronize staged/confirmed state before either task starts.
    const esp_err_t restore_err =
        composer::attachAtPersistence(comp, scene, bench::SceneStore::saveCallback, &scene_store);
    if (restore_err != ESP_OK) {
        ESP_LOGE("PIXELS_AT", "restore rejected (%s); keeping stored data, starting empty",
                 esp_err_to_name(restore_err));
        ESP_ERROR_CHECK(comp.at(false));
        ESP_ERROR_CHECK(comp.end());
        ESP_ERROR_CHECK(comp.build(sink));
        installLedmap(comp, map);
        scene.count = 0;
        scene.brightness = 255;
        scene.paused = false;
        ESP_ERROR_CHECK(comp.at(true));
        ESP_ERROR_CHECK(
            composer::attachAtPersistence(comp, scene, bench::SceneStore::saveCallback, &scene_store));
    }

    // Named presets: entirely separate NVS partition/journal from the
    // current-scene store above (see preset_store.hpp) -- a preset-store
    // failure never affects the current scene already restored above, and
    // is never fatal to boot. Scanned against the SAME active topology/map, so
    // Catalog every valid preset; compatibility is checked only on load.
    // Incompatible entries still occupy their original slots.
    static bench::PresetStore presets;
    const esp_err_t presets_err = presets.begin(topology, map);
    if (presets_err != ESP_OK) {
        ESP_LOGE("PIXELS_AT", "preset store unavailable (%s); AT+PRESET* will report errors",
                 esp_err_to_name(presets_err));
    } else {
        ESP_ERROR_CHECK(bench::presetsAt(comp, presets, true));
    }

    // Output configuration AT library: stages/saves topology changes for
    // the NEXT reboot only -- never touches the adapter/Composer already
    // built above.
    ESP_ERROR_CHECK(bench::outputsAt(topology, output_store, scene_store, map_store, true));

    // Canvas/ledmap configuration AT library: stages/saves matrix changes
    // for the NEXT reboot only -- never touches the Composer's own
    // already-installed setLedmap() canvas above.
    ESP_ERROR_CHECK(bench::mapAt(map, map_store, scene_store, output_store, true));

    ESP_ERROR_CHECK(comp.start());
    ESP_ERROR_CHECK(esp32libfun::at.start());

    ESP_LOGI("PIXELS_AT",
             "ready: Composer=%s FX=%s Pixels=%s outputs=%u total=%u canvas=%ux%u commands=%u/%u "
             "persistence=scene-v4",
             ESP_PIXELS_COMPOSER_VERSION, ESP_PIXELS_FX_VERSION, ESP_PIXELS_VERSION,
             static_cast<unsigned>(topology.count), static_cast<unsigned>(topology.totalPixels()),
             static_cast<unsigned>(comp.canvasWidth()), static_cast<unsigned>(comp.canvasHeight()),
             static_cast<unsigned>(esp32libfun::at.commandCount()),
             static_cast<unsigned>(esp32libfun::At::MAX_COMMANDS));
}
