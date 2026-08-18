#include "esp32libfun_adc.hpp"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "soc/soc_caps.h"

#ifndef SOC_ADC_PERIPH_NUM
#define SOC_ADC_PERIPH_NUM 2
#endif

namespace {

enum class CalibrationKind {
    None,
    CurveFitting,
    LineFitting,
};

struct AdcUnitSlot {
    adc_oneshot_unit_handle_t handle = nullptr;
    size_t users = 0;
};

struct AdcSlot {
    bool used = false;
    int pin = -1;
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    adc_atten_t atten = ADC_ATTEN_DB_12;
    adc_bitwidth_t bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_cali_handle_t cali = nullptr;
    CalibrationKind cali_kind = CalibrationKind::None;
};

StaticSemaphore_t s_adc_mutex_storage = {};
SemaphoreHandle_t s_adc_mutex = nullptr;
portMUX_TYPE s_adc_sync_lock = portMUX_INITIALIZER_UNLOCKED;
AdcUnitSlot s_adc_units[SOC_ADC_PERIPH_NUM] = {};
AdcSlot s_adc_slots[esp32libfun::Adc::MAX_CHANNELS] = {};

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

int unitIndex(adc_unit_t unit)
{
    const int index = static_cast<int>(unit) - static_cast<int>(ADC_UNIT_1);
    if (index < 0 || index >= static_cast<int>(SOC_ADC_PERIPH_NUM)) {
        return -1;
    }
    return index;
}

esp_err_t ensureUnit(adc_unit_t unit)
{
    const int index = unitIndex(unit);
    if (index < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_adc_units[index].handle != nullptr) {
        ++s_adc_units[index].users;
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t cfg = {};
    cfg.unit_id = unit;
    // adc_oneshot_clk_src_t aliases a different enum depending on whether the
    // chip drives ADC oneshot through the RTC controller (ESP32/S2/S3) or
    // only through the digital controller (e.g. ESP32-C3/C6); each variant
    // only declares its own *_CLK_SRC_DEFAULT value.
#if SOC_ADC_RTC_CTRL_SUPPORTED
    cfg.clk_src = ADC_RTC_CLK_SRC_DEFAULT;
#else
    cfg.clk_src = ADC_DIGI_CLK_SRC_DEFAULT;
#endif
    cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    esp_err_t err = adc_oneshot_new_unit(&cfg, &s_adc_units[index].handle);
    if (err == ESP_OK) {
        s_adc_units[index].users = 1;
    }
    return err;
}

void releaseUnit(adc_unit_t unit)
{
    const int index = unitIndex(unit);
    if (index < 0 || s_adc_units[index].handle == nullptr || s_adc_units[index].users == 0) {
        return;
    }

    --s_adc_units[index].users;
    if (s_adc_units[index].users == 0) {
        adc_oneshot_del_unit(s_adc_units[index].handle);
        s_adc_units[index].handle = nullptr;
    }
}

adc_oneshot_unit_handle_t unitHandle(adc_unit_t unit)
{
    const int index = unitIndex(unit);
    if (index < 0) {
        return nullptr;
    }
    return s_adc_units[index].handle;
}

void deleteCalibration(AdcSlot &slot)
{
    if (slot.cali == nullptr) {
        return;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (slot.cali_kind == CalibrationKind::CurveFitting) {
        adc_cali_delete_scheme_curve_fitting(slot.cali);
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (slot.cali_kind == CalibrationKind::LineFitting) {
        adc_cali_delete_scheme_line_fitting(slot.cali);
    }
#endif

    slot.cali = nullptr;
    slot.cali_kind = CalibrationKind::None;
}

esp_err_t createCalibration(AdcSlot &slot)
{
    adc_cali_scheme_ver_t scheme_mask = {};
    if (adc_cali_check_scheme(&scheme_mask) != ESP_OK) {
        return ESP_ERR_NOT_SUPPORTED;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if ((scheme_mask & ADC_CALI_SCHEME_VER_CURVE_FITTING) != 0) {
        adc_cali_curve_fitting_config_t cfg = {};
        cfg.unit_id = slot.unit;
        cfg.chan = slot.channel;
        cfg.atten = slot.atten;
        cfg.bitwidth = slot.bitwidth;
        if (adc_cali_create_scheme_curve_fitting(&cfg, &slot.cali) == ESP_OK) {
            slot.cali_kind = CalibrationKind::CurveFitting;
            return ESP_OK;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if ((scheme_mask & ADC_CALI_SCHEME_VER_LINE_FITTING) != 0) {
        adc_cali_line_fitting_config_t cfg = {};
        cfg.unit_id = slot.unit;
        cfg.atten = slot.atten;
        cfg.bitwidth = slot.bitwidth;
#if CONFIG_IDF_TARGET_ESP32
        cfg.default_vref = 1100;
#endif
        if (adc_cali_create_scheme_line_fitting(&cfg, &slot.cali) == ESP_OK) {
            slot.cali_kind = CalibrationKind::LineFitting;
            return ESP_OK;
        }
    }
#endif

    return ESP_ERR_NOT_SUPPORTED;
}

} // namespace

namespace esp32libfun {

esp_err_t Adc::ensureSyncPrimitives(void)
{
    portENTER_CRITICAL(&s_adc_sync_lock);
    if (s_adc_mutex == nullptr) {
        s_adc_mutex = xSemaphoreCreateMutexStatic(&s_adc_mutex_storage);
    }
    portEXIT_CRITICAL(&s_adc_sync_lock);

    return (s_adc_mutex != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

int Adc::findSlotByPin(int pin)
{
    for (size_t i = 0; i < MAX_CHANNELS; ++i) {
        if (s_adc_slots[i].used && s_adc_slots[i].pin == pin) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Adc::findFreeSlot(void)
{
    for (size_t i = 0; i < MAX_CHANNELS; ++i) {
        if (!s_adc_slots[i].used) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

esp_err_t Adc::begin(int pin, adc_atten_t atten, adc_bitwidth_t bitwidth, bool calibrate) const
{
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t adc_channel = ADC_CHANNEL_0;
    esp_err_t err = adc_oneshot_io_to_channel(pin, &unit, &adc_channel);
    if (err != ESP_OK) {
        return err;
    }

    err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_adc_mutex);
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

    err = ensureUnit(unit);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = atten;
    chan_cfg.bitwidth = bitwidth;
    err = adc_oneshot_config_channel(unitHandle(unit), adc_channel, &chan_cfg);
    if (err != ESP_OK) {
        releaseUnit(unit);
        return err;
    }

    AdcSlot &slot = s_adc_slots[slot_index];
    slot.used = true;
    slot.pin = pin;
    slot.unit = unit;
    slot.channel = adc_channel;
    slot.atten = atten;
    slot.bitwidth = bitwidth;

    if (calibrate) {
        createCalibration(slot);
    }

    return ESP_OK;
}

esp_err_t Adc::end(int pin) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_adc_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    AdcSlot &slot = s_adc_slots[slot_index];
    deleteCalibration(slot);
    releaseUnit(slot.unit);
    slot = {};
    return ESP_OK;
}

bool Adc::ready(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return false;
    }

    LockGuard guard(s_adc_mutex);
    if (!guard.locked()) {
        return false;
    }

    return findSlotByPin(pin) >= 0;
}

esp_err_t Adc::read(int pin, int *value) const
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_adc_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const AdcSlot &slot = s_adc_slots[slot_index];
    return adc_oneshot_read(unitHandle(slot.unit), slot.channel, value);
}

int Adc::read(int pin) const
{
    int value = 0;
    if (read(pin, &value) != ESP_OK) {
        return 0;
    }
    return value;
}

esp_err_t Adc::voltage(int pin, int *millivolts) const
{
    if (millivolts == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_adc_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const AdcSlot &slot = s_adc_slots[slot_index];
    if (slot.cali == nullptr) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return adc_oneshot_get_calibrated_result(unitHandle(slot.unit), slot.cali, slot.channel, millivolts);
}

int Adc::voltage(int pin) const
{
    int millivolts = 0;
    if (voltage(pin, &millivolts) != ESP_OK) {
        return 0;
    }
    return millivolts;
}

esp_err_t Adc::channel(int pin, adc_unit_t *unit, adc_channel_t *channel) const
{
    if (unit == nullptr || channel == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    return adc_oneshot_io_to_channel(pin, unit, channel);
}

Adc adc;

} // namespace esp32libfun
