#include "esp32libfun_gpio.hpp"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "ESP32LIBFUN_GPIO";
static uint64_t s_gpio_output_shadow = 0;
static uint64_t s_gpio_output_shadow_mask = 0;

namespace esp32libfun {

bool Gpio::isValidPin(int pin)
{
    return GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(pin));
}

bool Gpio::usesOutputShadow(int direction)
{
    switch (direction) {
        case OUTPUT:
        case INPUT_OUTPUT:
        case INPUT_OUTPUT_OPENDRAIN:
        case OUTPUT_OPENDRAIN:
            return true;
        default:
            return false;
    }
}

esp_err_t Gpio::applyConfig(int pin, int direction)
{
    if (!isValidPin(pin)) {
        ESP_LOGE(TAG, "invalid GPIO pin: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.intr_type = GPIO_INTR_DISABLE;

    switch (direction) {
        case INPUT:
            cfg.mode = GPIO_MODE_INPUT;
            cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case INPUT_PULLUP:
            cfg.mode = GPIO_MODE_INPUT;
            cfg.pull_up_en = GPIO_PULLUP_ENABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case INPUT_PULLDOWN:
            cfg.mode = GPIO_MODE_INPUT;
            cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
            break;
        case OUTPUT:
            cfg.mode = GPIO_MODE_OUTPUT;
            cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case INPUT_OUTPUT:
            cfg.mode = GPIO_MODE_INPUT_OUTPUT;
            cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case INPUT_OUTPUT_OPENDRAIN:
            cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
            cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case OUTPUT_OPENDRAIN:
            cfg.mode = GPIO_MODE_OUTPUT_OD;
            cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        default:
            ESP_LOGE(TAG, "invalid GPIO direction: %d", direction);
            return ESP_ERR_INVALID_ARG;
    }

    return gpio_config(&cfg);
}

esp_err_t Gpio::cfg(int pin, int direction) const
{
    if (!isValidPin(pin)) {
        ESP_LOGE(TAG, "invalid GPIO pin: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_num_t gpio_pin = static_cast<gpio_num_t>(pin);
    const uint64_t pin_mask = (1ULL << pin);
    esp_err_t err = gpio_reset_pin(gpio_pin);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_reset_pin(%d) failed: %s", pin, esp_err_to_name(err));
        return err;
    }

    err = applyConfig(pin, direction);
    if (err == ESP_OK) {
        if (usesOutputShadow(direction)) {
            s_gpio_output_shadow &= ~pin_mask;
            s_gpio_output_shadow_mask |= pin_mask;
        } else {
            s_gpio_output_shadow_mask &= ~pin_mask;
        }
        ESP_LOGD(TAG, "GPIO %d configured (dir=%d)", pin, direction);
    } else {
        ESP_LOGE(TAG, "GPIO %d configuration failed: %s", pin, esp_err_to_name(err));
    }
    return err;
}

esp_err_t Gpio::write(int pin, bool level) const
{
    if (!isValidPin(pin)) {
        ESP_LOGE(TAG, "invalid GPIO pin: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = gpio_set_level(static_cast<gpio_num_t>(pin), level ? 1 : 0);
    if (err == ESP_OK) {
        const uint64_t pin_mask = (1ULL << pin);
        s_gpio_output_shadow_mask |= pin_mask;
        if (level) {
            s_gpio_output_shadow |= pin_mask;
        } else {
            s_gpio_output_shadow &= ~pin_mask;
        }
    }
    return err;
}

esp_err_t Gpio::high(int pin) const
{
    return write(pin, HIGH);
}

esp_err_t Gpio::low(int pin) const
{
    return write(pin, LOW);
}

bool Gpio::read(int pin) const
{
    if (!isValidPin(pin)) {
        ESP_LOGE(TAG, "invalid GPIO pin: %d", pin);
        return false;
    }

    return gpio_get_level(static_cast<gpio_num_t>(pin)) != 0;
}

bool Gpio::state(int pin) const
{
    if (!isValidPin(pin)) {
        ESP_LOGE(TAG, "invalid GPIO pin: %d", pin);
        return false;
    }

    const uint64_t pin_mask = (1ULL << pin);
    if ((s_gpio_output_shadow_mask & pin_mask) != 0) {
        return (s_gpio_output_shadow & pin_mask) != 0;
    }

    return read(pin);
}

esp_err_t Gpio::toggle(int pin) const
{
    if (!isValidPin(pin)) {
        ESP_LOGE(TAG, "invalid GPIO pin: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }

    return write(pin, !state(pin));
}

Gpio gpio;

} // namespace esp32libfun
