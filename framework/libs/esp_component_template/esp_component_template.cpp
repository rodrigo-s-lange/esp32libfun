#include "esp_component_template.hpp"

#include <string.h>

// TODO rename: replace file, namespace, class, callback, and object names to
// match the final library before copying this template into production code.
namespace esp_component_template {

namespace {

bool copyName(char *dst, size_t dst_len, const char *src)
{
    if (dst == nullptr || dst_len == 0 || src == nullptr) {
        return false;
    }

    const size_t len = strlen(src);
    if (len >= dst_len) {
        return false;
    }

    memcpy(dst, src, len + 1);
    return true;
}

} // namespace

void Template::taskEntry(void *arg)
{
    Template *self = static_cast<Template *>(arg);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        TickType_t delay_ticks = 1;

        if (self != nullptr && self->lock()) {
            if (!self->started_) {
                self->task_handle_ = nullptr;
                self->unlock();
                break;
            }

            self->step();
            delay_ticks = pdMS_TO_TICKS(self->interval_ms_);
            if (delay_ticks == 0) {
                delay_ticks = 1;
            }
            self->unlock();
        }

        vTaskDelayUntil(&last_wake, delay_ticks);
    }

    vTaskDelete(nullptr);
}

esp_err_t Template::ensureMutex(void)
{
    if (mutex_ != nullptr) {
        return ESP_OK;
    }

    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    return (mutex_ != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool Template::lock(void) const
{
    return mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
}

void Template::unlock(void) const
{
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

esp_err_t Template::setName(const char *name)
{
    if (name == nullptr || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    return copyName(name_, sizeof(name_), name) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

void Template::resetState(void)
{
    counter_ = 0;
}

void Template::step(void)
{
    counter_++;

    template_callback_t callback = callback_;
    if (callback != nullptr) {
        callback(*this);
    }
}

esp_err_t Template::init(const char *name, uint32_t interval_ms)
{
    if (interval_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ensureMutex();
    if (err != ESP_OK) {
        return err;
    }

    if (!lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    err = setName(name);
    if (err != ESP_OK) {
        unlock();
        return err;
    }

    configured_ = true;
    started_ = false;
    interval_ms_ = interval_ms;
    resetState();
    unlock();
    return ESP_OK;
}

esp_err_t Template::start(uint32_t interval_ms, UBaseType_t priority, BaseType_t core)
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
        name_,
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

esp_err_t Template::stop(void)
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

esp_err_t Template::end(void)
{
    if (mutex_ != nullptr && task_handle_ == xTaskGetCurrentTaskHandle()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = stop();
    if (err != ESP_OK) {
        return err;
    }

    if (mutex_ != nullptr && !lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    configured_ = false;
    started_ = false;
    interval_ms_ = DEFAULT_INTERVAL_MS;
    name_[0] = '\0';
    callback_ = nullptr;
    resetState();

    if (mutex_ != nullptr) {
        unlock();
    }
    return ESP_OK;
}

bool Template::ready(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return configured_;
    }

    const bool value = configured_;
    unlock();
    return value;
}

bool Template::started(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return started_;
    }

    const bool value = started_;
    unlock();
    return value;
}

esp_err_t Template::loop(void)
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

    step();
    unlock();
    return ESP_OK;
}

esp_err_t Template::intervalMs(uint32_t interval_ms)
{
    if (interval_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mutex_ != nullptr && !lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    interval_ms_ = interval_ms;

    if (mutex_ != nullptr) {
        unlock();
    }
    return ESP_OK;
}

esp_err_t Template::onTick(template_callback_t callback)
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

const char *Template::name(void) const
{
    return name_;
}

uint32_t Template::intervalMs(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return interval_ms_;
    }

    const uint32_t value = interval_ms_;
    unlock();
    return value;
}

uint32_t Template::counter(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return counter_;
    }

    const uint32_t value = counter_;
    unlock();
    return value;
}

// TODO rename: rename or remove the global convenience object for the real lib.
Template templ;

} // namespace esp_component_template
