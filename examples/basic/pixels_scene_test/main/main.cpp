#include "esp32libfun_at.hpp"
#include "esp32libfun_serial.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_mock.hpp"

extern int run_composer_tests();
extern int composer_check_count();
extern int run_composer_at_tests();
extern int composer_at_check_count();
extern int run_scene_tests();
extern int scene_check_count();
extern int run_preset_tests();
extern int preset_check_count();
extern int run_output_tests();
extern int output_check_count();

extern "C" void app_main()
{
    ESP_ERROR_CHECK(esp32libfun::serial.init());
    ESP_ERROR_CHECK(esp32libfun::at.init());
    bench_mock::resetAll(); // once, before any suite -- see nvs_mock.hpp's own doc
    const int core = run_composer_tests();
    const int at = run_composer_at_tests();
    const int scene = run_scene_tests();
    const int preset = run_preset_tests();
    // Last: some of its own gates call bench_mock::resetAll() themselves for
    // a clean persisted-result check (see test_outputs.cpp) -- harmless here
    // since scene/preset results above are already captured as plain ints.
    const int outputs = run_output_tests();
    const int count = composer_check_count() + composer_at_check_count() + scene_check_count() +
        preset_check_count() + output_check_count();
    for (;;) {
        ESP_LOGI("SCENE_TEST", "core=%d AT=%d scene=%d preset=%d outputs=%d checks=%d RESULT=%s", core, at, scene,
                 preset, outputs, count,
                 core + at + scene + preset + outputs == 0 ? "ALL_PASS" : "FAIL");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
