#include "esp32libfun.hpp"
#include "esp_pixels.hpp"
#include "esp_pixels_composer.hpp"
#include "esp_pixels_composer_adapter_esp_pixels.hpp"

namespace composer = esp_pixels_composer;
namespace adapter = esp_pixels_composer_adapter_esp_pixels;

namespace {

// Physical bench: ESP32-WROOM-32, GPIO16, one WS2812 strip of 50 pixels, GRB.
constexpr int kPixelPin = 16;
constexpr uint16_t kPixelCount = 50;

} // namespace

extern "C" void app_main(void)
{
    esp32libfun_init();

    // Static, not stack-local (Task 6C): sink/comp must outlive app_main()'s
    // own return -- the managed runtime now owns the render role via its own
    // task (Composer::start(), Design v2 §9), so app_main() no longer needs
    // to block forever in a loop just to keep them alive by scope. Composer/
    // EspPixelsSink are both non-copyable/non-movable and own heap for as
    // long as they stay built/configured, same convention as every other
    // long-lived instance in this codebase (e.g. the global `pixels`).
    static adapter::EspPixelsSink sink;
    static composer::Composer comp;

    // The adapter is the sole owner of pixels' physical configuration
    // (Design v2 §9): it calls pixels.beginRaw() internally. Neither
    // pixels.at(true) nor esp_pixels_fx::player.at(true) are enabled here --
    // both sidecars would control a different transport/effect object than
    // the one Composer actually drives through this adapter, and would only
    // conflict with it.
    ESP_ERROR_CHECK(sink.init(pixels, kPixelPin, kPixelCount, PixelOrder::GRB, false));
    ESP_ERROR_CHECK(comp.build(sink));

    // AT owns the control role from here on (AT+SEGSET/AT+SEGCOL/AT+SEGPUB
    // over the console) -- app_main() itself never
    // calls addSegment()/applyConfig() again after this point. The initial
    // configuration is deliberately empty (nothing staged or published yet):
    // Design v2 §9 treats that as a routine, valid state, not a failure --
    // the managed task below simply renders nothing until an AT+SEGPUB (or
    // equivalent) sequence publishes the first real configuration, which it
    // then adopts automatically on its own next frame attempt, with no
    // further action needed here.
    ESP_ERROR_CHECK(comp.at(true));

    // Composer owns the render role from here on: start() creates its own
    // managed task (Design v2 §9), which calls the same internal render
    // path app_main() used to call manually in a loop, at its own paced
    // cadence, reading whatever configuration AT has published so far.
    // app_main() has nothing left to do -- it returns, and the managed task
    // (plus the AT console task) keep the board running.
    ESP_ERROR_CHECK(comp.start());
}
