#include "esp32libfun_delay.hpp"

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace esp32libfun {

namespace {

constexpr uint64_t kMsPerSecond = 1000ULL;
constexpr uint64_t kSecondsPerMinute = 60ULL;
constexpr uint64_t kMinutesPerHour = 60ULL;
constexpr uint64_t kUsPerMs = 1000ULL;

} // namespace

uint64_t Delay::uptimeMillis(void)
{
    return static_cast<uint64_t>(esp_timer_get_time()) / kUsPerMs;
}

void Delay::delayTicks(uint64_t ticks)
{
    while (ticks > 0) {
        const TickType_t chunk = (ticks > static_cast<uint64_t>(portMAX_DELAY))
            ? portMAX_DELAY
            : static_cast<TickType_t>(ticks);
        vTaskDelay(chunk);
        ticks -= chunk;
    }
}

void Delay::h(uint64_t hours) const
{
    ms(hours * kMinutesPerHour * kSecondsPerMinute * kMsPerSecond);
}

void Delay::m(uint64_t minutes) const
{
    ms(minutes * kSecondsPerMinute * kMsPerSecond);
}

void Delay::s(uint64_t seconds) const
{
    ms(seconds * kMsPerSecond);
}

void Delay::ms(uint64_t milliseconds) const
{
    if (milliseconds == 0) {
        return;
    }

    const uint64_t whole_ticks = milliseconds / portTICK_PERIOD_MS;
    const uint64_t remainder_ms = milliseconds % portTICK_PERIOD_MS;

    if (whole_ticks > 0) {
        t(whole_ticks);
    }

    if (remainder_ms > 0) {
        us(remainder_ms * kUsPerMs);
    }
}

void Delay::us(uint64_t microseconds) const
{
    while (microseconds > 0) {
        const uint32_t chunk = (microseconds > UINT32_MAX)
            ? UINT32_MAX
            : static_cast<uint32_t>(microseconds);
        esp_rom_delay_us(chunk);
        microseconds -= chunk;
    }
}

void Delay::t(uint64_t ticks) const
{
    delayTicks(ticks);
}

void Delay::millis(uint64_t target_millis) const
{
    const uint64_t now = uptimeMillis();
    if (now >= target_millis) {
        return;
    }

    ms(target_millis - now);
}

Delay delay;

} // namespace esp32libfun
