#include "esp32libfun_gptimer.hpp"

#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

static constexpr size_t GPTIMER_QUEUE_LEN = 16;
static constexpr uint32_t GPTIMER_TASK_STACK_WORDS = 2048;

struct GptimerSlot {
    bool used = false;
    bool enabled = false;
    bool running = false;
    int id = -1;
    uint32_t resolution_hz = 0;
    gptimer_handle_t handle = nullptr;
    esp32libfun::gptimer_callback_t callback = nullptr;
    void *user_ctx = nullptr;
};

struct GptimerEvent {
    int timer = -1;
    uint64_t count = 0;
};

StaticSemaphore_t s_gptimer_mutex_storage = {};
SemaphoreHandle_t s_gptimer_mutex = nullptr;
portMUX_TYPE s_gptimer_sync_lock = portMUX_INITIALIZER_UNLOCKED;
GptimerSlot s_gptimer_slots[esp32libfun::Gptimer::MAX_TIMERS] = {};
StaticQueue_t s_gptimer_queue_storage = {};
uint8_t s_gptimer_queue_buffer[GPTIMER_QUEUE_LEN * sizeof(GptimerEvent)] = {};
QueueHandle_t s_gptimer_queue = nullptr;
StaticTask_t s_gptimer_task_storage = {};
StackType_t s_gptimer_task_stack[GPTIMER_TASK_STACK_WORDS] = {};
TaskHandle_t s_gptimer_task = nullptr;

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

bool validTimer(int timer)
{
    return timer >= 0 && timer < static_cast<int>(esp32libfun::Gptimer::MAX_TIMERS);
}

uint64_t usToTicks(uint64_t us, uint32_t resolution_hz)
{
    return (us * static_cast<uint64_t>(resolution_hz) + 999999ULL) / 1000000ULL;
}

uint64_t ticksToUs(uint64_t ticks, uint32_t resolution_hz)
{
    if (resolution_hz == 0) {
        return 0;
    }
    return (ticks * 1000000ULL) / static_cast<uint64_t>(resolution_hz);
}

bool IRAM_ATTR onGptimerAlarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    (void)timer;

    auto *slot = static_cast<GptimerSlot *>(user_ctx);
    if (slot == nullptr || slot->callback == nullptr || s_gptimer_queue == nullptr) {
        return false;
    }

    GptimerEvent event = {};
    event.timer = slot->id;
    event.count = edata->count_value;

    BaseType_t higher_priority_woken = pdFALSE;
    xQueueSendFromISR(s_gptimer_queue, &event, &higher_priority_woken);
    return higher_priority_woken == pdTRUE;
}

void gptimerCallbackTask(void *arg)
{
    (void)arg;

    GptimerEvent event = {};
    while (true) {
        if (xQueueReceive(s_gptimer_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp32libfun::gptimer_callback_t callback = nullptr;
        void *user_ctx = nullptr;

        if (s_gptimer_mutex != nullptr && xSemaphoreTake(s_gptimer_mutex, portMAX_DELAY) == pdTRUE) {
            if (validTimer(event.timer) && s_gptimer_slots[event.timer].used) {
                callback = s_gptimer_slots[event.timer].callback;
                user_ctx = s_gptimer_slots[event.timer].user_ctx;
            }
            xSemaphoreGive(s_gptimer_mutex);
        }

        if (callback != nullptr) {
            callback(event.timer, event.count, user_ctx);
        }
    }
}

esp_err_t deleteSlot(GptimerSlot &slot)
{
    if (slot.handle != nullptr && slot.running) {
        gptimer_stop(slot.handle);
    }
    if (slot.handle != nullptr && slot.enabled) {
        gptimer_disable(slot.handle);
    }
    if (slot.handle != nullptr) {
        gptimer_del_timer(slot.handle);
    }
    slot = {};
    return ESP_OK;
}

} // namespace

namespace esp32libfun {

esp_err_t Gptimer::ensureSyncPrimitives(void)
{
    portENTER_CRITICAL(&s_gptimer_sync_lock);
    if (s_gptimer_mutex == nullptr) {
        s_gptimer_mutex = xSemaphoreCreateMutexStatic(&s_gptimer_mutex_storage);
    }
    portEXIT_CRITICAL(&s_gptimer_sync_lock);

    return (s_gptimer_mutex != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t Gptimer::ensureCallbackRuntime(void)
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gptimer_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_gptimer_queue == nullptr) {
        s_gptimer_queue = xQueueCreateStatic(
            GPTIMER_QUEUE_LEN,
            sizeof(GptimerEvent),
            s_gptimer_queue_buffer,
            &s_gptimer_queue_storage);
        if (s_gptimer_queue == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_gptimer_task == nullptr) {
        s_gptimer_task = xTaskCreateStatic(
            gptimerCallbackTask,
            "gptimer_cb",
            GPTIMER_TASK_STACK_WORDS,
            nullptr,
            tskIDLE_PRIORITY + 1U,
            s_gptimer_task_stack,
            &s_gptimer_task_storage);
        if (s_gptimer_task == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

esp_err_t Gptimer::begin(int timer, uint32_t resolution_hz, bool start_now) const
{
    if (!validTimer(timer) || resolution_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureCallbackRuntime();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gptimer_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_gptimer_slots[timer].used) {
        return ESP_ERR_INVALID_STATE;
    }

    GptimerSlot &slot = s_gptimer_slots[timer];
    slot.id = timer;
    slot.resolution_hz = resolution_hz;

    gptimer_config_t cfg = {};
    cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    cfg.direction = GPTIMER_COUNT_UP;
    cfg.resolution_hz = resolution_hz;

    err = gptimer_new_timer(&cfg, &slot.handle);
    if (err != ESP_OK) {
        slot = {};
        return err;
    }

    gptimer_event_callbacks_t cbs = {};
    cbs.on_alarm = onGptimerAlarm;
    err = gptimer_register_event_callbacks(slot.handle, &cbs, &slot);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = gptimer_enable(slot.handle);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }
    slot.enabled = true;

    if (start_now) {
        err = gptimer_start(slot.handle);
        if (err != ESP_OK) {
            deleteSlot(slot);
            return err;
        }
        slot.running = true;
    }

    slot.used = true;
    return ESP_OK;
}

esp_err_t Gptimer::end(int timer) const
{
    if (!validTimer(timer)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gptimer_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_gptimer_slots[timer].used) {
        return ESP_ERR_NOT_FOUND;
    }

    return deleteSlot(s_gptimer_slots[timer]);
}

bool Gptimer::ready(int timer) const
{
    if (!validTimer(timer) || ensureSyncPrimitives() != ESP_OK) {
        return false;
    }

    LockGuard guard(s_gptimer_mutex);
    if (!guard.locked()) {
        return false;
    }

    return s_gptimer_slots[timer].used;
}

esp_err_t Gptimer::start(int timer) const
{
    if (!validTimer(timer)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gptimer_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    GptimerSlot &slot = s_gptimer_slots[timer];
    if (!slot.used) {
        return ESP_ERR_NOT_FOUND;
    }

    err = gptimer_start(slot.handle);
    if (err == ESP_OK) {
        slot.running = true;
    }
    return err;
}

esp_err_t Gptimer::stop(int timer) const
{
    if (!validTimer(timer)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gptimer_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    GptimerSlot &slot = s_gptimer_slots[timer];
    if (!slot.used) {
        return ESP_ERR_NOT_FOUND;
    }

    err = gptimer_stop(slot.handle);
    if (err == ESP_OK) {
        slot.running = false;
    }
    return err;
}

esp_err_t Gptimer::clear(int timer) const
{
    if (!validTimer(timer)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gptimer_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_gptimer_slots[timer].used) {
        return ESP_ERR_NOT_FOUND;
    }

    return gptimer_set_raw_count(s_gptimer_slots[timer].handle, 0);
}

esp_err_t Gptimer::count(int timer, uint64_t *value) const
{
    if (!validTimer(timer) || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gptimer_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_gptimer_slots[timer].used) {
        return ESP_ERR_NOT_FOUND;
    }

    return gptimer_get_raw_count(s_gptimer_slots[timer].handle, value);
}

uint64_t Gptimer::count(int timer) const
{
    uint64_t value = 0;
    if (count(timer, &value) != ESP_OK) {
        return 0;
    }
    return value;
}

uint64_t Gptimer::micros(int timer) const
{
    if (!validTimer(timer) || ensureSyncPrimitives() != ESP_OK) {
        return 0;
    }

    LockGuard guard(s_gptimer_mutex);
    if (!guard.locked() || !s_gptimer_slots[timer].used) {
        return 0;
    }

    uint64_t value = 0;
    if (gptimer_get_raw_count(s_gptimer_slots[timer].handle, &value) != ESP_OK) {
        return 0;
    }
    return ticksToUs(value, s_gptimer_slots[timer].resolution_hz);
}

uint64_t Gptimer::millis(int timer) const
{
    return micros(timer) / 1000ULL;
}

uint32_t Gptimer::resolution(int timer) const
{
    if (!validTimer(timer) || ensureSyncPrimitives() != ESP_OK) {
        return 0;
    }

    LockGuard guard(s_gptimer_mutex);
    if (!guard.locked() || !s_gptimer_slots[timer].used) {
        return 0;
    }

    uint32_t resolution_hz = 0;
    if (gptimer_get_resolution(s_gptimer_slots[timer].handle, &resolution_hz) != ESP_OK) {
        return 0;
    }
    return resolution_hz;
}

esp_err_t Gptimer::alarm(int timer,
                         uint64_t period_us,
                         gptimer_callback_t callback,
                         void *user_ctx,
                         bool auto_reload) const
{
    if (!validTimer(timer) || period_us == 0 || callback == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureCallbackRuntime();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gptimer_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    GptimerSlot &slot = s_gptimer_slots[timer];
    if (!slot.used) {
        return ESP_ERR_NOT_FOUND;
    }

    const uint64_t alarm_count = usToTicks(period_us, slot.resolution_hz);
    if (alarm_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    gptimer_alarm_config_t cfg = {};
    cfg.alarm_count = alarm_count;
    cfg.reload_count = 0;
    cfg.flags.auto_reload_on_alarm = auto_reload ? 1 : 0;

    slot.callback = callback;
    slot.user_ctx = user_ctx;

    err = gptimer_set_raw_count(slot.handle, 0);
    if (err != ESP_OK) {
        slot.callback = nullptr;
        slot.user_ctx = nullptr;
        return err;
    }

    err = gptimer_set_alarm_action(slot.handle, &cfg);
    if (err != ESP_OK) {
        slot.callback = nullptr;
        slot.user_ctx = nullptr;
    }
    return err;
}

esp_err_t Gptimer::alarmOff(int timer) const
{
    if (!validTimer(timer)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_gptimer_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    GptimerSlot &slot = s_gptimer_slots[timer];
    if (!slot.used) {
        return ESP_ERR_NOT_FOUND;
    }

    err = gptimer_set_alarm_action(slot.handle, nullptr);
    if (err == ESP_OK) {
        slot.callback = nullptr;
        slot.user_ctx = nullptr;
    }
    return err;
}

Gptimer gptimer;

} // namespace esp32libfun
