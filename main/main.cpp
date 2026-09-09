#include "esp32libfun_at.hpp"
#include "esp32libfun_serial.hpp"
#include "esp_pixels.hpp"
#include "esp_pixels_composer.hpp"
#include "esp_pixels_composer_adapter_esp_pixels.hpp"
#include "esp_log.h"
#include "scene_store.hpp"

namespace composer = esp_pixels_composer;
namespace adapter = esp_pixels_composer_adapter_esp_pixels;

using bench::kPixelPin;
using bench::kPixelCount;

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(esp32libfun::serial.init());
    ESP_ERROR_CHECK(esp32libfun::at.init());

    // These instances outlive app_main(); the adapter owns the transport.
    static adapter::EspPixelsSink sink;
    static composer::Composer comp;
    static bench::SceneStore store;
    static composer::AtScene scene;
    const esp_err_t load_err = store.load(scene);
    if (load_err != ESP_OK && load_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW("PIXELS_AT", "saved scene unavailable (%s); booting empty; NVS was NOT erased",
                 esp_err_to_name(load_err));
    }
    ESP_ERROR_CHECK(sink.init(pixels, kPixelPin, kPixelCount, PixelOrder::GRB, false));
    ESP_ERROR_CHECK(comp.build(sink));
    ESP_ERROR_CHECK(comp.at(true));
    // Restore and synchronize staged/confirmed state before either task starts.
    const esp_err_t restore_err = composer::attachAtPersistence(comp, scene, bench::SceneStore::saveCallback, &store);
    if (restore_err != ESP_OK) {
        ESP_LOGE("PIXELS_AT", "restore rejected (%s); keeping stored data, starting empty",
                 esp_err_to_name(restore_err));
        ESP_ERROR_CHECK(comp.at(false));
        ESP_ERROR_CHECK(comp.end());
        ESP_ERROR_CHECK(comp.build(sink));
        scene.count = 0;
        scene.brightness = 255;
        scene.paused = false;
        ESP_ERROR_CHECK(comp.at(true));
        ESP_ERROR_CHECK(composer::attachAtPersistence(comp, scene, bench::SceneStore::saveCallback, &store));
    }
    ESP_ERROR_CHECK(comp.start());
    ESP_ERROR_CHECK(esp32libfun::at.start());

    ESP_LOGI("PIXELS_AT", "ready: Composer=%s FX=%s Pixels=%s GPIO=%d count=%u GRB commands=%u/%u persistence=scene-v1",
             ESP_PIXELS_COMPOSER_VERSION, ESP_PIXELS_FX_VERSION, ESP_PIXELS_VERSION,
             kPixelPin, static_cast<unsigned>(kPixelCount),
             static_cast<unsigned>(esp32libfun::at.commandCount()),
             static_cast<unsigned>(esp32libfun::At::MAX_COMMANDS));
}
