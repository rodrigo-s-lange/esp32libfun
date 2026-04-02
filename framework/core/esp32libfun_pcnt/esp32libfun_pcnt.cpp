#include "esp32libfun_pcnt.hpp"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

static constexpr size_t PCNT_QUEUE_LEN = 16;
static constexpr uint32_t PCNT_TASK_STACK_WORDS = 2048;

struct PcntSlot {
    bool used = false;
    bool started = false;
    int pin = -1;
    int edge = 0;
    int watch_point = 0;
    bool watch_enabled = false;
    pcnt_unit_handle_t unit = nullptr;
    pcnt_channel_handle_t channel = nullptr;
    esp32libfun::pcnt_callback_t callback = nullptr;
    void *user_ctx = nullptr;
};

struct PcntEvent {
    int pin = -1;
    int watch_point = 0;
};

StaticSemaphore_t s_pcnt_mutex_storage = {};
SemaphoreHandle_t s_pcnt_mutex = nullptr;
portMUX_TYPE s_pcnt_sync_lock = portMUX_INITIALIZER_UNLOCKED;
PcntSlot s_pcnt_slots[esp32libfun::Pcnt::MAX_COUNTERS] = {};
StaticQueue_t s_pcnt_queue_storage = {};
uint8_t s_pcnt_queue_buffer[PCNT_QUEUE_LEN * sizeof(PcntEvent)] = {};
QueueHandle_t s_pcnt_queue = nullptr;
StaticTask_t s_pcnt_task_storage = {};
StackType_t s_pcnt_task_stack[PCNT_TASK_STACK_WORDS] = {};
TaskHandle_t s_pcnt_task = nullptr;

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

bool isValidInputPin(int pin)
{
    return GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(pin));
}

bool IRAM_ATTR onPcntReach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)
{
    (void)unit;

    auto *slot = static_cast<PcntSlot *>(user_ctx);
    if (slot == nullptr || s_pcnt_queue == nullptr) {
        return false;
    }

    PcntEvent event = {};
    event.pin = slot->pin;
    event.watch_point = edata->watch_point_value;

    BaseType_t higher_priority_woken = pdFALSE;
    xQueueSendFromISR(s_pcnt_queue, &event, &higher_priority_woken);
    return higher_priority_woken == pdTRUE;
}

void pcntCallbackTask(void *arg)
{
    (void)arg;

    PcntEvent event = {};
    while (true) {
        if (xQueueReceive(s_pcnt_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp32libfun::pcnt_callback_t callback = nullptr;
        void *user_ctx = nullptr;

        if (s_pcnt_mutex != nullptr && xSemaphoreTake(s_pcnt_mutex, portMAX_DELAY) == pdTRUE) {
            for (size_t i = 0; i < esp32libfun::Pcnt::MAX_COUNTERS; ++i) {
                if (s_pcnt_slots[i].used && s_pcnt_slots[i].pin == event.pin) {
                    callback = s_pcnt_slots[i].callback;
                    user_ctx = s_pcnt_slots[i].user_ctx;
                    break;
                }
            }
            xSemaphoreGive(s_pcnt_mutex);
        }

        if (callback != nullptr) {
            callback(event.pin, event.watch_point, user_ctx);
        }
    }
}

esp_err_t deleteSlot(PcntSlot &slot)
{
    if (slot.unit != nullptr) {
        pcnt_unit_stop(slot.unit);
        pcnt_unit_disable(slot.unit);
    }
    if (slot.watch_enabled && slot.unit != nullptr) {
        pcnt_unit_remove_watch_point(slot.unit, slot.watch_point);
    }
    if (slot.channel != nullptr) {
        pcnt_del_channel(slot.channel);
    }
    if (slot.unit != nullptr) {
        pcnt_del_unit(slot.unit);
    }

    slot = {};
    return ESP_OK;
}

} // namespace

namespace esp32libfun {

esp_err_t Pcnt::ensureSyncPrimitives(void)
{
    portENTER_CRITICAL(&s_pcnt_sync_lock);
    if (s_pcnt_mutex == nullptr) {
        s_pcnt_mutex = xSemaphoreCreateMutexStatic(&s_pcnt_mutex_storage);
    }
    portEXIT_CRITICAL(&s_pcnt_sync_lock);

    return (s_pcnt_mutex != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t Pcnt::ensureCallbackRuntime(void)
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_pcnt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_pcnt_queue == nullptr) {
        s_pcnt_queue = xQueueCreateStatic(
            PCNT_QUEUE_LEN,
            sizeof(PcntEvent),
            s_pcnt_queue_buffer,
            &s_pcnt_queue_storage);
        if (s_pcnt_queue == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_pcnt_task == nullptr) {
        s_pcnt_task = xTaskCreateStatic(
            pcntCallbackTask,
            "pcnt_cb",
            PCNT_TASK_STACK_WORDS,
            nullptr,
            tskIDLE_PRIORITY + 1U,
            s_pcnt_task_stack,
            &s_pcnt_task_storage);
        if (s_pcnt_task == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

int Pcnt::findSlotByPin(int pin)
{
    for (size_t i = 0; i < MAX_COUNTERS; ++i) {
        if (s_pcnt_slots[i].used && s_pcnt_slots[i].pin == pin) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Pcnt::findFreeSlot(void)
{
    for (size_t i = 0; i < MAX_COUNTERS; ++i) {
        if (!s_pcnt_slots[i].used) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

esp_err_t Pcnt::begin(int pin, int edge, int low_limit, int high_limit, uint32_t glitch_ns) const
{
    if (!isValidInputPin(pin) || low_limit >= 0 || high_limit <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (edge != PCNT_RISE && edge != PCNT_FALL && edge != PCNT_BOTH) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_pcnt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (findSlotByPin(pin) >= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const int slot_index = findFreeSlot();
    if (slot_index < 0) {
        return ESP_ERR_NO_MEM;
    }

    pcnt_unit_config_t unit_cfg = {};
    unit_cfg.low_limit = low_limit;
    unit_cfg.high_limit = high_limit;
    unit_cfg.flags.accum_count = 1;

    pcnt_unit_handle_t unit = nullptr;
    err = pcnt_new_unit(&unit_cfg, &unit);
    if (err != ESP_OK) {
        return err;
    }

    if (glitch_ns > 0) {
        pcnt_glitch_filter_config_t filter_cfg = {};
        filter_cfg.max_glitch_ns = glitch_ns;
        err = pcnt_unit_set_glitch_filter(unit, &filter_cfg);
        if (err != ESP_OK) {
            pcnt_del_unit(unit);
            return err;
        }
    }

    pcnt_chan_config_t channel_cfg = {};
    channel_cfg.edge_gpio_num = pin;
    channel_cfg.level_gpio_num = -1;
    channel_cfg.flags.virt_level_io_level = 0;

    pcnt_channel_handle_t channel_handle = nullptr;
    err = pcnt_new_channel(unit, &channel_cfg, &channel_handle);
    if (err != ESP_OK) {
        pcnt_del_unit(unit);
        return err;
    }

    pcnt_channel_edge_action_t pos = PCNT_CHANNEL_EDGE_ACTION_HOLD;
    pcnt_channel_edge_action_t neg = PCNT_CHANNEL_EDGE_ACTION_HOLD;
    if (edge == PCNT_RISE || edge == PCNT_BOTH) {
        pos = PCNT_CHANNEL_EDGE_ACTION_INCREASE;
    }
    if (edge == PCNT_FALL || edge == PCNT_BOTH) {
        neg = PCNT_CHANNEL_EDGE_ACTION_INCREASE;
    }

    err = pcnt_channel_set_edge_action(channel_handle, pos, neg);
    if (err != ESP_OK) {
        pcnt_del_channel(channel_handle);
        pcnt_del_unit(unit);
        return err;
    }

    err = pcnt_channel_set_level_action(channel_handle, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP);
    if (err != ESP_OK) {
        pcnt_del_channel(channel_handle);
        pcnt_del_unit(unit);
        return err;
    }

    pcnt_event_callbacks_t cbs = {};
    cbs.on_reach = onPcntReach;
    PcntSlot *slot = &s_pcnt_slots[slot_index];
    err = pcnt_unit_register_event_callbacks(unit, &cbs, slot);
    if (err != ESP_OK) {
        pcnt_del_channel(channel_handle);
        pcnt_del_unit(unit);
        return err;
    }

    slot->used = true;
    slot->started = false;
    slot->pin = pin;
    slot->edge = edge;
    slot->unit = unit;
    slot->channel = channel_handle;

    err = pcnt_unit_enable(unit);
    if (err != ESP_OK) {
        *slot = {};
        pcnt_del_channel(channel_handle);
        pcnt_del_unit(unit);
        return err;
    }

    err = pcnt_unit_clear_count(unit);
    if (err != ESP_OK) {
        *slot = {};
        pcnt_unit_disable(unit);
        pcnt_del_channel(channel_handle);
        pcnt_del_unit(unit);
        return err;
    }

    err = pcnt_unit_start(unit);
    if (err != ESP_OK) {
        *slot = {};
        pcnt_unit_disable(unit);
        pcnt_del_channel(channel_handle);
        pcnt_del_unit(unit);
        return err;
    }

    slot->started = true;

    return ESP_OK;
}

esp_err_t Pcnt::end(int pin) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_pcnt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return deleteSlot(s_pcnt_slots[slot_index]);
}

bool Pcnt::ready(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return false;
    }

    LockGuard guard(s_pcnt_mutex);
    if (!guard.locked()) {
        return false;
    }

    return findSlotByPin(pin) >= 0;
}

esp_err_t Pcnt::start(int pin) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_pcnt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    err = pcnt_unit_start(s_pcnt_slots[slot_index].unit);
    if (err == ESP_OK) {
        s_pcnt_slots[slot_index].started = true;
    }
    return err;
}

esp_err_t Pcnt::stop(int pin) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_pcnt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    err = pcnt_unit_stop(s_pcnt_slots[slot_index].unit);
    if (err == ESP_OK) {
        s_pcnt_slots[slot_index].started = false;
    }
    return err;
}

esp_err_t Pcnt::clear(int pin) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_pcnt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return pcnt_unit_clear_count(s_pcnt_slots[slot_index].unit);
}

esp_err_t Pcnt::count(int pin, int *value) const
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_pcnt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return pcnt_unit_get_count(s_pcnt_slots[slot_index].unit, value);
}

int Pcnt::count(int pin) const
{
    int value = 0;
    if (count(pin, &value) != ESP_OK) {
        return 0;
    }
    return value;
}

esp_err_t Pcnt::watch(int pin, int watch_point, pcnt_callback_t callback, void *user_ctx) const
{
    if (callback == nullptr) {
        return watchOff(pin);
    }

    esp_err_t err = ensureCallbackRuntime();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_pcnt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    auto &slot = s_pcnt_slots[slot_index];
    if (slot.watch_enabled) {
        pcnt_unit_remove_watch_point(slot.unit, slot.watch_point);
    }

    err = pcnt_unit_add_watch_point(slot.unit, watch_point);
    if (err != ESP_OK) {
        return err;
    }

    slot.watch_point = watch_point;
    slot.watch_enabled = true;
    slot.callback = callback;
    slot.user_ctx = user_ctx;

    return ESP_OK;
}

esp_err_t Pcnt::watchOff(int pin) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_pcnt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    auto &slot = s_pcnt_slots[slot_index];
    if (slot.watch_enabled) {
        err = pcnt_unit_remove_watch_point(slot.unit, slot.watch_point);
        if (err != ESP_OK) {
            return err;
        }
        slot.watch_enabled = false;
    }

    slot.callback = nullptr;
    slot.user_ctx = nullptr;
    return ESP_OK;
}

Pcnt pcnt;

} // namespace esp32libfun
