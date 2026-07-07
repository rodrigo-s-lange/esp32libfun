#pragma once

// esp_example_reference is a worked example, not a core module or a real
// device library. It exists only to show one concrete instantiation of
// esp_component_template. See framework/libs/esp_example_reference/README.md
// before treating it as anything else.

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace esp_example_reference {

class Reference;

using reference_callback_t = void (*)(Reference &instance);

/// Worked example built from esp_component_template. Supports both the
/// manual lifecycle path and the optional managed runtime path.
class Reference {
public:
    /// Default period used by the manual loop and the managed task.
    static constexpr uint32_t DEFAULT_INTERVAL_MS = 1000;
    /// Default stack size, in FreeRTOS stack words, for the managed task.
    static constexpr uint32_t DEFAULT_TASK_STACK_WORDS = 1024;
    /// Conservative default priority for the managed task.
    static constexpr UBaseType_t DEFAULT_TASK_PRIORITY = (tskIDLE_PRIORITY + 1U);
    /// Default core affinity for the managed task.
    static constexpr BaseType_t DEFAULT_TASK_CORE = tskNO_AFFINITY;
    /// Maximum visible name length stored by the component.
    static constexpr size_t MAX_NAME_LEN = 24;

    /// Configures the component in manual mode with a name and default interval.
    ///
    /// @param name Human-readable name used by logs and the managed task.
    /// @param interval_ms Default execution period in milliseconds.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t init(const char *name = "reference", uint32_t interval_ms = DEFAULT_INTERVAL_MS);
    /// Starts the optional background task with the given interval, priority, and core affinity.
    ///
    /// @param interval_ms Execution period in milliseconds for the task loop.
    /// @param priority FreeRTOS priority used by the managed task.
    /// @param core Core affinity passed to `xTaskCreateStaticPinnedToCore()`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t start(uint32_t interval_ms = DEFAULT_INTERVAL_MS,
                    UBaseType_t priority = DEFAULT_TASK_PRIORITY,
                    BaseType_t core = DEFAULT_TASK_CORE);
    /// Stops the optional background task and keeps manual mode available.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t stop(void);
    /// Stops the task, clears state, and returns the instance to the uninitialized state.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t end(void);

    /// Returns true when the instance has been configured with `init()`.
    ///
    /// @return true when `init()` has already succeeded.
    [[nodiscard]] bool ready(void) const;
    /// Returns true when the optional managed task is active.
    ///
    /// @return true when `start()` is currently active.
    [[nodiscard]] bool started(void) const;
    /// Executes one manual-mode processing step. Fails if the managed task is running.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t loop(void);

    /// Updates the default interval used by `loop()` and `start()`.
    ///
    /// @param interval_ms New execution period in milliseconds.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t intervalMs(uint32_t interval_ms);
    /// Registers one optional callback fired by `loop()` or the managed task, or clears it with `nullptr`.
    ///
    /// @param callback Callback pointer, or `nullptr` to clear the callback.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t onTick(reference_callback_t callback);

    /// Returns the configured component name.
    ///
    /// @return Configured name, or an empty string when not configured.
    [[nodiscard]] const char *name(void) const;
    /// Returns the current execution interval in milliseconds.
    ///
    /// @return Current interval in milliseconds.
    [[nodiscard]] uint32_t intervalMs(void) const;
    /// Returns the number of times the component step has executed.
    ///
    /// @return Number of executed steps since the last `init()`/`end()`.
    [[nodiscard]] uint32_t counter(void) const;

private:
    static void taskEntry(void *arg);

    esp_err_t ensureMutex(void);
    bool lock(void) const;
    void unlock(void) const;
    esp_err_t setName(const char *name);
    void resetState(void);
    void step(void);

    bool configured_ = false;
    bool started_ = false;
    uint32_t interval_ms_ = DEFAULT_INTERVAL_MS;
    uint32_t counter_ = 0;
    char name_[MAX_NAME_LEN + 1] = {};
    reference_callback_t callback_ = nullptr;

    SemaphoreHandle_t mutex_ = nullptr;
    StaticSemaphore_t mutex_storage_ {};
    TaskHandle_t task_handle_ = nullptr;
    StaticTask_t task_storage_ {};
    StackType_t task_stack_[DEFAULT_TASK_STACK_WORDS] = {};
};

/// Global convenience instance for this example only.
extern Reference reference;

} // namespace esp_example_reference

using esp_example_reference::Reference;
using esp_example_reference::reference;
using esp_example_reference::reference_callback_t;
