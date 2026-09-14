#include "esp32libfun_serial.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern int run_adapter_tests(int pin, int pin2);
extern int adapter_check_count();

extern "C" void app_main()
{
    ESP_ERROR_CHECK(esp32libfun::serial.init());
    // Unconnected bench pins: preserve the strips on GPIO16/17.
    const int failures = run_adapter_tests(4, 5);
    for (;;) {
        ESP_LOGI("ADAPTER_TEST", "checks=%d failures=%d RESULT=%s", adapter_check_count(),
                 failures, failures == 0 ? "ALL_PASS" : "FAIL");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
