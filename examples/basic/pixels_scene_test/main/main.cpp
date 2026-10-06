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
extern int run_map_tests();
extern int map_check_count();
extern int run_xy_gradient_tests();
extern int xy_gradient_check_count();
extern int run_stripes_tests();
extern int stripes_check_count();
extern int run_blink_tests();
extern int blink_check_count();
extern int run_breathe_tests();
extern int breathe_check_count();
extern int run_chase_tests();
extern int chase_check_count();
extern int run_meteor_tests();
extern int meteor_check_count();
extern int run_twinkle_tests();
extern int twinkle_check_count();
extern int run_running_lights_tests();
extern int running_lights_check_count();
extern int run_plasma_tests();
extern int plasma_check_count();
extern int run_matrix_rain_tests();
extern int matrix_rain_check_count();
extern int run_ripple_tests();
extern int ripple_check_count();
extern int run_fire_tests();
extern int fire_check_count();
extern int run_labareda_tests();
extern int labareda_check_count();
extern int run_spiral_tests();
extern int spiral_check_count();
extern int run_chase_variants_tests();
extern int chase_variants_check_count();
extern int run_snake_tests();
extern int snake_check_count();

extern "C" void app_main()
{
    ESP_ERROR_CHECK(esp32libfun::serial.init());
    ESP_ERROR_CHECK(esp32libfun::at.init());
    bench_mock::resetAll(); // once, before any suite -- see nvs_mock.hpp's own doc
    const int core = run_composer_tests();
    const int at = run_composer_at_tests();
    const int scene = run_scene_tests();
    const int preset = run_preset_tests();
    // Last two: some of their own gates call bench_mock::resetAll()
    // themselves for a clean persisted-result check (see test_outputs.cpp/
    // test_map.cpp) -- harmless here since scene/preset results above are
    // already captured as plain ints.
    const int outputs = run_output_tests();
    const int map = run_map_tests();
    const int xy = run_xy_gradient_tests();
    const int stripes = run_stripes_tests();
    const int blink = run_blink_tests();
    const int breathe = run_breathe_tests();
    const int chase = run_chase_tests();
    const int meteor = run_meteor_tests();
    const int twinkle = run_twinkle_tests();
    const int running_lights = run_running_lights_tests();
    const int plasma = run_plasma_tests();
    const int matrix_rain = run_matrix_rain_tests();
    const int ripple = run_ripple_tests();
    const int fire = run_fire_tests();
    const int labareda = run_labareda_tests();
    const int spiral = run_spiral_tests();
    const int chase_variants = run_chase_variants_tests();
    const int snake = run_snake_tests();
    const int count = composer_check_count() + composer_at_check_count() + scene_check_count() +
        preset_check_count() + output_check_count() + map_check_count() + xy_gradient_check_count() +
        stripes_check_count() + blink_check_count() + breathe_check_count() + chase_check_count() +
        meteor_check_count() + twinkle_check_count() + running_lights_check_count() +
        plasma_check_count() + matrix_rain_check_count() + ripple_check_count() + fire_check_count() +
        labareda_check_count() + spiral_check_count() + chase_variants_check_count() + snake_check_count();
    const int total_failed = core + at + scene + preset + outputs + map + xy + stripes + blink + breathe +
        chase + meteor + twinkle + running_lights + plasma + matrix_rain + ripple + fire + labareda + spiral +
        chase_variants + snake;
    for (;;) {
        ESP_LOGI("SCENE_TEST",
                 "core=%d AT=%d scene=%d preset=%d outputs=%d map=%d xy=%d stripes=%d blink=%d breathe=%d "
                 "chase=%d meteor=%d twinkle=%d running_lights=%d plasma=%d matrix_rain=%d ripple=%d fire=%d labareda=%d "
                 "spiral=%d chase_variants=%d snake=%d checks=%d RESULT=%s",
                 core, at, scene, preset, outputs, map, xy, stripes, blink, breathe, chase, meteor, twinkle,
                 running_lights, plasma, matrix_rain, ripple, fire, labareda, spiral, chase_variants, snake, count,
                 total_failed == 0 ? "ALL_PASS" : "FAIL");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
