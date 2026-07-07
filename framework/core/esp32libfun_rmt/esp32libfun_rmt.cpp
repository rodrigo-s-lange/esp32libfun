#include "esp32libfun_rmt.hpp"

#include <string.h>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static_assert(sizeof(esp32libfun::RmtSymbol) == sizeof(rmt_symbol_word_t),
              "esp32libfun::RmtSymbol must be layout-compatible with rmt_symbol_word_t");

namespace {

static const char *TAG = "ESP32LIBFUN_RMT";

static constexpr size_t RMT_RX_QUEUE_LEN = 8;
static constexpr uint32_t RMT_RX_TASK_STACK_WORDS = 2048;

struct RmtSlot {
    bool used = false;
    bool is_tx = false;
    int pin = -1;
    rmt_channel_handle_t channel = nullptr;
    rmt_encoder_handle_t bytes_encoder = nullptr;
    rmt_encoder_handle_t copy_encoder = nullptr;
    bool bits_configured = false;
    esp32libfun::rmt_rx_callback_t rx_callback = nullptr;
    void *rx_user_ctx = nullptr;
};

struct RmtRxEvent {
    int pin = -1;
    const rmt_symbol_word_t *symbols = nullptr;
    size_t count = 0;
};

StaticSemaphore_t s_rmt_mutex_storage = {};
SemaphoreHandle_t s_rmt_mutex = nullptr;
portMUX_TYPE s_rmt_sync_lock = portMUX_INITIALIZER_UNLOCKED;
RmtSlot s_rmt_slots[esp32libfun::Rmt::MAX_CHANNELS] = {};
StaticQueue_t s_rmt_rx_queue_storage = {};
uint8_t s_rmt_rx_queue_buffer[RMT_RX_QUEUE_LEN * sizeof(RmtRxEvent)] = {};
QueueHandle_t s_rmt_rx_queue = nullptr;
StaticTask_t s_rmt_rx_task_storage = {};
StackType_t s_rmt_rx_task_stack[RMT_RX_TASK_STACK_WORDS] = {};
TaskHandle_t s_rmt_rx_task = nullptr;

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

bool isValidTxPin(int pin)
{
    return GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(pin));
}

bool isValidRxPin(int pin)
{
    return GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(pin));
}

bool IRAM_ATTR onRmtRxDone(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_ctx)
{
    (void)rx_chan;

    auto *slot = static_cast<RmtSlot *>(user_ctx);
    if (slot == nullptr || s_rmt_rx_queue == nullptr) {
        return false;
    }

    RmtRxEvent event = {};
    event.pin = slot->pin;
    event.symbols = edata->received_symbols;
    event.count = edata->num_symbols;

    BaseType_t higher_priority_woken = pdFALSE;
    xQueueSendFromISR(s_rmt_rx_queue, &event, &higher_priority_woken);
    return higher_priority_woken == pdTRUE;
}

void rmtRxCallbackTask(void *arg)
{
    (void)arg;

    RmtRxEvent event = {};
    while (true) {
        if (xQueueReceive(s_rmt_rx_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp32libfun::rmt_rx_callback_t callback = nullptr;
        void *user_ctx = nullptr;

        if (s_rmt_mutex != nullptr && xSemaphoreTake(s_rmt_mutex, portMAX_DELAY) == pdTRUE) {
            for (size_t i = 0; i < esp32libfun::Rmt::MAX_CHANNELS; ++i) {
                if (s_rmt_slots[i].used && s_rmt_slots[i].pin == event.pin) {
                    callback = s_rmt_slots[i].rx_callback;
                    user_ctx = s_rmt_slots[i].rx_user_ctx;
                    break;
                }
            }
            xSemaphoreGive(s_rmt_mutex);
        }

        if (callback != nullptr) {
            callback(event.pin, reinterpret_cast<const esp32libfun::RmtSymbol *>(event.symbols), event.count, user_ctx);
        }
    }
}

void deleteSlot(RmtSlot &slot)
{
    if (slot.channel != nullptr) {
        rmt_disable(slot.channel);
    }
    if (slot.bytes_encoder != nullptr) {
        rmt_del_encoder(slot.bytes_encoder);
    }
    if (slot.copy_encoder != nullptr) {
        rmt_del_encoder(slot.copy_encoder);
    }
    if (slot.channel != nullptr) {
        rmt_del_channel(slot.channel);
    }
    slot = {};
}

} // namespace

namespace esp32libfun {

esp_err_t Rmt::ensureSyncPrimitives(void)
{
    portENTER_CRITICAL(&s_rmt_sync_lock);
    if (s_rmt_mutex == nullptr) {
        s_rmt_mutex = xSemaphoreCreateMutexStatic(&s_rmt_mutex_storage);
    }
    portEXIT_CRITICAL(&s_rmt_sync_lock);

    return (s_rmt_mutex != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t Rmt::ensureCallbackRuntime(void)
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_rmt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_rmt_rx_queue == nullptr) {
        s_rmt_rx_queue = xQueueCreateStatic(
            RMT_RX_QUEUE_LEN,
            sizeof(RmtRxEvent),
            s_rmt_rx_queue_buffer,
            &s_rmt_rx_queue_storage);
        if (s_rmt_rx_queue == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_rmt_rx_task == nullptr) {
        s_rmt_rx_task = xTaskCreateStatic(
            rmtRxCallbackTask,
            "rmt_rx_cb",
            RMT_RX_TASK_STACK_WORDS,
            nullptr,
            tskIDLE_PRIORITY + 1U,
            s_rmt_rx_task_stack,
            &s_rmt_rx_task_storage);
        if (s_rmt_rx_task == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

int Rmt::findSlotByPin(int pin)
{
    for (size_t i = 0; i < MAX_CHANNELS; ++i) {
        if (s_rmt_slots[i].used && s_rmt_slots[i].pin == pin) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Rmt::findFreeSlot(void)
{
    for (size_t i = 0; i < MAX_CHANNELS; ++i) {
        if (!s_rmt_slots[i].used) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

esp_err_t Rmt::beginTx(int pin, uint32_t resolution_hz, size_t mem_block_symbols, size_t trans_queue_depth) const
{
    if (!isValidTxPin(pin)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_rmt_mutex);
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

    rmt_tx_channel_config_t cfg = {};
    cfg.gpio_num = static_cast<gpio_num_t>(pin);
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = resolution_hz;
    cfg.mem_block_symbols = mem_block_symbols;
    cfg.trans_queue_depth = trans_queue_depth;

    rmt_channel_handle_t channel_handle = nullptr;
    err = rmt_new_tx_channel(&cfg, &channel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel(pin=%d) failed: %s", pin, esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(channel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable(pin=%d) failed: %s", pin, esp_err_to_name(err));
        rmt_del_channel(channel_handle);
        return err;
    }

    RmtSlot &slot = s_rmt_slots[slot_index];
    slot.used = true;
    slot.is_tx = true;
    slot.pin = pin;
    slot.channel = channel_handle;

    ESP_LOGI(TAG, "TX channel ready on pin %d @ %lu Hz", pin, static_cast<unsigned long>(resolution_hz));
    return ESP_OK;
}

esp_err_t Rmt::beginRx(int pin, uint32_t resolution_hz, size_t mem_block_symbols) const
{
    if (!isValidRxPin(pin)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureCallbackRuntime();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_rmt_mutex);
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

    rmt_rx_channel_config_t cfg = {};
    cfg.gpio_num = static_cast<gpio_num_t>(pin);
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = resolution_hz;
    cfg.mem_block_symbols = mem_block_symbols;

    rmt_channel_handle_t channel_handle = nullptr;
    err = rmt_new_rx_channel(&cfg, &channel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_rx_channel(pin=%d) failed: %s", pin, esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(channel_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable(pin=%d) failed: %s", pin, esp_err_to_name(err));
        rmt_del_channel(channel_handle);
        return err;
    }

    RmtSlot &slot = s_rmt_slots[slot_index];
    slot.used = true;
    slot.is_tx = false;
    slot.pin = pin;
    slot.channel = channel_handle;

    rmt_rx_event_callbacks_t cbs = {};
    cbs.on_recv_done = onRmtRxDone;
    err = rmt_rx_register_event_callbacks(channel_handle, &cbs, &slot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_rx_register_event_callbacks(pin=%d) failed: %s", pin, esp_err_to_name(err));
        slot = {};
        rmt_disable(channel_handle);
        rmt_del_channel(channel_handle);
        return err;
    }

    ESP_LOGI(TAG, "RX channel ready on pin %d @ %lu Hz", pin, static_cast<unsigned long>(resolution_hz));
    return ESP_OK;
}

esp_err_t Rmt::end(int pin) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_rmt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    deleteSlot(s_rmt_slots[slot_index]);
    return ESP_OK;
}

bool Rmt::ready(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return false;
    }

    LockGuard guard(s_rmt_mutex);
    if (!guard.locked()) {
        return false;
    }

    return findSlotByPin(pin) >= 0;
}

void *Rmt::channel(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return nullptr;
    }

    LockGuard guard(s_rmt_mutex);
    if (!guard.locked()) {
        return nullptr;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return nullptr;
    }

    return s_rmt_slots[slot_index].channel;
}

esp_err_t Rmt::carrier(int pin, uint32_t freq_hz, float duty, bool active_low) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_rmt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    rmt_carrier_config_t cfg = {};
    cfg.frequency_hz = freq_hz;
    cfg.duty_cycle = duty;
    cfg.flags.polarity_active_low = active_low ? 1 : 0;

    return rmt_apply_carrier(s_rmt_slots[slot_index].channel, &cfg);
}

esp_err_t Rmt::carrierOff(int pin) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_rmt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return rmt_apply_carrier(s_rmt_slots[slot_index].channel, nullptr);
}

esp_err_t Rmt::bits(int pin, rmt_symbol_t bit0, rmt_symbol_t bit1, bool msb_first) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_rmt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    RmtSlot &slot = s_rmt_slots[slot_index];
    if (!slot.is_tx) {
        return ESP_ERR_INVALID_STATE;
    }

    rmt_bytes_encoder_config_t cfg = {};
    memcpy(&cfg.bit0, &bit0, sizeof(cfg.bit0));
    memcpy(&cfg.bit1, &bit1, sizeof(cfg.bit1));
    cfg.flags.msb_first = msb_first ? 1 : 0;

    if (slot.bytes_encoder == nullptr) {
        err = rmt_new_bytes_encoder(&cfg, &slot.bytes_encoder);
    } else {
        err = rmt_bytes_encoder_update_config(slot.bytes_encoder, &cfg);
    }

    if (err == ESP_OK) {
        slot.bits_configured = true;
    }
    return err;
}

esp_err_t Rmt::write(int pin, const uint8_t *data, size_t len, bool wait) const
{
    if (data == nullptr && len > 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_rmt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    RmtSlot &slot = s_rmt_slots[slot_index];
    if (!slot.is_tx || !slot.bits_configured) {
        return ESP_ERR_INVALID_STATE;
    }

    rmt_transmit_config_t tx_cfg = {};
    err = rmt_transmit(slot.channel, slot.bytes_encoder, data, len, &tx_cfg);
    if (err != ESP_OK) {
        return err;
    }

    return wait ? rmt_tx_wait_all_done(slot.channel, -1) : ESP_OK;
}

esp_err_t Rmt::writeSymbols(int pin, const rmt_symbol_t *symbols, size_t count, bool wait) const
{
    if (symbols == nullptr && count > 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_rmt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    RmtSlot &slot = s_rmt_slots[slot_index];
    if (!slot.is_tx) {
        return ESP_ERR_INVALID_STATE;
    }

    if (slot.copy_encoder == nullptr) {
        rmt_copy_encoder_config_t copy_cfg = {};
        err = rmt_new_copy_encoder(&copy_cfg, &slot.copy_encoder);
        if (err != ESP_OK) {
            return err;
        }
    }

    rmt_transmit_config_t tx_cfg = {};
    err = rmt_transmit(slot.channel, slot.copy_encoder, symbols, count * sizeof(rmt_symbol_t), &tx_cfg);
    if (err != ESP_OK) {
        return err;
    }

    return wait ? rmt_tx_wait_all_done(slot.channel, -1) : ESP_OK;
}

esp_err_t Rmt::wait(int pin, int timeout_ms) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_rmt_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return rmt_tx_wait_all_done(s_rmt_slots[slot_index].channel, timeout_ms);
}

esp_err_t Rmt::receive(int pin, rmt_symbol_t *buffer, size_t buffer_symbols,
                        uint32_t signal_range_min_ns, uint32_t signal_range_max_ns,
                        rmt_rx_callback_t callback, void *user_ctx) const
{
    if (buffer == nullptr || buffer_symbols == 0 || callback == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureCallbackRuntime();
    if (err != ESP_OK) {
        return err;
    }

    rmt_channel_handle_t channel_handle = nullptr;

    {
        LockGuard guard(s_rmt_mutex);
        if (!guard.locked()) {
            return ESP_ERR_TIMEOUT;
        }

        const int slot_index = findSlotByPin(pin);
        if (slot_index < 0) {
            return ESP_ERR_NOT_FOUND;
        }

        RmtSlot &slot = s_rmt_slots[slot_index];
        if (slot.is_tx) {
            return ESP_ERR_INVALID_STATE;
        }

        slot.rx_callback = callback;
        slot.rx_user_ctx = user_ctx;
        channel_handle = slot.channel;
    }

    rmt_receive_config_t rx_cfg = {};
    rx_cfg.signal_range_min_ns = signal_range_min_ns;
    rx_cfg.signal_range_max_ns = signal_range_max_ns;

    return rmt_receive(channel_handle, buffer, buffer_symbols * sizeof(rmt_symbol_t), &rx_cfg);
}

Rmt rmt;

} // namespace esp32libfun
