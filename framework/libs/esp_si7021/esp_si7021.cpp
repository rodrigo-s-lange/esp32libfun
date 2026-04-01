#include "esp_si7021.hpp"
#include "esp32libfun_i2c.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esp_si7021 {

namespace {

static const char *TAG = "ESP_SI7021";

// Commands — no-hold master mode for humidity; temperature read from result
// register avoids a second conversion and cuts total acquisition time in half.
constexpr uint8_t kCmdMeasureRH      = 0xF5;
constexpr uint8_t kCmdReadTempFromRH = 0xE0;
constexpr uint8_t kCmdReset          = 0xFE;

// 25 ms covers the worst-case 12-bit humidity conversion (12 ms) with margin.
constexpr uint32_t kMeasureDelayMs = 25;
// Si7021 datasheet: max 15 ms after reset before accepting commands.
constexpr uint32_t kResetDelayMs = 20;

float toHumidity(uint16_t raw)
{
    return ((125.0f * static_cast<float>(raw)) / 65536.0f) - 6.0f;
}

float toTemperature(uint16_t raw)
{
    return ((175.72f * static_cast<float>(raw)) / 65536.0f) - 46.85f;
}

} // namespace

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

esp_err_t Si7021::ensureMutex(void)
{
    if (mutex_ != nullptr) {
        return ESP_OK;
    }

    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    return (mutex_ != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool Si7021::lock(void) const
{
    return mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
}

void Si7021::unlock(void) const
{
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void Si7021::resetState(void)
{
    configured_ = false;
    started_ = false;
    interval_ms_ = DEFAULT_INTERVAL_MS;
    address_ = DEFAULT_ADDRESS;
    port_ = 0;
    temperature_ = 0.0f;
    humidity_ = 0.0f;
    callback_ = nullptr;
}

esp_err_t Si7021::measureRH(float *humidity_pct)
{
    const uint8_t cmd = kCmdMeasureRH;
    esp_err_t err = i2c.write(address_, &cmd, 1, port_);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(kMeasureDelayMs));

    uint8_t buf[2] = {};
    err = i2c.read(address_, buf, sizeof(buf), port_);
    if (err != ESP_OK) {
        return err;
    }

    const uint16_t raw = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
    *humidity_pct = toHumidity(raw);
    return ESP_OK;
}

esp_err_t Si7021::readTempFromLastRH(float *temperature_c)
{
    const uint8_t cmd = kCmdReadTempFromRH;
    uint8_t buf[2] = {};
    const esp_err_t err = i2c.writeRead(address_, &cmd, 1, buf, sizeof(buf), port_);
    if (err != ESP_OK) {
        return err;
    }

    const uint16_t raw = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
    *temperature_c = toTemperature(raw);
    return ESP_OK;
}

void Si7021::step(void)
{
    // read() acquires the lock only for the final value update, so I2C
    // operations run outside the lock and the task stays responsive.
    const esp_err_t err = read();
    si7021_callback_t callback = nullptr;

    if (err == ESP_OK && lock()) {
        callback = callback_;
        unlock();
    }

    if (err == ESP_OK && callback != nullptr) {
        callback(*this);
    }
}

void Si7021::taskEntry(void *arg)
{
    Si7021 *self = static_cast<Si7021 *>(arg);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        bool should_stop = false;
        TickType_t delay_ticks = 1;

        if (self->lock()) {
            should_stop = !self->started_;
            if (should_stop) {
                self->task_handle_ = nullptr;
            } else {
                delay_ticks = pdMS_TO_TICKS(self->interval_ms_);
                if (delay_ticks == 0) {
                    delay_ticks = 1;
                }
            }
            self->unlock();
        }

        if (should_stop) {
            break;
        }

        self->step();

        vTaskDelayUntil(&last_wake, delay_ticks);
    }

    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t Si7021::init(uint16_t address, int port)
{
    if (configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!i2c.ready(port)) {
        ESP_LOGE(TAG, "bus %d not ready — call i2c.begin() first", port);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2c.probe(address, port);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "probe failed at 0x%02X on bus %d: %s", address, port, esp_err_to_name(err));
        return err;
    }

    err = i2c.add(address, port);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c.add(0x%02X) failed: %s", address, esp_err_to_name(err));
        return err;
    }

    err = ensureMutex();
    if (err != ESP_OK) {
        i2c.remove(address, port);
        return err;
    }

    if (!lock()) {
        i2c.remove(address, port);
        return ESP_ERR_INVALID_STATE;
    }

    address_ = address;
    port_ = port;
    unlock();

    // Soft reset must succeed before the instance is marked as configured.
    const uint8_t cmd = kCmdReset;
    err = i2c.write(address_, &cmd, 1, port_);
    if (err != ESP_OK) {
        i2c.remove(address, port);
        if (lock()) {
            resetState();
            unlock();
        }
        ESP_LOGE(TAG, "reset failed at 0x%02X: %s", address, esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(kResetDelayMs));

    if (!lock()) {
        i2c.remove(address, port);
        resetState();
        return ESP_ERR_INVALID_STATE;
    }
    configured_ = true;
    unlock();

    ESP_LOGI(TAG, "initialized at 0x%02X on port %d", address, port);
    return ESP_OK;
}

esp_err_t Si7021::start(uint32_t interval_ms, UBaseType_t priority, BaseType_t core)
{
    if (interval_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (started_) {
        unlock();
        return ESP_OK;
    }

    interval_ms_ = interval_ms;
    task_handle_ = xTaskCreateStaticPinnedToCore(
        taskEntry,
        "si7021",
        DEFAULT_TASK_STACK_WORDS,
        this,
        priority,
        task_stack_,
        &task_storage_,
        core);

    if (task_handle_ == nullptr) {
        unlock();
        return ESP_ERR_NO_MEM;
    }

    started_ = true;
    unlock();
    return ESP_OK;
}

esp_err_t Si7021::stop(void)
{
    if (mutex_ == nullptr) {
        return ESP_OK;
    }

    if (!lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!started_) {
        unlock();
        return ESP_OK;
    }

    TaskHandle_t handle = task_handle_;
    const bool self_delete = (handle == xTaskGetCurrentTaskHandle());
    started_ = false;
    task_handle_ = nullptr;
    unlock();

    if (handle != nullptr) {
        if (self_delete) {
            vTaskDelete(nullptr);
            return ESP_OK;
        }

        vTaskDelete(handle);
    }

    return ESP_OK;
}

esp_err_t Si7021::end(void)
{
    if (mutex_ != nullptr && task_handle_ == xTaskGetCurrentTaskHandle()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = stop();
    if (err != ESP_OK) {
        return err;
    }

    if (configured_) {
        esp_err_t remove_err = i2c.remove(address_, port_);
        if (remove_err != ESP_OK && remove_err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "i2c.remove(0x%02X) failed: %s", address_, esp_err_to_name(remove_err));
        }
    }

    if (mutex_ != nullptr && !lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    resetState();

    if (mutex_ != nullptr) {
        unlock();
    }

    return ESP_OK;
}

esp_err_t Si7021::read(void)
{
    if (!configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    float humidity = 0.0f;
    float temperature = 0.0f;

    esp_err_t err = measureRH(&humidity);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "humidity measurement failed: %s", esp_err_to_name(err));
        return err;
    }

    err = readTempFromLastRH(&temperature);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "temperature read failed: %s", esp_err_to_name(err));
        return err;
    }

    if (lock()) {
        humidity_ = humidity;
        temperature_ = temperature;
        unlock();
    }

    return ESP_OK;
}

esp_err_t Si7021::reset(void)
{
    if (!configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t cmd = kCmdReset;
    const esp_err_t err = i2c.write(address_, &cmd, 1, port_);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(kResetDelayMs));
    }
    return err;
}

esp_err_t Si7021::onRead(si7021_callback_t callback)
{
    if (mutex_ != nullptr && !lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    callback_ = callback;

    if (mutex_ != nullptr) {
        unlock();
    }

    return ESP_OK;
}

bool Si7021::ready(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return configured_;
    }

    const bool value = configured_;
    unlock();
    return value;
}

bool Si7021::started(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return started_;
    }

    const bool value = started_;
    unlock();
    return value;
}

esp_err_t Si7021::loop(void)
{
    if (!configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (started_) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }

    unlock();
    step();
    return ESP_OK;
}

float Si7021::temperature(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return temperature_;
    }

    const float value = temperature_;
    unlock();
    return value;
}

float Si7021::humidity(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return humidity_;
    }

    const float value = humidity_;
    unlock();
    return value;
}

uint16_t Si7021::address(void) const
{
    return address_;
}

Si7021 si7021;

} // namespace esp_si7021
