#include "esp_button.hpp"

// Inspired by Button2 by Lennart Hennigs.
// Original project: https://github.com/LennartHennigs/Button2

#include "esp_log.h"
#include "esp_timer.h"
#include "esp32libfun_gpio.hpp"

namespace {

static const char *TAG = "ESP_BUTTON";

} // namespace

namespace esp_button {

void Button::taskEntry(void *arg)
{
    Button *self = static_cast<Button *>(arg);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        ButtonEvent events[MAX_PENDING_EVENTS] = {};
        button_callback_t callbacks[MAX_PENDING_EVENTS] = {};
        size_t count = 0;
        TickType_t delay_ticks = 1;

        if (self != nullptr && self->lock()) {
            if (!self->started_) {
                self->task_handle_ = nullptr;
                self->unlock();
                break;
            }

            delay_ticks = pdMS_TO_TICKS(self->poll_ms_);
            if (delay_ticks == 0) {
                delay_ticks = 1;
            }

            count = self->process(events, callbacks, MAX_PENDING_EVENTS);
            self->unlock();
        }

        if (self != nullptr) {
            self->dispatch(events, callbacks, count);
        }

        vTaskDelayUntil(&last_wake, delay_ticks);
    }

    vTaskDelete(nullptr);
}

uint32_t Button::nowMs(void)
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool Button::isValidMode(int mode)
{
    switch (mode) {
        case BUTTON_INPUT:
        case BUTTON_INPUT_PULLUP:
        case BUTTON_INPUT_PULLDOWN:
            return true;
        default:
            return false;
    }
}

int Button::toGpioMode(int mode)
{
    switch (mode) {
        case BUTTON_INPUT_PULLUP:
            return INPUT_PULLUP;
        case BUTTON_INPUT_PULLDOWN:
            return INPUT_PULLDOWN;
        case BUTTON_INPUT:
        default:
            return INPUT;
    }
}

esp_err_t Button::ensureMutex(void)
{
    if (mutex_ != nullptr) {
        return ESP_OK;
    }

    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    return (mutex_ != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool Button::lock(void) const
{
    return mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
}

void Button::unlock(void) const
{
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void Button::resetRuntimeState(void)
{
    raw_pressed_ = false;
    stable_pressed_ = false;
    long_detected_ = false;
    click_count_ = 0;
    long_click_count_ = 0;
    last_change_ms_ = 0;
    press_start_ms_ = 0;
    last_press_duration_ms_ = 0;
    click_deadline_ms_ = 0;
    last_long_report_ms_ = 0;
    last_event_ = BUTTON_EVENT_NONE;
}

bool Button::samplePressed(void) const
{
    const bool level = gpio.read(pin_);
    return active_low_ ? !level : level;
}

void Button::queueEvent(ButtonEvent event,
                        ButtonEvent *events,
                        button_callback_t *callbacks,
                        size_t capacity,
                        size_t *count)
{
    last_event_ = event;

    if (events == nullptr || callbacks == nullptr || count == nullptr || *count >= capacity) {
        return;
    }

    const size_t index = static_cast<size_t>(event);
    events[*count] = event;
    callbacks[*count] = (index < CALLBACK_COUNT) ? callbacks_[index] : nullptr;
    (*count)++;
}

void Button::finalizeClicks(ButtonEvent *events,
                            button_callback_t *callbacks,
                            size_t capacity,
                            size_t *count)
{
    if (click_count_ == 0U) {
        return;
    }

    switch (click_count_) {
        case 1:
            queueEvent(BUTTON_EVENT_CLICK, events, callbacks, capacity, count);
            break;
        case 2:
            queueEvent(BUTTON_EVENT_DOUBLE_CLICK, events, callbacks, capacity, count);
            break;
        default:
            queueEvent(BUTTON_EVENT_TRIPLE_CLICK, events, callbacks, capacity, count);
            break;
    }

    click_count_ = 0U;
    click_deadline_ms_ = 0U;
}

size_t Button::process(ButtonEvent *events, button_callback_t *callbacks, size_t capacity)
{
    if (!configured_) {
        return 0U;
    }

    size_t count = 0;
    const uint32_t now_ms = nowMs();
    const bool pressed = samplePressed();

    if (pressed != raw_pressed_) {
        raw_pressed_ = pressed;
        last_change_ms_ = now_ms;
    }

    if ((now_ms - last_change_ms_) < debounce_ms_) {
        if (click_deadline_ms_ != 0U && !stable_pressed_ && now_ms >= click_deadline_ms_) {
            finalizeClicks(events, callbacks, capacity, &count);
        }
        return count;
    }

    if (stable_pressed_ != raw_pressed_) {
        stable_pressed_ = raw_pressed_;
        queueEvent(BUTTON_EVENT_CHANGED, events, callbacks, capacity, &count);

        if (stable_pressed_) {
            press_start_ms_ = now_ms;
            long_detected_ = false;
            last_long_report_ms_ = now_ms;
            queueEvent(BUTTON_EVENT_PRESSED, events, callbacks, capacity, &count);
        } else {
            last_press_duration_ms_ = now_ms - press_start_ms_;
            queueEvent(BUTTON_EVENT_RELEASED, events, callbacks, capacity, &count);

            if (long_detected_ || last_press_duration_ms_ >= long_click_ms_) {
                long_click_count_++;
                queueEvent(BUTTON_EVENT_LONG_CLICK, events, callbacks, capacity, &count);
                click_count_ = 0U;
                click_deadline_ms_ = 0U;
            } else {
                queueEvent(BUTTON_EVENT_TAP, events, callbacks, capacity, &count);
                if (click_count_ < 3U) {
                    click_count_++;
                }
                click_deadline_ms_ = now_ms + double_click_ms_;
            }
        }
    }

    if (stable_pressed_) {
        const uint32_t held_ms = now_ms - press_start_ms_;
        if (!long_detected_ && held_ms >= long_click_ms_) {
            long_detected_ = true;
            long_click_count_ = 0U;
            last_long_report_ms_ = now_ms;
            queueEvent(BUTTON_EVENT_LONG_DETECTED, events, callbacks, capacity, &count);
        } else if (long_detected_ && long_detect_retrigger_ &&
                   (now_ms - last_long_report_ms_) >= long_click_ms_) {
            last_long_report_ms_ = now_ms;
            queueEvent(BUTTON_EVENT_LONG_DETECTED, events, callbacks, capacity, &count);
        }
    } else if (click_deadline_ms_ != 0U && now_ms >= click_deadline_ms_) {
        finalizeClicks(events, callbacks, capacity, &count);
    }

    return count;
}

void Button::dispatch(ButtonEvent *events, button_callback_t *callbacks, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (events[i] == BUTTON_EVENT_NONE) {
            continue;
        }

        button_callback_t callback = callbacks[i];
        if (callback != nullptr) {
            callback(*this);
        }
    }
}

esp_err_t Button::init(int pin, int mode, bool active_low)
{
    if (!isValidMode(mode)) {
        ESP_LOGE(TAG, "invalid button mode: %d", mode);
        return ESP_ERR_INVALID_ARG;
    }

    if (configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ensureMutex();
    if (err != ESP_OK) {
        return err;
    }

    err = gpio.cfg(pin, toGpioMode(mode));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "button GPIO config failed on pin %d: %s", pin, esp_err_to_name(err));
        return err;
    }

    if (!lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    pin_ = pin;
    active_low_ = active_low;
    configured_ = true;
    started_ = false;
    poll_ms_ = DEFAULT_POLL_MS;
    resetRuntimeState();

    const uint32_t now_ms = nowMs();
    raw_pressed_ = samplePressed();
    stable_pressed_ = raw_pressed_;
    last_change_ms_ = now_ms;

    unlock();
    return ESP_OK;
}

esp_err_t Button::start(uint32_t poll_ms, UBaseType_t priority, BaseType_t core)
{
    if (poll_ms == 0U) {
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

    poll_ms_ = poll_ms;
    task_handle_ = xTaskCreateStaticPinnedToCore(
        taskEntry,
        "esp_button",
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

esp_err_t Button::stop(void)
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

esp_err_t Button::end(void)
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
    pin_ = -1;
    started_ = false;
    poll_ms_ = DEFAULT_POLL_MS;
    resetRuntimeState();

    if (mutex_ != nullptr) {
        unlock();
    }
    return ESP_OK;
}

bool Button::ready(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return configured_;
    }

    const bool value = configured_;
    unlock();
    return value;
}

bool Button::started(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return started_;
    }

    const bool value = started_;
    unlock();
    return value;
}

esp_err_t Button::loop(void)
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

    ButtonEvent events[MAX_PENDING_EVENTS] = {};
    button_callback_t callbacks[MAX_PENDING_EVENTS] = {};
    const size_t count = process(events, callbacks, MAX_PENDING_EVENTS);
    unlock();

    dispatch(events, callbacks, count);
    return ESP_OK;
}

esp_err_t Button::debounceMs(uint32_t ms)
{
    if (ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mutex_ != nullptr && !lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    debounce_ms_ = ms;

    if (mutex_ != nullptr) {
        unlock();
    }
    return ESP_OK;
}

esp_err_t Button::longClickMs(uint32_t ms)
{
    if (ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mutex_ != nullptr && !lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    long_click_ms_ = ms;

    if (mutex_ != nullptr) {
        unlock();
    }
    return ESP_OK;
}

esp_err_t Button::doubleClickMs(uint32_t ms)
{
    if (ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mutex_ != nullptr && !lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    double_click_ms_ = ms;

    if (mutex_ != nullptr) {
        unlock();
    }
    return ESP_OK;
}

esp_err_t Button::longDetectRetrigger(bool enable)
{
    if (mutex_ != nullptr && !lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    long_detect_retrigger_ = enable;

    if (mutex_ != nullptr) {
        unlock();
    }
    return ESP_OK;
}

esp_err_t Button::on(ButtonEvent event, button_callback_t callback)
{
    const size_t index = static_cast<size_t>(event);
    if (event == BUTTON_EVENT_NONE || index >= CALLBACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mutex_ != nullptr && !lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    callbacks_[index] = callback;

    if (mutex_ != nullptr) {
        unlock();
    }
    return ESP_OK;
}

esp_err_t Button::onChanged(button_callback_t callback)
{
    return on(BUTTON_EVENT_CHANGED, callback);
}

esp_err_t Button::onPressed(button_callback_t callback)
{
    return on(BUTTON_EVENT_PRESSED, callback);
}

esp_err_t Button::onReleased(button_callback_t callback)
{
    return on(BUTTON_EVENT_RELEASED, callback);
}

esp_err_t Button::onTap(button_callback_t callback)
{
    return on(BUTTON_EVENT_TAP, callback);
}

esp_err_t Button::onClick(button_callback_t callback)
{
    return on(BUTTON_EVENT_CLICK, callback);
}

esp_err_t Button::onDoubleClick(button_callback_t callback)
{
    return on(BUTTON_EVENT_DOUBLE_CLICK, callback);
}

esp_err_t Button::onTripleClick(button_callback_t callback)
{
    return on(BUTTON_EVENT_TRIPLE_CLICK, callback);
}

esp_err_t Button::onLongDetected(button_callback_t callback)
{
    return on(BUTTON_EVENT_LONG_DETECTED, callback);
}

esp_err_t Button::onLongClick(button_callback_t callback)
{
    return on(BUTTON_EVENT_LONG_CLICK, callback);
}

int Button::pin(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return pin_;
    }

    const int value = pin_;
    unlock();
    return value;
}

bool Button::pressed(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return stable_pressed_;
    }

    const bool value = stable_pressed_;
    unlock();
    return value;
}

bool Button::activeLow(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return active_low_;
    }

    const bool value = active_low_;
    unlock();
    return value;
}

uint32_t Button::heldMs(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return stable_pressed_ ? (nowMs() - press_start_ms_) : 0U;
    }

    const uint32_t value = stable_pressed_ ? (nowMs() - press_start_ms_) : 0U;
    unlock();
    return value;
}

uint32_t Button::lastPressMs(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return last_press_duration_ms_;
    }

    const uint32_t value = last_press_duration_ms_;
    unlock();
    return value;
}

uint8_t Button::clickCount(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return click_count_;
    }

    const uint8_t value = click_count_;
    unlock();
    return value;
}

uint8_t Button::longClickCount(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return long_click_count_;
    }

    const uint8_t value = long_click_count_;
    unlock();
    return value;
}

ButtonEvent Button::event(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return last_event_;
    }

    const ButtonEvent value = last_event_;
    unlock();
    return value;
}

Button button;

} // namespace esp_button
