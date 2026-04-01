#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace esp_si7021 {

class Si7021;

using si7021_callback_t = void (*)(Si7021 &instance);

/// Driver for the Si7021 temperature and humidity sensor (I2C address 0x40).
///
/// Supports both the manual lifecycle path (init / loop) and the optional
/// managed task path (init / start), following the esp_* library convention.
///
/// Humidity and temperature are acquired together on each step: a no-hold
/// humidity measurement is triggered first, and the temperature is read from
/// the Si7021's internal result register after the measurement completes.
class Si7021 {
public:
    /// Fixed I2C address of the Si7021.
    static constexpr uint16_t DEFAULT_ADDRESS = 0x40;
    /// Default read period used by loop() and start().
    static constexpr uint32_t DEFAULT_INTERVAL_MS = 2000;
    /// Stack size in FreeRTOS words for the managed task.
    static constexpr uint32_t DEFAULT_TASK_STACK_WORDS = 2048;
    /// Conservative default priority for the managed task.
    static constexpr UBaseType_t DEFAULT_TASK_PRIORITY = (tskIDLE_PRIORITY + 1U);
    /// Default core affinity for the managed task.
    static constexpr BaseType_t DEFAULT_TASK_CORE = tskNO_AFFINITY;

    /// Probes and registers the Si7021 on a bus already started with i2c.begin().
    ///
    /// The caller is responsible for calling i2c.begin() before init().
    /// This keeps bus ownership at application level and allows multiple
    /// devices to share the same port without conflict.
    ///
    /// @param address 7-bit I2C address (always 0x40 on Si7021).
    /// @param port    I2C port number matching the i2c.begin() call.
    /// @return ESP_OK on success, or an esp_err_t describing the failure.
    esp_err_t init(uint16_t address = DEFAULT_ADDRESS, int port = 0);

    /// Starts the optional background task.
    ///
    /// @param interval_ms Read period in milliseconds.
    /// @param priority    FreeRTOS priority for the managed task.
    /// @param core        Core affinity.
    /// @return ESP_OK on success, or an esp_err_t describing the failure.
    esp_err_t start(uint32_t interval_ms = DEFAULT_INTERVAL_MS,
                    UBaseType_t priority = DEFAULT_TASK_PRIORITY,
                    BaseType_t core = DEFAULT_TASK_CORE);

    /// Stops the managed task and keeps the driver in the ready state.
    ///
    /// @return ESP_OK on success, or an esp_err_t describing the failure.
    esp_err_t stop(void);

    /// Stops the task, releases the registered I2C device, and resets all state.
    ///
    /// @return ESP_OK on success, or an esp_err_t describing the failure.
    esp_err_t end(void);

    /// Returns true when init() has completed successfully.
    [[nodiscard]] bool ready(void) const;
    /// Returns true when the managed task is active.
    [[nodiscard]] bool started(void) const;

    /// Executes one manual read step. Fails when the managed task is running.
    ///
    /// @return ESP_OK on success, or an esp_err_t describing the failure.
    esp_err_t loop(void);

    /// Triggers a humidity measurement, waits for completion, then reads the
    /// temperature from the Si7021 result register. Updates temperature() and
    /// humidity() on success.
    ///
    /// @return ESP_OK on success, or an esp_err_t describing the failure.
    esp_err_t read(void);

    /// Sends a soft reset to the Si7021. The caller must wait at least 15 ms
    /// before issuing the next command.
    ///
    /// @return ESP_OK on success, or an esp_err_t describing the failure.
    esp_err_t reset(void);

    /// Registers a callback fired after each successful read.
    ///
    /// @param callback Callback pointer, or nullptr to clear.
    /// @return ESP_OK on success, or an esp_err_t describing the failure.
    esp_err_t onRead(si7021_callback_t callback);

    /// Returns the temperature in degrees Celsius from the last successful read.
    [[nodiscard]] float temperature(void) const;
    /// Returns the relative humidity in percent from the last successful read.
    [[nodiscard]] float humidity(void) const;
    /// Returns the configured 7-bit I2C address.
    [[nodiscard]] uint16_t address(void) const;

private:
    static void taskEntry(void *arg);

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
