#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace esp_bmp280 {

class Bmp280;

using bmp280_callback_t = void (*)(Bmp280 &instance);

/// Driver for the BMP280 temperature and pressure sensor.
///
/// The library attaches to an I2C bus already started with `i2c.begin()`.
/// After `init()`, the component can run in manual mode through `read()` /
/// `loop()`, or in managed mode through the optional `start()` task.
class Bmp280 {
public:
    /// Default I2C address when SDO is pulled low.
    static constexpr uint16_t DEFAULT_ADDRESS = 0x76;
    /// Alternate I2C address when SDO is pulled high.
    static constexpr uint16_t ALTERNATE_ADDRESS = 0x77;
    /// Expected chip ID value read from register `0xD0`.
    static constexpr uint8_t CHIP_ID = 0x58;
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
    /// @param address 7-bit I2C address. Must be `DEFAULT_ADDRESS` or `ALTERNATE_ADDRESS`.
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

    /// Triggers one forced-mode conversion and updates temperature and pressure.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t read(void);

    /// Registers a callback fired after each successful acquisition.
    ///
    /// @param callback Callback pointer, or `nullptr` to clear it.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t onRead(bmp280_callback_t callback);

    /// Updates the default read period used by `loop()` and `start()`.
    ///
    /// @param interval_ms New read period in milliseconds.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t intervalMs(uint32_t interval_ms);

    /// Returns the last temperature in degrees Celsius.
    [[nodiscard]] float temperature(void) const;
    /// Returns the last pressure in hectopascals.
    [[nodiscard]] float pressure(void) const;
    /// Returns the configured 7-bit I2C address.
    [[nodiscard]] uint16_t address(void) const;
    /// Returns the configured I2C port index.
    [[nodiscard]] int port(void) const;
    /// Returns the current read period in milliseconds.
    [[nodiscard]] uint32_t intervalMs(void) const;

private:
    struct Calib {
        uint16_t T1;
        int16_t T2;
        int16_t T3;
        uint16_t P1;
        int16_t P2;
        int16_t P3;
        int16_t P4;
        int16_t P5;
        int16_t P6;
        int16_t P7;
        int16_t P8;
        int16_t P9;
    };

    static void taskEntry(void *arg);
    static bool isValidAddress(uint16_t address);
    static float compensateTemperature(int32_t adc_t, const Calib &calib, int32_t *t_fine);
    static float compensatePressure(int32_t adc_p, const Calib &calib, int32_t t_fine);

    esp_err_t ensureMutex(void);
    bool lock(void) const;
    void unlock(void) const;
    void step(void);
    esp_err_t readCalib(void);
    void resetState(void);

    bool configured_ = false;
    bool started_ = false;
    uint32_t interval_ms_ = DEFAULT_INTERVAL_MS;
    uint16_t address_ = DEFAULT_ADDRESS;
    int port_ = 0;
    float temperature_ = 0.0f;
    float pressure_ = 0.0f;
    Calib calib_ = {};
    bmp280_callback_t callback_ = nullptr;

    SemaphoreHandle_t mutex_ = nullptr;
    StaticSemaphore_t mutex_storage_ {};
    TaskHandle_t task_handle_ = nullptr;
    StaticTask_t task_storage_ {};
    StackType_t task_stack_[DEFAULT_TASK_STACK_WORDS] = {};
};

extern Bmp280 bmp280;

} // namespace esp_bmp280

using esp_bmp280::Bmp280;
using esp_bmp280::bmp280;
using esp_bmp280::bmp280_callback_t;
