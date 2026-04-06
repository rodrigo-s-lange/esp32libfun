#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace esp_si7021 {

class Si7021;

using si7021_callback_t = void (*)(Si7021 &instance);

/// Driver for the Si7021 temperature and humidity sensor.
///
/// The library attaches to an I2C bus already started with `i2c.begin()`.
/// After `init()`, the component can run in manual mode through `read()` /
/// `loop()`, or in managed mode through the optional `start()` task.
class Si7021 {
public:
    /// Fixed 7-bit I2C address used by every Si7021 device.
    static constexpr uint16_t DEFAULT_ADDRESS = 0x40;
    /// Default read period used by `loop()` and `start()`.
    static constexpr uint32_t DEFAULT_INTERVAL_MS = 2000;
    /// Stack size in FreeRTOS stack words for the optional managed task.
    static constexpr uint32_t DEFAULT_TASK_STACK_WORDS = 2048;
    /// Conservative default priority for the optional managed task.
    static constexpr UBaseType_t DEFAULT_TASK_PRIORITY = (tskIDLE_PRIORITY + 1U);
    /// Default core affinity for the optional managed task.
    static constexpr BaseType_t DEFAULT_TASK_CORE = tskNO_AFFINITY;

    /// Attaches the sensor to an I2C bus already started with `i2c.begin()`.
    ///
    /// @param address Fixed 7-bit I2C address. Must be `DEFAULT_ADDRESS`.
    /// @param port I2C port index matching the previous `i2c.begin()` call.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t init(uint16_t address = DEFAULT_ADDRESS, int port = 0);

    /// Starts the optional background read task.
    ///
    /// @param interval_ms Read period in milliseconds.
    /// @param priority FreeRTOS priority for the managed task.
    /// @param core Core affinity for the managed task.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t start(uint32_t interval_ms = DEFAULT_INTERVAL_MS,
                    UBaseType_t priority = DEFAULT_TASK_PRIORITY,
                    BaseType_t core = DEFAULT_TASK_CORE);

    /// Stops the managed task and keeps the driver ready for manual use.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t stop(void);

    /// Stops the task, removes the registered I2C device, and clears the state.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(void);

    /// Returns true when `init()` has completed successfully.
    [[nodiscard]] bool ready(void) const;
    /// Returns true when the optional managed task is active.
    [[nodiscard]] bool started(void) const;

    /// Executes one manual acquisition step.
    ///
    /// Fails when the managed task is already active.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t loop(void);

    /// Reads humidity first, then temperature from the last humidity conversion.
    ///
    /// Updates `humidity()` and `temperature()` on success.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t read(void);

    /// Sends a soft reset and waits for the sensor to become ready again.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t reset(void);

    /// Registers a callback fired after each successful acquisition.
    ///
    /// @param callback Callback pointer, or `nullptr` to clear it.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t onRead(si7021_callback_t callback);

    /// Updates the default read period used by `loop()` and `start()`.
    ///
    /// @param interval_ms New read period in milliseconds.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t intervalMs(uint32_t interval_ms);

    /// Returns the last temperature in degrees Celsius.
    [[nodiscard]] float temperature(void) const;
    /// Returns the last relative humidity in percent.
    [[nodiscard]] float humidity(void) const;
    /// Returns the configured 7-bit I2C address.
    [[nodiscard]] uint16_t address(void) const;
    /// Returns the configured I2C port index.
    [[nodiscard]] int port(void) const;
    /// Returns the current read period in milliseconds.
    [[nodiscard]] uint32_t intervalMs(void) const;

private:
    static void taskEntry(void *arg);
    static bool isValidAddress(uint16_t address);

    esp_err_t ensureMutex(void);
    bool lock(void) const;
    void unlock(void) const;
    void step(void);
    esp_err_t measureRH(float *humidity_pct);
    esp_err_t readTempFromLastRH(float *temperature_c);
    void resetState(void);

    bool configured_ = false;
    bool started_ = false;
    uint32_t interval_ms_ = DEFAULT_INTERVAL_MS;
    uint16_t address_ = DEFAULT_ADDRESS;
    int port_ = 0;
    float temperature_ = 0.0f;
    float humidity_ = 0.0f;
    si7021_callback_t callback_ = nullptr;

    SemaphoreHandle_t mutex_ = nullptr;
    StaticSemaphore_t mutex_storage_ {};
    TaskHandle_t task_handle_ = nullptr;
    StaticTask_t task_storage_ {};
    StackType_t task_stack_[DEFAULT_TASK_STACK_WORDS] = {};
};

extern Si7021 si7021;

} // namespace esp_si7021

using esp_si7021::Si7021;
using esp_si7021::si7021;
using esp_si7021::si7021_callback_t;
