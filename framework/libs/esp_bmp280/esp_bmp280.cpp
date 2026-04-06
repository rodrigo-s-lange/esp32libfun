#include "esp_bmp280.hpp"
#include "esp32libfun_i2c.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esp_bmp280 {

namespace {

static const char *TAG = "ESP_BMP280";

constexpr uint8_t kRegCalib = 0x88;
constexpr uint8_t kRegChipId = 0xD0;
constexpr uint8_t kRegReset = 0xE0;
constexpr uint8_t kRegCtrlMeas = 0xF4;
constexpr uint8_t kRegConfig = 0xF5;
constexpr uint8_t kRegData = 0xF7;

constexpr uint8_t kResetValue = 0xB6;
constexpr uint8_t kCtrlMeasForced = 0x2D;
constexpr uint8_t kConfigValue = 0x00;

constexpr uint32_t kResetDelayMs = 20;
constexpr uint32_t kMeasureDelayMs = 30;

TickType_t msToTicks(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    if (ticks == 0 && ms > 0U) {
        ticks = 1;
    }

    return ticks;
}

} // namespace

bool Bmp280::isValidAddress(uint16_t address)
{
    return address == DEFAULT_ADDRESS || address == ALTERNATE_ADDRESS;
}

float Bmp280::compensateTemperature(int32_t adc_t, const Calib &calib, int32_t *t_fine)
{
    int32_t var1 = ((adc_t >> 3) - (static_cast<int32_t>(calib.T1) << 1));
    var1 = (var1 * static_cast<int32_t>(calib.T2)) >> 11;

    int32_t var2 = (adc_t >> 4) - static_cast<int32_t>(calib.T1);
    var2 = ((var2 * var2) >> 12) * static_cast<int32_t>(calib.T3);
    var2 >>= 14;

    *t_fine = var1 + var2;
    return static_cast<float>((*t_fine * 5 + 128) >> 8) / 100.0f;
}

float Bmp280::compensatePressure(int32_t adc_p, const Calib &calib, int32_t t_fine)
{
    int64_t var1 = static_cast<int64_t>(t_fine) - 128000;
    int64_t var2 = var1 * var1 * static_cast<int64_t>(calib.P6);
    var2 += (var1 * static_cast<int64_t>(calib.P5)) << 17;
    var2 += static_cast<int64_t>(calib.P4) << 35;
    var1 = ((var1 * var1 * static_cast<int64_t>(calib.P3)) >> 8) +
            ((var1 * static_cast<int64_t>(calib.P2)) << 12);
    var1 = ((static_cast<int64_t>(1) << 47) + var1) * static_cast<int64_t>(calib.P1) >> 33;

    if (var1 == 0) {
        return 0.0f;
    }

    int64_t pressure = 1048576 - adc_p;
    pressure = (((pressure << 31) - var2) * 3125) / var1;
    var1 = (static_cast<int64_t>(calib.P9) * (pressure >> 13) * (pressure >> 13)) >> 25;
    var2 = (static_cast<int64_t>(calib.P8) * pressure) >> 19;
    pressure = ((pressure + var1 + var2) >> 8) + (static_cast<int64_t>(calib.P7) << 4);

    return static_cast<float>(pressure) / 25600.0f;
}

esp_err_t Bmp280::ensureMutex(void)
{
    if (mutex_ != nullptr) {
        return ESP_OK;
    }

    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    return (mutex_ != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool Bmp280::lock(void) const
{
    return mutex_ != nullptr && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
}

void Bmp280::unlock(void) const
{
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void Bmp280::resetState(void)
{
    configured_ = false;
    started_ = false;
    interval_ms_ = DEFAULT_INTERVAL_MS;
    address_ = DEFAULT_ADDRESS;
    port_ = 0;
    temperature_ = 0.0f;
    pressure_ = 0.0f;
    calib_ = {};
    callback_ = nullptr;
}

esp_err_t Bmp280::readCalib(void)
{
    uint8_t buf[24] = {};
    const esp_err_t err = i2c.regRead(address_, kRegCalib, buf, sizeof(buf), port_);
    if (err != ESP_OK) {
        return err;
    }

    auto u16 = [&](int index) -> uint16_t {
        return static_cast<uint16_t>(buf[index]) | (static_cast<uint16_t>(buf[index + 1]) << 8);
    };
    auto s16 = [&](int index) -> int16_t {
        return static_cast<int16_t>(u16(index));
    };

    calib_.T1 = u16(0);
    calib_.T2 = s16(2);
    calib_.T3 = s16(4);
    calib_.P1 = u16(6);
    calib_.P2 = s16(8);
    calib_.P3 = s16(10);
    calib_.P4 = s16(12);
    calib_.P5 = s16(14);
    calib_.P6 = s16(16);
    calib_.P7 = s16(18);
    calib_.P8 = s16(20);
    calib_.P9 = s16(22);

    if (calib_.T1 == 0U || calib_.P1 == 0U) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

void Bmp280::step(void)
{
    const esp_err_t err = read();
    bmp280_callback_t callback = nullptr;

    if (err == ESP_OK && lock()) {
        callback = callback_;
        unlock();
    }

    if (err == ESP_OK && callback != nullptr) {
        callback(*this);
    }
}

void Bmp280::taskEntry(void *arg)
{
    Bmp280 *self = static_cast<Bmp280 *>(arg);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        bool should_stop = false;
        TickType_t delay_ticks = 1;

        if (self->lock()) {
            should_stop = !self->started_;
            if (should_stop) {
                self->task_handle_ = nullptr;
            } else {
                delay_ticks = msToTicks(self->interval_ms_);
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

esp_err_t Bmp280::init(uint16_t address, int port)
{
    if (configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!isValidAddress(address)) {
        ESP_LOGE(TAG, "invalid address 0x%02X - use 0x%02X or 0x%02X",
                 address,
                 DEFAULT_ADDRESS,
                 ALTERNATE_ADDRESS);
        return ESP_ERR_INVALID_ARG;
    }

    if (!i2c.ready(port)) {
        ESP_LOGE(TAG, "bus %d not ready - call i2c.begin() first", port);
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

    uint8_t chip_id = 0;
    err = i2c.regRead8(address, kRegChipId, &chip_id, port);
    if (err != ESP_OK) {
        i2c.remove(address, port);
        ESP_LOGE(TAG, "chip id read failed: %s", esp_err_to_name(err));
        return err;
    }

    if (chip_id != CHIP_ID) {
        i2c.remove(address, port);
        ESP_LOGE(TAG, "unexpected chip id 0x%02X at 0x%02X (expected 0x%02X)",
                 chip_id,
                 address,
                 CHIP_ID);
        return ESP_ERR_INVALID_RESPONSE;
    }

    err = i2c.regWrite8(address, kRegReset, kResetValue, port);
    if (err != ESP_OK) {
        i2c.remove(address, port);
        ESP_LOGE(TAG, "reset failed at 0x%02X: %s", address, esp_err_to_name(err));
        return err;
    }

    vTaskDelay(msToTicks(kResetDelayMs));

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

    err = readCalib();
    if (err != ESP_OK) {
        i2c.remove(address, port);
        if (lock()) {
            resetState();
            unlock();
        }
        ESP_LOGE(TAG, "calibration read failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c.regWrite8(address_, kRegConfig, kConfigValue, port_);
    if (err != ESP_OK) {
        i2c.remove(address, port);
        if (lock()) {
            resetState();
            unlock();
        }
        ESP_LOGE(TAG, "config write failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!lock()) {
        i2c.remove(address, port);
        resetState();
        return ESP_ERR_INVALID_STATE;
    }

    configured_ = true;
    unlock();

    ESP_LOGI(TAG, "init ok addr=0x%02X port=%d", address, port);
    return ESP_OK;
}

esp_err_t Bmp280::start(uint32_t interval_ms, UBaseType_t priority, BaseType_t core)
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
        "bmp280",
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

esp_err_t Bmp280::stop(void)
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

esp_err_t Bmp280::end(void)
{
    if (mutex_ != nullptr && task_handle_ == xTaskGetCurrentTaskHandle()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = stop();
    if (err != ESP_OK) {
        return err;
    }

    if (configured_) {
        const esp_err_t remove_err = i2c.remove(address_, port_);
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

esp_err_t Bmp280::read(void)
{
    if (!configured_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2c.regWrite8(address_, kRegCtrlMeas, kCtrlMeasForced, port_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ctrl_meas write failed: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(msToTicks(kMeasureDelayMs));

    uint8_t buf[6] = {};
    err = i2c.regRead(address_, kRegData, buf, sizeof(buf), port_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "data read failed: %s", esp_err_to_name(err));
        return err;
    }

    const int32_t adc_p = (static_cast<int32_t>(buf[0]) << 12) |
                          (static_cast<int32_t>(buf[1]) << 4) |
                          (buf[2] >> 4);
    const int32_t adc_t = (static_cast<int32_t>(buf[3]) << 12) |
                          (static_cast<int32_t>(buf[4]) << 4) |
                          (buf[5] >> 4);

    Calib calib_copy = {};
    if (!lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    calib_copy = calib_;
    unlock();

    int32_t t_fine = 0;
    const float temperature = compensateTemperature(adc_t, calib_copy, &t_fine);
    const float pressure = compensatePressure(adc_p, calib_copy, t_fine);

    if (lock()) {
        temperature_ = temperature;
        pressure_ = pressure;
        unlock();
    }

    return ESP_OK;
}

esp_err_t Bmp280::onRead(bmp280_callback_t callback)
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

esp_err_t Bmp280::intervalMs(uint32_t interval_ms)
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

bool Bmp280::ready(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return configured_;
    }

    const bool value = configured_;
    unlock();
    return value;
}

bool Bmp280::started(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return started_;
    }

    const bool value = started_;
    unlock();
    return value;
}

esp_err_t Bmp280::loop(void)
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

float Bmp280::temperature(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return temperature_;
    }

    const float value = temperature_;
    unlock();
    return value;
}

float Bmp280::pressure(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return pressure_;
    }

    const float value = pressure_;
    unlock();
    return value;
}

uint16_t Bmp280::address(void) const
{
    return address_;
}

int Bmp280::port(void) const
{
    return port_;
}

uint32_t Bmp280::intervalMs(void) const
{
    if (mutex_ == nullptr || !lock()) {
        return interval_ms_;
    }

    const uint32_t value = interval_ms_;
    unlock();
    return value;
}

Bmp280 bmp280;

} // namespace esp_bmp280
