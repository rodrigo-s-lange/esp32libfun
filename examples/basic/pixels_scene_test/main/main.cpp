#include "esp32libfun_at.hpp"
#include "esp32libfun_serial.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern int run_composer_tests();
extern int composer_check_count();
extern int run_composer_at_tests();
extern int composer_at_check_count();
extern int run_scene_tests();
extern int scene_check_count();

extern "C" void app_main()
{
    ESP_ERROR_CHECK(esp32libfun::serial.init());
    ESP_ERROR_CHECK(esp32libfun::at.init());
    const int core = run_composer_tests();
    const int at = run_composer_at_tests();
    const int scene = run_scene_tests();
    const int count = composer_check_count() + composer_at_check_count() + scene_check_count();
    for (;;) {
        ESP_LOGI("SCENE_TEST", "core=%d AT=%d scene=%d checks=%d RESULT=%s", core, at, scene,
                 count, core + at + scene == 0 ? "ALL_PASS" : "FAIL");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
