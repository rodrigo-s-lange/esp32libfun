#include "esp32libfun.hpp"
#include "esp_component_template.hpp"

// TODO rename: replace Template, templ, and the log tag with your final names.
static void onTemplateTick(Template &instance)
{
    serial.println(O "[esp_component_template]" C " managed tick %lu from %s",
                   static_cast<unsigned long>(instance.counter()),
                   instance.name());
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(templ.init("managed-template", 1000));
    ESP_ERROR_CHECK(templ.onTick(onTemplateTick));
    ESP_ERROR_CHECK(templ.start());

    while (true) {
        delay.s(1);
    }
}
