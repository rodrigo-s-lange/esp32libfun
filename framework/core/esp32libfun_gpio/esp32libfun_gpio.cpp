#include "esp32libfun_gpio.hpp"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

namespace {

static const char *TAG = "ESP32LIBFUN_GPIO";
static constexpr size_t GPIO_IRQ_QUEUE_LEN = 32;
static constexpr uint32_t GPIO_IRQ_TASK_STACK_WORDS = 2048;

uint64_t s_gpio_output_shadow = 0;
uint64_t s_gpio_output_shadow_mask = 0;

StaticSemaphore_t s_gpio_mutex_storage = {};
SemaphoreHandle_t s_gpio_mutex = nullptr;
portMUX_TYPE s_gpio_sync_lock = portMUX_INITIALIZER_UNLOCKED;

struct GpioIrqSlot {
    esp32libfun::gpio_callback_t callback = nullptr;
    void *user_ctx = nullptr;
    bool armed = false;
};

struct GpioIrqEvent {
    int pin = -1;
    bool level = false;
};

StaticQueue_t s_gpio_irq_queue_storage = {};
uint8_t s_gpio_irq_queue_buffer[GPIO_IRQ_QUEUE_LEN * sizeof(GpioIrqEvent)] = {};
QueueHandle_t s_gpio_irq_queue = nullptr;
StaticTask_t s_gpio_irq_task_storage = {};
StackType_t s_gpio_irq_task_stack[GPIO_IRQ_TASK_STACK_WORDS] = {};
TaskHandle_t s_gpio_irq_task = nullptr;
bool s_gpio_irq_service_ready = false;
GpioIrqSlot s_gpio_irq_slots[SOC_GPIO_PIN_COUNT] = {};

class LockGuard {
public:
    explicit LockGuard(SemaphoreHandle_t mutex)
        : mutex_(mutex), locked_((mutex != nullptr) && (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE))
    {
    }

    ~LockGuard(void)
    {
        if (locked_) {
            xSemaphoreGive(mutex_);
        }
    }

    [[nodiscard]] bool locked(void) const
    {
        return locked_;
    }

private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
};

gpio_int_type_t toIdfIntrType(int type)
{
    switch (type) {
        case esp32libfun::GPIO_IRQ_DISABLE:
            return GPIO_INTR_DISABLE;
        case esp32libfun::GPIO_IRQ_RISING:
            return GPIO_INTR_POSEDGE;
        case esp32libfun::GPIO_IRQ_FALLING:
            return GPIO_INTR_NEGEDGE;
        case esp32libfun::GPIO_IRQ_CHANGE:
            return GPIO_INTR_ANYEDGE;
        case esp32libfun::GPIO_IRQ_LOW_LEVEL:
            return GPIO_INTR_LOW_LEVEL;
        case esp32libfun::GPIO_IRQ_HIGH_LEVEL:
            return GPIO_INTR_HIGH_LEVEL;
        default:
            return GPIO_INTR_DISABLE;
    }
}

void IRAM_ATTR gpioIrqHandler(void *arg)
{
    const int pin = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    if (pin < 0 || pin >= SOC_GPIO_PIN_COUNT || s_gpio_irq_queue == nullptr) {
        return;
    }

    GpioIrqEvent event = {};
    event.pin = pin;
    event.level = gpio_get_level(static_cast<gpio_num_t>(pin)) != 0;

    BaseType_t higher_priority_woken = pdFALSE;
    xQueueSendFromISR(s_gpio_irq_queue, &event, &higher_priority_woken);
    if (higher_priority_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void gpioIrqTask(void *arg)
{
    (void)arg;

    GpioIrqEvent event = {};
    while (true) {
        if (xQueueReceive(s_gpio_irq_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp32libfun::gpio_callback_t callback = nullptr;
        void *user_ctx = nullptr;

        if (s_gpio_mutex != nullptr && xSemaphoreTake(s_gpio_mutex, portMAX_DELAY) == pdTRUE) {
            if (event.pin >= 0 && event.pin < SOC_GPIO_PIN_COUNT) {
                callback = s_gpio_irq_slots[event.pin].callback;
                user_ctx = s_gpio_irq_slots[event.pin].user_ctx;
            }
            xSemaphoreGive(s_gpio_mutex);
        }

        if (callback != nullptr) {
            callback(event.pin, event.level, user_ctx);
        }
    }
}

} // namespace

namespace esp32libfun {

esp_err_t Gpio::ensureSyncPrimitives(void)
{
    portENTER_CRITICAL(&s_gpio_sync_lock);
    if (s_gpio_mutex == nullptr) {
        s_gpio_mutex = xSemaphoreCreateMutexStatic(&s_gpio_mutex_storage);
    }
    portEXIT_CRITICAL(&s_gpio_sync_lock);

    return (s_gpio_mutex != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t Gpio::ensureInterruptRuntime(void)
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gpio_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_gpio_irq_queue == nullptr) {
        s_gpio_irq_queue = xQueueCreateStatic(
            GPIO_IRQ_QUEUE_LEN,
            sizeof(GpioIrqEvent),
            s_gpio_irq_queue_buffer,
            &s_gpio_irq_queue_storage);
        if (s_gpio_irq_queue == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_gpio_irq_task == nullptr) {
        s_gpio_irq_task = xTaskCreateStatic(
            gpioIrqTask,
            "gpio_irq",
            GPIO_IRQ_TASK_STACK_WORDS,
            nullptr,
            tskIDLE_PRIORITY + 1U,
            s_gpio_irq_task_stack,
            &s_gpio_irq_task_storage);
        if (s_gpio_irq_task == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_gpio_irq_service_ready) {
        err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
            return err;
        }
        s_gpio_irq_service_ready = true;
    }

    return ESP_OK;
}

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

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gpio_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    gpio_num_t gpio_pin = static_cast<gpio_num_t>(pin);
    const uint64_t pin_mask = (1ULL << pin);

    err = gpio_reset_pin(gpio_pin);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_reset_pin(%d) failed: %s", pin, esp_err_to_name(err));
        return err;
    }

    err = applyConfig(pin, direction);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d configuration failed: %s", pin, esp_err_to_name(err));
        return err;
    }

    if (usesOutputShadow(direction)) {
        s_gpio_output_shadow &= ~pin_mask;
        s_gpio_output_shadow_mask |= pin_mask;
    } else {
        s_gpio_output_shadow_mask &= ~pin_mask;
        s_gpio_output_shadow &= ~pin_mask;
    }

    ESP_LOGD(TAG, "GPIO %d configured (dir=%d)", pin, direction);
    return ESP_OK;
}

esp_err_t Gpio::write(int pin, bool level) const
{
    if (!isValidPin(pin)) {
        ESP_LOGE(TAG, "invalid GPIO pin: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gpio_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    err = gpio_set_level(static_cast<gpio_num_t>(pin), level ? 1 : 0);
    if (err != ESP_OK) {
        return err;
    }

    const uint64_t pin_mask = (1ULL << pin);
    s_gpio_output_shadow_mask |= pin_mask;
    if (level) {
        s_gpio_output_shadow |= pin_mask;
    } else {
        s_gpio_output_shadow &= ~pin_mask;
    }

    return ESP_OK;
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

    if (ensureSyncPrimitives() != ESP_OK) {
        return false;
    }

    LockGuard guard(s_gpio_mutex);
    if (!guard.locked()) {
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

    if (ensureSyncPrimitives() != ESP_OK) {
        return false;
    }

    LockGuard guard(s_gpio_mutex);
    if (!guard.locked()) {
        return false;
    }

    const uint64_t pin_mask = (1ULL << pin);
    if ((s_gpio_output_shadow_mask & pin_mask) != 0) {
        return (s_gpio_output_shadow & pin_mask) != 0;
    }

    return gpio_get_level(static_cast<gpio_num_t>(pin)) != 0;
}

esp_err_t Gpio::toggle(int pin) const
{
    if (!isValidPin(pin)) {
        ESP_LOGE(TAG, "invalid GPIO pin: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gpio_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    bool next_level = false;
    const uint64_t pin_mask = (1ULL << pin);
    if ((s_gpio_output_shadow_mask & pin_mask) != 0) {
        next_level = (s_gpio_output_shadow & pin_mask) == 0;
    } else {
        next_level = gpio_get_level(static_cast<gpio_num_t>(pin)) == 0;
    }

    err = gpio_set_level(static_cast<gpio_num_t>(pin), next_level ? 1 : 0);
    if (err != ESP_OK) {
        return err;
    }

    s_gpio_output_shadow_mask |= pin_mask;
    if (next_level) {
        s_gpio_output_shadow |= pin_mask;
    } else {
        s_gpio_output_shadow &= ~pin_mask;
    }

    return ESP_OK;
}

esp_err_t Gpio::irq(int pin, int type, gpio_callback_t callback, void *user_ctx) const
{
    if (!isValidPin(pin)) {
        ESP_LOGE(TAG, "invalid GPIO pin: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }

    if (callback == nullptr || type == GPIO_IRQ_DISABLE) {
        return irqOff(pin);
    }

    gpio_int_type_t intr_type = toIdfIntrType(type);
    if (intr_type == GPIO_INTR_DISABLE) {
        ESP_LOGE(TAG, "invalid GPIO IRQ type: %d", type);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureInterruptRuntime();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gpio_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    err = gpio_input_enable(static_cast<gpio_num_t>(pin));
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_intr_type(static_cast<gpio_num_t>(pin), intr_type);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_isr_handler_remove(static_cast<gpio_num_t>(pin));
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = gpio_isr_handler_add(static_cast<gpio_num_t>(pin), gpioIrqHandler, reinterpret_cast<void *>(static_cast<intptr_t>(pin)));
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_intr_enable(static_cast<gpio_num_t>(pin));
    if (err != ESP_OK) {
        gpio_isr_handler_remove(static_cast<gpio_num_t>(pin));
        return err;
    }

    s_gpio_irq_slots[pin].callback = callback;
    s_gpio_irq_slots[pin].user_ctx = user_ctx;
    s_gpio_irq_slots[pin].armed = true;

    ESP_LOGD(TAG, "GPIO %d IRQ armed (type=%d)", pin, type);
    return ESP_OK;
}

esp_err_t Gpio::irqOff(int pin) const
{
    if (!isValidPin(pin)) {
        ESP_LOGE(TAG, "invalid GPIO pin: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gpio_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    gpio_intr_disable(static_cast<gpio_num_t>(pin));
    gpio_set_intr_type(static_cast<gpio_num_t>(pin), GPIO_INTR_DISABLE);
    if (s_gpio_irq_service_ready) {
        err = gpio_isr_handler_remove(static_cast<gpio_num_t>(pin));
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
    }

    s_gpio_irq_slots[pin] = {};
    return ESP_OK;
}

Gpio gpio;

} // namespace esp32libfun
