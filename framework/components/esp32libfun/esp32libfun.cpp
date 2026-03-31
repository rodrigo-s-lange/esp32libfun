#include "esp32libfun.hpp"

const char *esp32libfun_version(void)
{
    return ESP32LIBFUN_VERSION;
}

const char *esp32libfun_major(void)
{
    return "0";
}

const char *esp32libfun_minor(void)
{
    return "0";
}

const char *esp32libfun_patch(void)
{
    return "0";
}

void esp32libfun_init(void)
{
#if CONFIG_ESP32LIBFUN_SERIAL
    ESP_ERROR_CHECK(serial.init());
    serial.println(O "esp32libfun " C "%s", esp32libfun_version());
#endif

#if CONFIG_ESP32LIBFUN_RUNTIME
    esp32libfun_runtime_init();
#endif

#if CONFIG_ESP32LIBFUN_AT
    ESP_ERROR_CHECK(at.init());
    ESP_ERROR_CHECK(at.start());
#endif

}
