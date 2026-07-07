#include "esp32libfun_twai.hpp"

#include <string.h>

#include "driver/gpio.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "hal/twai_types.h"
#include "soc/soc_caps.h"

namespace {

static constexpr size_t TWAI_RX_QUEUE_LEN = 16;

struct TwaiSlot {
    bool used = false;
    int tx_pin = -1;
    int rx_pin = -1;
    uint32_t bitrate = 0;
    twai_node_handle_t node = nullptr;
};

struct TwaiRxEvent {
    int tx_pin = -1;
    esp32libfun::TwaiFrame frame = {};
};

StaticSemaphore_t s_twai_mutex_storage = {};
SemaphoreHandle_t s_twai_mutex = nullptr;
portMUX_TYPE s_twai_sync_lock = portMUX_INITIALIZER_UNLOCKED;
TwaiSlot s_twai_slots[esp32libfun::Twai::MAX_NODES] = {};
StaticQueue_t s_twai_rx_queue_storage = {};
uint8_t s_twai_rx_queue_buffer[TWAI_RX_QUEUE_LEN * sizeof(TwaiRxEvent)] = {};
QueueHandle_t s_twai_rx_queue = nullptr;

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

bool isValidOutputPin(int pin)
{
    return GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(pin));
}

bool isValidInputPin(int pin)
{
    return GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(pin));
}

bool isValidId(uint32_t id, bool ext)
{
    return ext ? ((id & ~TWAI_EXT_ID_MASK) == 0) : ((id & ~TWAI_STD_ID_MASK) == 0);
}

TickType_t timeoutTicks(int timeout_ms)
{
    if (timeout_ms < 0) {
        return portMAX_DELAY;
    }
    return pdMS_TO_TICKS(timeout_ms);
}

size_t frameDataLength(const twai_frame_t &frame)
{
    size_t len = frame.header.fdf ? twaifd_dlc2len(frame.header.dlc) : frame.header.dlc;
    if (len > frame.buffer_len) {
        len = frame.buffer_len;
    }
    if (len > esp32libfun::TWAI_MAX_DATA_LEN) {
        len = esp32libfun::TWAI_MAX_DATA_LEN;
    }
    return len;
}

bool IRAM_ATTR onTwaiRxDone(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx)
{
    (void)edata;

    auto *slot = static_cast<TwaiSlot *>(user_ctx);
    if (slot == nullptr || s_twai_rx_queue == nullptr) {
        return false;
    }

    TwaiRxEvent event = {};
    event.tx_pin = slot->tx_pin;

    twai_frame_t native = {};
    native.buffer = event.frame.data;
    native.buffer_len = sizeof(event.frame.data);
    if (twai_node_receive_from_isr(handle, &native) != ESP_OK) {
        return false;
    }

    event.frame.id = native.header.id;
    event.frame.len = frameDataLength(native);
    event.frame.ext = native.header.ide != 0;
    event.frame.rtr = native.header.rtr != 0;
    event.frame.fd = native.header.fdf != 0;
    event.frame.brs = native.header.brs != 0;
    event.frame.timestamp = native.header.timestamp;

    BaseType_t higher_priority_woken = pdFALSE;
    xQueueSendFromISR(s_twai_rx_queue, &event, &higher_priority_woken);
    return higher_priority_woken == pdTRUE;
}

esp_err_t deleteSlot(TwaiSlot &slot)
{
    if (slot.node != nullptr) {
        twai_node_disable(slot.node);
        twai_node_delete(slot.node);
    }
    slot = {};
    return ESP_OK;
}

} // namespace

namespace esp32libfun {

esp_err_t Twai::ensureSyncPrimitives(void)
{
    portENTER_CRITICAL(&s_twai_sync_lock);
    if (s_twai_mutex == nullptr) {
        s_twai_mutex = xSemaphoreCreateMutexStatic(&s_twai_mutex_storage);
    }
    portEXIT_CRITICAL(&s_twai_sync_lock);

    return (s_twai_mutex != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t Twai::ensureRxRuntime(void)
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_twai_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_twai_rx_queue == nullptr) {
        s_twai_rx_queue = xQueueCreateStatic(
            TWAI_RX_QUEUE_LEN,
            sizeof(TwaiRxEvent),
            s_twai_rx_queue_buffer,
            &s_twai_rx_queue_storage);
        if (s_twai_rx_queue == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

int Twai::findSlotByTxPin(int tx_pin)
{
    for (size_t i = 0; i < MAX_NODES; ++i) {
        if (s_twai_slots[i].used && s_twai_slots[i].tx_pin == tx_pin) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Twai::findFreeSlot(void)
{
    for (size_t i = 0; i < MAX_NODES; ++i) {
        if (!s_twai_slots[i].used) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

esp_err_t Twai::begin(int tx_pin,
                      int rx_pin,
                      uint32_t bitrate,
                      size_t tx_queue_depth,
                      bool loopback,
                      bool self_test,
                      bool listen_only) const
{
    if (!isValidOutputPin(tx_pin) || !isValidInputPin(rx_pin) || bitrate == 0 || tx_queue_depth == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureRxRuntime();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_twai_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (findSlotByTxPin(tx_pin) >= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const int slot_index = findFreeSlot();
    if (slot_index < 0) {
        return ESP_ERR_NO_MEM;
    }

    TwaiSlot &slot = s_twai_slots[slot_index];
    slot.tx_pin = tx_pin;
    slot.rx_pin = rx_pin;
    slot.bitrate = bitrate;

    twai_onchip_node_config_t cfg = {};
    cfg.io_cfg.tx = static_cast<gpio_num_t>(tx_pin);
    cfg.io_cfg.rx = static_cast<gpio_num_t>(rx_pin);
    cfg.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    cfg.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    cfg.clk_src = TWAI_CLK_SRC_DEFAULT;
    cfg.bit_timing.bitrate = bitrate;
    cfg.fail_retry_cnt = -1;
    cfg.tx_queue_depth = tx_queue_depth;
    cfg.flags.enable_loopback = loopback ? 1 : 0;
    cfg.flags.enable_self_test = self_test ? 1 : 0;
    cfg.flags.enable_listen_only = listen_only ? 1 : 0;

    err = twai_new_node_onchip(&cfg, &slot.node);
    if (err != ESP_OK) {
        slot = {};
        return err;
    }

    twai_event_callbacks_t cbs = {};
    cbs.on_rx_done = onTwaiRxDone;
    err = twai_node_register_event_callbacks(slot.node, &cbs, &slot);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = twai_node_enable(slot.node);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    slot.used = true;
    return ESP_OK;
}

esp_err_t Twai::end(int tx_pin) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_twai_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByTxPin(tx_pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return deleteSlot(s_twai_slots[slot_index]);
}

bool Twai::ready(int tx_pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return false;
    }

    LockGuard guard(s_twai_mutex);
    if (!guard.locked()) {
        return false;
    }

    return findSlotByTxPin(tx_pin) >= 0;
}

void *Twai::node(int tx_pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return nullptr;
    }

    LockGuard guard(s_twai_mutex);
    if (!guard.locked()) {
        return nullptr;
    }

    const int slot_index = findSlotByTxPin(tx_pin);
    if (slot_index < 0) {
        return nullptr;
    }

    return s_twai_slots[slot_index].node;
}

esp_err_t Twai::write(int tx_pin, const TwaiFrame &frame, int timeout_ms) const
{
    if (!isValidId(frame.id, frame.ext) || frame.len > TWAI_MAX_DATA_LEN || (!frame.fd && frame.len > TWAI_FRAME_MAX_LEN)) {
        return ESP_ERR_INVALID_ARG;
    }

#if !SOC_TWAI_FD_SUPPORTED
    if (frame.fd || frame.brs) {
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_twai_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByTxPin(tx_pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    twai_frame_t native = {};
    native.header.id = frame.id;
    native.header.dlc = frame.fd ? twaifd_len2dlc(static_cast<uint16_t>(frame.len)) : static_cast<uint16_t>(frame.len);
    native.header.ide = frame.ext ? 1 : 0;
    native.header.rtr = frame.rtr ? 1 : 0;
    native.header.fdf = frame.fd ? 1 : 0;
    native.header.brs = frame.brs ? 1 : 0;
    native.buffer = const_cast<uint8_t *>(frame.data);
    native.buffer_len = frame.len;

    return twai_node_transmit(s_twai_slots[slot_index].node, &native, timeout_ms);
}

esp_err_t Twai::write(int tx_pin, uint32_t id, const uint8_t *data, size_t len, bool ext, int timeout_ms) const
{
    if (data == nullptr && len > 0) {
        return ESP_ERR_INVALID_ARG;
    }

    TwaiFrame frame = {};
    frame.id = id;
    frame.len = len;
    frame.ext = ext;
    if (len > 0) {
        memcpy(frame.data, data, len <= sizeof(frame.data) ? len : sizeof(frame.data));
    }

    return write(tx_pin, frame, timeout_ms);
}

esp_err_t Twai::read(int tx_pin, TwaiFrame *frame, int timeout_ms) const
{
    if (frame == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureRxRuntime();
    if (err != ESP_OK) {
        return err;
    }

    if (ensureSyncPrimitives() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }

    {
        LockGuard guard(s_twai_mutex);
        if (!guard.locked()) {
            return ESP_ERR_TIMEOUT;
        }
        if (findSlotByTxPin(tx_pin) < 0) {
            return ESP_ERR_NOT_FOUND;
        }
    }

    TwaiRxEvent event = {};
    const TickType_t wait_ticks = timeoutTicks(timeout_ms);
    while (xQueueReceive(s_twai_rx_queue, &event, wait_ticks) == pdTRUE) {
        if (event.tx_pin == tx_pin) {
            *frame = event.frame;
            return ESP_OK;
        }
        xQueueSend(s_twai_rx_queue, &event, 0);
        if (timeout_ms == 0) {
            break;
        }
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t Twai::wait(int tx_pin, int timeout_ms) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_twai_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByTxPin(tx_pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return twai_node_transmit_wait_all_done(s_twai_slots[slot_index].node, timeout_ms);
}

esp_err_t Twai::recover(int tx_pin) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_twai_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByTxPin(tx_pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return twai_node_recover(s_twai_slots[slot_index].node);
}

esp_err_t Twai::status(int tx_pin, TwaiStatus *status) const
{
    if (status == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_twai_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByTxPin(tx_pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    twai_node_status_t native_status = {};
    twai_node_record_t record = {};
    err = twai_node_get_info(s_twai_slots[slot_index].node, &native_status, &record);
    if (err != ESP_OK) {
        return err;
    }

    status->state = native_status.state;
    status->tx_error_count = native_status.tx_error_count;
    status->rx_error_count = native_status.rx_error_count;
    status->tx_queue_remaining = native_status.tx_queue_remaining;
    status->bus_error_count = record.bus_err_num;
    return ESP_OK;
}

esp_err_t Twai::filter(int tx_pin, uint32_t id, uint32_t mask, bool ext, uint8_t filter_id) const
{
    if (!isValidId(id, ext)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_twai_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByTxPin(tx_pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    twai_mask_filter_config_t cfg = {};
    cfg.id = id;
    cfg.mask = mask;
    cfg.is_ext = ext ? 1 : 0;
    cfg.no_fd = false;
    cfg.no_classic = false;
    cfg.dual_filter = false;

    return twai_node_config_mask_filter(s_twai_slots[slot_index].node, filter_id, &cfg);
}

Twai twai;

} // namespace esp32libfun
