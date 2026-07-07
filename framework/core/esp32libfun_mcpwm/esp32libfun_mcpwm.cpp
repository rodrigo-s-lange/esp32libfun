#include "esp32libfun_mcpwm.hpp"

#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "soc/soc_caps.h"

#ifndef SOC_MCPWM_GROUPS
#define SOC_MCPWM_GROUPS 1
#endif

namespace {

struct McpwmSlot {
    bool used = false;
    bool complementary = false;
    int pin_high = -1;
    int pin_low = -1;
    int group = 0;
    uint32_t resolution_hz = 0;
    uint32_t freq_hz = 0;
    float duty_percent = 0.0f;
    uint32_t deadtime_ticks = 0;
    mcpwm_timer_handle_t timer = nullptr;
    mcpwm_oper_handle_t oper = nullptr;
    mcpwm_cmpr_handle_t comparator = nullptr;
    mcpwm_gen_handle_t generator_high = nullptr;
    mcpwm_gen_handle_t generator_low = nullptr;
};

StaticSemaphore_t s_mcpwm_mutex_storage = {};
SemaphoreHandle_t s_mcpwm_mutex = nullptr;
portMUX_TYPE s_mcpwm_sync_lock = portMUX_INITIALIZER_UNLOCKED;
McpwmSlot s_mcpwm_slots[esp32libfun::Mcpwm::MAX_CHANNELS] = {};

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

uint32_t periodTicks(uint32_t resolution_hz, uint32_t freq_hz)
{
    if (resolution_hz == 0 || freq_hz == 0) {
        return 0;
    }
    return resolution_hz / freq_hz;
}

uint32_t compareTicks(uint32_t period_ticks, float duty_percent)
{
    if (duty_percent < 0.0f) {
        duty_percent = 0.0f;
    }
    if (duty_percent > 100.0f) {
        duty_percent = 100.0f;
    }
    return static_cast<uint32_t>((static_cast<float>(period_ticks) * duty_percent) / 100.0f + 0.5f);
}

uint32_t microsecondsToTicks(uint32_t microseconds, uint32_t resolution_hz)
{
    const uint64_t ticks = (static_cast<uint64_t>(microseconds) * static_cast<uint64_t>(resolution_hz) + 999999ULL) / 1000000ULL;
    return static_cast<uint32_t>(ticks);
}

uint32_t ticksToMicroseconds(uint32_t ticks, uint32_t resolution_hz)
{
    if (resolution_hz == 0) {
        return 0;
    }

    const uint64_t microseconds = (static_cast<uint64_t>(ticks) * 1000000ULL) / static_cast<uint64_t>(resolution_hz);
    return static_cast<uint32_t>(microseconds);
}

bool slotOwnsPin(const McpwmSlot &slot, int pin)
{
    return slot.used && (slot.pin_high == pin || slot.pin_low == pin);
}

mcpwm_gen_handle_t generatorForPin(const McpwmSlot &slot, int pin)
{
    if (slot.pin_high == pin) {
        return slot.generator_high;
    }
    if (slot.pin_low == pin) {
        return slot.generator_low;
    }
    return nullptr;
}

int translateForceLevel(const McpwmSlot &slot, int pin, int level)
{
    if (level < 0) {
        return level;
    }

    if (slot.complementary && pin == slot.pin_low) {
        return level == 0 ? 1 : 0;
    }

    return level;
}

esp_err_t configureGeneratorWaveform(mcpwm_gen_handle_t generator, mcpwm_cmpr_handle_t comparator)
{
    esp_err_t err = mcpwm_generator_set_action_on_timer_event(
        generator,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH));
    if (err != ESP_OK) {
        return err;
    }

    return mcpwm_generator_set_action_on_compare_event(
        generator,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_LOW));
}

esp_err_t applyComplementaryDeadtime(McpwmSlot &slot, uint32_t deadtime_ticks)
{
    if (!slot.complementary || slot.generator_high == nullptr || slot.generator_low == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    mcpwm_dead_time_config_t high_config = {};
    high_config.posedge_delay_ticks = deadtime_ticks;
    esp_err_t err = mcpwm_generator_set_dead_time(slot.generator_high, slot.generator_high, &high_config);
    if (err != ESP_OK) {
        return err;
    }

    mcpwm_dead_time_config_t low_config = {};
    low_config.negedge_delay_ticks = deadtime_ticks;
    low_config.flags.invert_output = true;
    err = mcpwm_generator_set_dead_time(slot.generator_low, slot.generator_low, &low_config);
    if (err != ESP_OK) {
        return err;
    }

    slot.deadtime_ticks = deadtime_ticks;
    return ESP_OK;
}

esp_err_t deleteSlot(McpwmSlot &slot)
{
    if (slot.timer != nullptr) {
        mcpwm_timer_start_stop(slot.timer, MCPWM_TIMER_STOP_EMPTY);
        mcpwm_timer_disable(slot.timer);
    }
    if (slot.generator_low != nullptr) {
        mcpwm_del_generator(slot.generator_low);
    }
    if (slot.generator_high != nullptr) {
        mcpwm_del_generator(slot.generator_high);
    }
    if (slot.comparator != nullptr) {
        mcpwm_del_comparator(slot.comparator);
    }
    if (slot.oper != nullptr) {
        mcpwm_del_operator(slot.oper);
    }
    if (slot.timer != nullptr) {
        mcpwm_del_timer(slot.timer);
    }

    slot = {};
    return ESP_OK;
}

} // namespace

namespace esp32libfun {

esp_err_t Mcpwm::ensureSyncPrimitives(void)
{
    portENTER_CRITICAL(&s_mcpwm_sync_lock);
    if (s_mcpwm_mutex == nullptr) {
        s_mcpwm_mutex = xSemaphoreCreateMutexStatic(&s_mcpwm_mutex_storage);
    }
    portEXIT_CRITICAL(&s_mcpwm_sync_lock);

    return (s_mcpwm_mutex != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

int Mcpwm::findSlotByPin(int pin)
{
    for (size_t i = 0; i < MAX_CHANNELS; ++i) {
        if (slotOwnsPin(s_mcpwm_slots[i], pin)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Mcpwm::findFreeSlot(void)
{
    for (size_t i = 0; i < MAX_CHANNELS; ++i) {
        if (!s_mcpwm_slots[i].used) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

esp_err_t Mcpwm::begin(int pin, uint32_t freq_hz, float duty_percent, int group, uint32_t resolution_hz) const
{
    if (!isValidOutputPin(pin) || freq_hz == 0 || resolution_hz == 0 || group < 0 || group >= SOC_MCPWM_GROUPS) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t period_ticks = periodTicks(resolution_hz, freq_hz);
    if (period_ticks == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mcpwm_mutex);
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

    McpwmSlot slot = {};
    slot.used = true;
    slot.pin_high = pin;
    slot.group = group;
    slot.resolution_hz = resolution_hz;
    slot.freq_hz = freq_hz;
    slot.duty_percent = duty_percent;

    mcpwm_timer_config_t timer_cfg = {};
    timer_cfg.group_id = group;
    timer_cfg.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
    timer_cfg.resolution_hz = resolution_hz;
    timer_cfg.count_mode = MCPWM_TIMER_COUNT_MODE_UP;
    timer_cfg.period_ticks = period_ticks;

    err = mcpwm_new_timer(&timer_cfg, &slot.timer);
    if (err != ESP_OK) {
        return err;
    }

    mcpwm_operator_config_t oper_cfg = {};
    oper_cfg.group_id = group;
    err = mcpwm_new_operator(&oper_cfg, &slot.oper);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = mcpwm_operator_connect_timer(slot.oper, slot.timer);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    mcpwm_comparator_config_t comparator_cfg = {};
    comparator_cfg.flags.update_cmp_on_tez = 1;
    err = mcpwm_new_comparator(slot.oper, &comparator_cfg, &slot.comparator);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    mcpwm_generator_config_t generator_cfg = {};
    generator_cfg.gen_gpio_num = pin;
    err = mcpwm_new_generator(slot.oper, &generator_cfg, &slot.generator_high);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = configureGeneratorWaveform(slot.generator_high, slot.comparator);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = mcpwm_comparator_set_compare_value(slot.comparator, compareTicks(period_ticks, duty_percent));
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = mcpwm_timer_enable(slot.timer);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = mcpwm_timer_start_stop(slot.timer, MCPWM_TIMER_START_NO_STOP);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    s_mcpwm_slots[slot_index] = slot;
    return ESP_OK;
}

esp_err_t Mcpwm::beginComplementary(int pin_high,
                                    int pin_low,
                                    uint32_t freq_hz,
                                    float duty_percent,
                                    int group,
                                    uint32_t resolution_hz) const
{
    if (!isValidOutputPin(pin_high) || !isValidOutputPin(pin_low) || pin_high == pin_low ||
        freq_hz == 0 || resolution_hz == 0 || group < 0 || group >= SOC_MCPWM_GROUPS) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t period_ticks = periodTicks(resolution_hz, freq_hz);
    if (period_ticks == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mcpwm_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (findSlotByPin(pin_high) >= 0 || findSlotByPin(pin_low) >= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const int slot_index = findFreeSlot();
    if (slot_index < 0) {
        return ESP_ERR_NO_MEM;
    }

    McpwmSlot slot = {};
    slot.used = true;
    slot.complementary = true;
    slot.pin_high = pin_high;
    slot.pin_low = pin_low;
    slot.group = group;
    slot.resolution_hz = resolution_hz;
    slot.freq_hz = freq_hz;
    slot.duty_percent = duty_percent;

    mcpwm_timer_config_t timer_cfg = {};
    timer_cfg.group_id = group;
    timer_cfg.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
    timer_cfg.resolution_hz = resolution_hz;
    timer_cfg.count_mode = MCPWM_TIMER_COUNT_MODE_UP;
    timer_cfg.period_ticks = period_ticks;

    err = mcpwm_new_timer(&timer_cfg, &slot.timer);
    if (err != ESP_OK) {
        return err;
    }

    mcpwm_operator_config_t oper_cfg = {};
    oper_cfg.group_id = group;
    err = mcpwm_new_operator(&oper_cfg, &slot.oper);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = mcpwm_operator_connect_timer(slot.oper, slot.timer);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    mcpwm_comparator_config_t comparator_cfg = {};
    comparator_cfg.flags.update_cmp_on_tez = 1;
    err = mcpwm_new_comparator(slot.oper, &comparator_cfg, &slot.comparator);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    mcpwm_generator_config_t generator_cfg = {};
    generator_cfg.gen_gpio_num = pin_high;
    err = mcpwm_new_generator(slot.oper, &generator_cfg, &slot.generator_high);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    generator_cfg.gen_gpio_num = pin_low;
    err = mcpwm_new_generator(slot.oper, &generator_cfg, &slot.generator_low);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = configureGeneratorWaveform(slot.generator_high, slot.comparator);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = configureGeneratorWaveform(slot.generator_low, slot.comparator);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = mcpwm_comparator_set_compare_value(slot.comparator, compareTicks(period_ticks, duty_percent));
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = applyComplementaryDeadtime(slot, 0);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = mcpwm_timer_enable(slot.timer);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    err = mcpwm_timer_start_stop(slot.timer, MCPWM_TIMER_START_NO_STOP);
    if (err != ESP_OK) {
        deleteSlot(slot);
        return err;
    }

    s_mcpwm_slots[slot_index] = slot;
    return ESP_OK;
}

esp_err_t Mcpwm::end(int pin) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mcpwm_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return deleteSlot(s_mcpwm_slots[slot_index]);
}

bool Mcpwm::ready(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return false;
    }

    LockGuard guard(s_mcpwm_mutex);
    if (!guard.locked()) {
        return false;
    }

    return findSlotByPin(pin) >= 0;
}

bool Mcpwm::complementary(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return false;
    }

    LockGuard guard(s_mcpwm_mutex);
    if (!guard.locked()) {
        return false;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return false;
    }

    return s_mcpwm_slots[slot_index].complementary;
}

esp_err_t Mcpwm::duty(int pin, float percent) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mcpwm_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (percent < 0.0f) {
        percent = 0.0f;
    }
    if (percent > 100.0f) {
        percent = 100.0f;
    }

    auto &slot = s_mcpwm_slots[slot_index];
    const uint32_t ticks = compareTicks(periodTicks(slot.resolution_hz, slot.freq_hz), percent);
    err = mcpwm_comparator_set_compare_value(slot.comparator, ticks);
    if (err == ESP_OK) {
        slot.duty_percent = percent;
    }
    return err;
}

float Mcpwm::duty(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return 0.0f;
    }

    LockGuard guard(s_mcpwm_mutex);
    if (!guard.locked()) {
        return 0.0f;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return 0.0f;
    }

    return s_mcpwm_slots[slot_index].duty_percent;
}

esp_err_t Mcpwm::freq(int pin, uint32_t hz) const
{
    if (hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mcpwm_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    auto &slot = s_mcpwm_slots[slot_index];
    const uint32_t ticks = periodTicks(slot.resolution_hz, hz);
    if (ticks == 0 || (slot.complementary && slot.deadtime_ticks >= ticks)) {
        return ESP_ERR_INVALID_ARG;
    }

    err = mcpwm_timer_set_period(slot.timer, ticks);
    if (err != ESP_OK) {
        return err;
    }

    slot.freq_hz = hz;
    return mcpwm_comparator_set_compare_value(slot.comparator, compareTicks(ticks, slot.duty_percent));
}

uint32_t Mcpwm::freq(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return 0;
    }

    LockGuard guard(s_mcpwm_mutex);
    if (!guard.locked()) {
        return 0;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return 0;
    }

    return s_mcpwm_slots[slot_index].freq_hz;
}

esp_err_t Mcpwm::deadtime(int pin, uint32_t deadtime_us) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mcpwm_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    auto &slot = s_mcpwm_slots[slot_index];
    if (!slot.complementary) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t ticks = microsecondsToTicks(deadtime_us, slot.resolution_hz);
    if (ticks >= periodTicks(slot.resolution_hz, slot.freq_hz)) {
        return ESP_ERR_INVALID_ARG;
    }

    return applyComplementaryDeadtime(slot, ticks);
}

uint32_t Mcpwm::deadtime(int pin) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return 0;
    }

    LockGuard guard(s_mcpwm_mutex);
    if (!guard.locked()) {
        return 0;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0 || !s_mcpwm_slots[slot_index].complementary) {
        return 0;
    }

    const auto &slot = s_mcpwm_slots[slot_index];
    return ticksToMicroseconds(slot.deadtime_ticks, slot.resolution_hz);
}

esp_err_t Mcpwm::pulse(int pin, uint32_t high_us, uint32_t period_us) const
{
    if (period_us == 0 || high_us > period_us) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mcpwm_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    auto &slot = s_mcpwm_slots[slot_index];
    if (slot.resolution_hz != 1000000U || (slot.complementary && slot.deadtime_ticks >= period_us)) {
        return ESP_ERR_INVALID_STATE;
    }

    err = mcpwm_timer_set_period(slot.timer, period_us);
    if (err != ESP_OK) {
        return err;
    }

    err = mcpwm_comparator_set_compare_value(slot.comparator, high_us);
    if (err == ESP_OK) {
        slot.freq_hz = 1000000U / period_us;
        slot.duty_percent = (static_cast<float>(high_us) * 100.0f) / static_cast<float>(period_us);
    }
    return err;
}

esp_err_t Mcpwm::force(int pin, int level, bool hold_on) const
{
    if (level < -1 || level > 1) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mcpwm_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int slot_index = findSlotByPin(pin);
    if (slot_index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const auto &slot = s_mcpwm_slots[slot_index];
    mcpwm_gen_handle_t generator = generatorForPin(slot, pin);
    if (generator == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    return mcpwm_generator_set_force_level(generator, translateForceLevel(slot, pin, level), hold_on);
}

Mcpwm mcpwm;

} // namespace esp32libfun
