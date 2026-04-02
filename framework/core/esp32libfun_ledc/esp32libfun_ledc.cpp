#include "esp32libfun_ledc.hpp"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"

namespace {

static const char *TAG = "ESP32LIBFUN_LEDC";
static constexpr ledc_mode_t SPEED_MODE = LEDC_LOW_SPEED_MODE;

struct LedcTimerSlot {
    bool used = false;
    uint32_t freq_hz = 0;
    uint8_t resolution_bits = 0;
    size_t refs = 0;
};

struct LedcChannelSlot {
    bool used = false;
    int pin = -1;
    int timer_index = -1;
    bool invert = false;
};

StaticSemaphore_t s_ledc_mutex_storage = {};
SemaphoreHandle_t s_ledc_mutex = nullptr;
portMUX_TYPE s_ledc_sync_lock = portMUX_INITIALIZER_UNLOCKED;
bool s_ledc_fade_ready = false;
LedcTimerSlot s_ledc_timers[LEDC_TIMER_MAX] = {};
LedcChannelSlot s_ledc_channels[SOC_LEDC_CHANNEL_NUM] = {};

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

uint32_t maxDutyForBits(uint8_t bits)
{
    if (bits == 0 || bits >= 31U) {
        return 0;
    }
    return (1UL << bits) - 1UL;
}

esp_err_t configureTimer(int timer_index, uint32_t freq_hz, uint8_t resolution_bits)
{
    ledc_timer_config_t cfg = {};
    cfg.speed_mode = SPEED_MODE;
    cfg.timer_num = static_cast<ledc_timer_t>(timer_index);
    cfg.freq_hz = freq_hz;
    cfg.duty_resolution = static_cast<ledc_timer_bit_t>(resolution_bits);
    cfg.clk_cfg = LEDC_AUTO_CLK;
    return ledc_timer_config(&cfg);
}

esp_err_t deconfigureTimer(int timer_index)
{
    esp_err_t err = ledc_timer_pause(SPEED_MODE, static_cast<ledc_timer_t>(timer_index));
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    ledc_timer_config_t cfg = {};
    cfg.speed_mode = SPEED_MODE;
    cfg.timer_num = static_cast<ledc_timer_t>(timer_index);
    cfg.deconfigure = true;
    return ledc_timer_config(&cfg);
}

} // namespace

namespace esp32libfun {

esp_err_t Ledc::ensureSyncPrimitives(void)
{
    portENTER_CRITICAL(&s_ledc_sync_lock);
    if (s_ledc_mutex == nullptr) {
        s_ledc_mutex = xSemaphoreCreateMutexStatic(&s_ledc_mutex_storage);
    }
    portEXIT_CRITICAL(&s_ledc_sync_lock);

    return (s_ledc_mutex != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t Ledc::ensureFadeSupport(void)
{
    if (s_ledc_fade_ready) {
        return ESP_OK;
    }

    esp_err_t err = ledc_fade_func_install(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "ledc_fade_func_install failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ledc_fade_ready = true;
    return ESP_OK;
}

int Ledc::findChannelByPin(int pin)
{
    for (size_t i = 0; i < SOC_LEDC_CHANNEL_NUM; ++i) {
        if (s_ledc_channels[i].used && s_ledc_channels[i].pin == pin) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Ledc::findFreeChannel(void)
{
    for (size_t i = 0; i < SOC_LEDC_CHANNEL_NUM; ++i) {
        if (!s_ledc_channels[i].used) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Ledc::findCompatibleTimer(uint32_t freq_hz, uint8_t resolution_bits)
{
    for (int i = 0; i < LEDC_TIMER_MAX; ++i) {
        if (s_ledc_timers[i].used &&
            s_ledc_timers[i].freq_hz == freq_hz &&
            s_ledc_timers[i].resolution_bits == resolution_bits) {
            return i;
        }
    }
    return -1;
}

int Ledc::findFreeTimer(void)
{
    for (int i = 0; i < LEDC_TIMER_MAX; ++i) {
        if (!s_ledc_timers[i].used) {
            return i;
        }
    }
    return -1;
}

esp_err_t Ledc::begin(int pin, uint32_t freq_hz, uint8_t resolution_bits, int channel, bool invert) const
{
    if (!isValidOutputPin(pin) || freq_hz == 0 || resolution_bits == 0 || resolution_bits > SOC_LEDC_TIMER_BIT_WIDTH) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_ledc_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    int channel_index = findChannelByPin(pin);
    if (channel_index >= 0) {
        const int timer_index = s_ledc_channels[channel_index].timer_index;
        if (s_ledc_timers[timer_index].freq_hz == freq_hz &&
            s_ledc_timers[timer_index].resolution_bits == resolution_bits &&
            s_ledc_channels[channel_index].invert == invert) {
            return ESP_OK;
        }
        return ESP_ERR_INVALID_STATE;
    }

    if (channel >= 0) {
        if (channel >= static_cast<int>(SOC_LEDC_CHANNEL_NUM) || s_ledc_channels[channel].used) {
            return ESP_ERR_INVALID_ARG;
        }
        channel_index = channel;
    } else {
        channel_index = findFreeChannel();
        if (channel_index < 0) {
            return ESP_ERR_NO_MEM;
        }
    }

    int timer_index = findCompatibleTimer(freq_hz, resolution_bits);
    if (timer_index < 0) {
        timer_index = findFreeTimer();
        if (timer_index < 0) {
            return ESP_ERR_NO_MEM;
        }

        err = configureTimer(timer_index, freq_hz, resolution_bits);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "timer config failed: %s", esp_err_to_name(err));
            return err;
        }

        s_ledc_timers[timer_index].used = true;
        s_ledc_timers[timer_index].freq_hz = freq_hz;
        s_ledc_timers[timer_index].resolution_bits = resolution_bits;
        s_ledc_timers[timer_index].refs = 0;
    }

    ledc_channel_config_t cfg = {};
    cfg.gpio_num = pin;
    cfg.speed_mode = SPEED_MODE;
    cfg.channel = static_cast<ledc_channel_t>(channel_index);
    cfg.timer_sel = static_cast<ledc_timer_t>(timer_index);
    cfg.duty = 0;
    cfg.hpoint = 0;
    cfg.sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;
    cfg.flags.output_invert = invert ? 1U : 0U;

    err = ledc_channel_config(&cfg);
    if (err != ESP_OK) {
        if (s_ledc_timers[timer_index].refs == 0) {
            deconfigureTimer(timer_index);
            s_ledc_timers[timer_index] = {};
        }
        return err;
    }

    s_ledc_timers[timer_index].refs++;
    s_ledc_channels[channel_index].used = true;
    s_ledc_channels[channel_index].pin = pin;
    s_ledc_channels[channel_index].timer_index = timer_index;
    s_ledc_channels[channel_index].invert = invert;

    return ESP_OK;
}

esp_err_t Ledc::end(int pin) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_ledc_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int channel_index = findChannelByPin(pin);
    if (channel_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const int timer_index = s_ledc_channels[channel_index].timer_index;
    err = ledc_stop(SPEED_MODE, static_cast<ledc_channel_t>(channel_index), 0);
    if (err != ESP_OK) {
        return err;
    }

    ledc_channel_config_t cfg = {};
    cfg.speed_mode = SPEED_MODE;
    cfg.channel = static_cast<ledc_channel_t>(channel_index);
    cfg.deconfigure = true;
    err = ledc_channel_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    s_ledc_channels[channel_index] = {};

    if (timer_index >= 0 && s_ledc_timers[timer_index].refs > 0) {
        s_ledc_timers[timer_index].refs--;
        if (s_ledc_timers[timer_index].refs == 0) {
            err = deconfigureTimer(timer_index);
            if (err != ESP_OK) {
                return err;
            }
            s_ledc_timers[timer_index] = {};
        }
    }

    return ESP_OK;
}

bool Ledc::ready(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return false;
    }

    LockGuard guard(s_ledc_mutex);
    if (!guard.locked()) {
        return false;
    }

    return findChannelByPin(pin) >= 0;
}

esp_err_t Ledc::duty(int pin, uint32_t value) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_ledc_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int channel_index = findChannelByPin(pin);
    if (channel_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const uint32_t max_duty = maxDutyForBits(s_ledc_timers[s_ledc_channels[channel_index].timer_index].resolution_bits);
    if (value > max_duty) {
        return ESP_ERR_INVALID_ARG;
    }

    return ledc_set_duty_and_update(SPEED_MODE, static_cast<ledc_channel_t>(channel_index), value, 0);
}

uint32_t Ledc::duty(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return 0;
    }

    LockGuard guard(s_ledc_mutex);
    if (!guard.locked()) {
        return 0;
    }

    const int channel_index = findChannelByPin(pin);
    if (channel_index < 0) {
        return 0;
    }

    return ledc_get_duty(SPEED_MODE, static_cast<ledc_channel_t>(channel_index));
}

esp_err_t Ledc::percent(int pin, float value) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_ledc_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int channel_index = findChannelByPin(pin);
    if (channel_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (value < 0.0f) {
        value = 0.0f;
    }
    if (value > 100.0f) {
        value = 100.0f;
    }

    const uint32_t max_duty = maxDutyForBits(s_ledc_timers[s_ledc_channels[channel_index].timer_index].resolution_bits);
    const uint32_t duty_value = static_cast<uint32_t>((value * static_cast<float>(max_duty)) / 100.0f + 0.5f);
    return ledc_set_duty_and_update(SPEED_MODE, static_cast<ledc_channel_t>(channel_index), duty_value, 0);
}

esp_err_t Ledc::freq(int pin, uint32_t hz) const
{
    if (hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_ledc_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int channel_index = findChannelByPin(pin);
    if (channel_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const int timer_index = s_ledc_channels[channel_index].timer_index;
    if (s_ledc_timers[timer_index].refs > 1 && s_ledc_timers[timer_index].freq_hz != hz) {
        return ESP_ERR_INVALID_STATE;
    }

    err = ledc_set_freq(SPEED_MODE, static_cast<ledc_timer_t>(timer_index), hz);
    if (err != ESP_OK) {
        return err;
    }

    s_ledc_timers[timer_index].freq_hz = hz;
    return ESP_OK;
}

uint32_t Ledc::freq(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return 0;
    }

    LockGuard guard(s_ledc_mutex);
    if (!guard.locked()) {
        return 0;
    }

    const int channel_index = findChannelByPin(pin);
    if (channel_index < 0) {
        return 0;
    }

    return s_ledc_timers[s_ledc_channels[channel_index].timer_index].freq_hz;
}

uint8_t Ledc::resolution(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return 0;
    }

    LockGuard guard(s_ledc_mutex);
    if (!guard.locked()) {
        return 0;
    }

    const int channel_index = findChannelByPin(pin);
    if (channel_index < 0) {
        return 0;
    }

    return s_ledc_timers[s_ledc_channels[channel_index].timer_index].resolution_bits;
}

uint32_t Ledc::maxDuty(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return 0;
    }

    LockGuard guard(s_ledc_mutex);
    if (!guard.locked()) {
        return 0;
    }

    const int channel_index = findChannelByPin(pin);
    if (channel_index < 0) {
        return 0;
    }

    return maxDutyForBits(s_ledc_timers[s_ledc_channels[channel_index].timer_index].resolution_bits);
}

esp_err_t Ledc::fade(int pin, uint32_t target_duty, uint32_t time_ms, bool wait_done) const
{
    if (time_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    err = ensureFadeSupport();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_ledc_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int channel_index = findChannelByPin(pin);
    if (channel_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const uint32_t max_duty = maxDutyForBits(s_ledc_timers[s_ledc_channels[channel_index].timer_index].resolution_bits);
    if (target_duty > max_duty) {
        return ESP_ERR_INVALID_ARG;
    }

    return ledc_set_fade_time_and_start(
        SPEED_MODE,
        static_cast<ledc_channel_t>(channel_index),
        target_duty,
        time_ms,
        wait_done ? LEDC_FADE_WAIT_DONE : LEDC_FADE_NO_WAIT);
}

Ledc ledc;

} // namespace esp32libfun
