#pragma once

/// Inspired by Button2 by Lennart Hennigs.
/// Original project: https://github.com/LennartHennigs/Button2
/// This component adapts that idea to the esp32libfun library model.

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace esp_button {

/// Events emitted by Button::loop() or the optional background task.
enum ButtonEvent : int {
    /// No event available.
    BUTTON_EVENT_NONE = 0,
    /// Stable level changed after debounce.
    BUTTON_EVENT_CHANGED,
    /// Button transitioned to pressed state.
    BUTTON_EVENT_PRESSED,
    /// Button transitioned to released state.
    BUTTON_EVENT_RELEASED,
    /// Short press-and-release detected.
    BUTTON_EVENT_TAP,
    /// Single click confirmed after the double-click window.
    BUTTON_EVENT_CLICK,
    /// Two taps confirmed inside the click window.
    BUTTON_EVENT_DOUBLE_CLICK,
    /// Three taps confirmed inside the click window.
    BUTTON_EVENT_TRIPLE_CLICK,
    /// Long-click threshold crossed while the button is still held.
    BUTTON_EVENT_LONG_DETECTED,
    /// Button released after a long hold.
    BUTTON_EVENT_LONG_CLICK,
};

/// Input modes supported by Button::init().
enum ButtonInputMode : int {
    /// Input without internal pull resistor.
    BUTTON_INPUT = 0,
    /// Input with internal pull-up resistor.
    BUTTON_INPUT_PULLUP,
    /// Input with internal pull-down resistor.
    BUTTON_INPUT_PULLDOWN,
    /// Floating input. Alias for BUTTON_INPUT.
    BUTTON_INPUT_FLOATING = BUTTON_INPUT,
};

class Button;

using button_callback_t = void (*)(Button &button);

/// Single-button helper with debouncing, click detection, and per-event callbacks.
///
/// The library works in manual mode after init(). start() is optional and
/// enables a conservative background polling task for convenience.
class Button {
public:
    static constexpr uint32_t DEFAULT_DEBOUNCE_MS = 50;
    static constexpr uint32_t DEFAULT_LONG_MS = 400;
    static constexpr uint32_t DEFAULT_DOUBLE_MS = 300;
    static constexpr uint32_t DEFAULT_POLL_MS = 5;
    static constexpr uint32_t DEFAULT_TASK_STACK_WORDS = 1024;
    static constexpr UBaseType_t DEFAULT_TASK_PRIORITY = (tskIDLE_PRIORITY + 1U);
    static constexpr BaseType_t DEFAULT_TASK_CORE = tskNO_AFFINITY;

    Button() = default;

    /// Configures the GPIO input and prepares manual mode.
    esp_err_t init(int pin, int mode = BUTTON_INPUT_PULLUP, bool active_low = true);
    /// Starts the optional background polling task with conservative defaults.
    esp_err_t start(uint32_t poll_ms = DEFAULT_POLL_MS,
                    UBaseType_t priority = DEFAULT_TASK_PRIORITY,
                    BaseType_t core = DEFAULT_TASK_CORE);
    /// Stops the optional background polling task and keeps manual mode available.
    esp_err_t stop(void);
    /// Stops background polling, clears state, and detaches the button instance.
    esp_err_t end(void);

    [[nodiscard]] bool ready(void) const;
    [[nodiscard]] bool started(void) const;
    /// Advances the state machine once in manual mode and emits callbacks when events occur.
    esp_err_t loop(void);

    /// Sets the debounce window in milliseconds.
    esp_err_t debounceMs(uint32_t ms);
    /// Sets the long-click threshold in milliseconds.
    esp_err_t longClickMs(uint32_t ms);
    /// Sets the multi-click confirmation window in milliseconds.
    esp_err_t doubleClickMs(uint32_t ms);
    /// Enables or disables repeated BUTTON_EVENT_LONG_DETECTED callbacks while held.
    esp_err_t longDetectRetrigger(bool enable);

    /// Registers a callback for a specific event.
    esp_err_t on(ButtonEvent event, button_callback_t callback);
    esp_err_t onChanged(button_callback_t callback);
    esp_err_t onPressed(button_callback_t callback);
    esp_err_t onReleased(button_callback_t callback);
    esp_err_t onTap(button_callback_t callback);
    esp_err_t onClick(button_callback_t callback);
    esp_err_t onDoubleClick(button_callback_t callback);
    esp_err_t onTripleClick(button_callback_t callback);
    esp_err_t onLongDetected(button_callback_t callback);
    esp_err_t onLongClick(button_callback_t callback);

    /// Returns the configured GPIO pin, or -1 if not configured.
    [[nodiscard]] int pin(void) const;
    /// Returns the debounced pressed state.
    [[nodiscard]] bool pressed(void) const;
    /// Returns true when the logical pressed state is active-low.
    [[nodiscard]] bool activeLow(void) const;
    /// Returns how long the button has been held in the current press.
    [[nodiscard]] uint32_t heldMs(void) const;
    /// Returns the duration of the last completed press.
    [[nodiscard]] uint32_t lastPressMs(void) const;
    /// Returns the number of pending taps waiting for click confirmation.
    [[nodiscard]] uint8_t clickCount(void) const;
    /// Returns how many long clicks have been completed.
    [[nodiscard]] uint8_t longClickCount(void) const;
    /// Returns the last emitted event.
    [[nodiscard]] ButtonEvent event(void) const;

private:
    static constexpr size_t CALLBACK_COUNT = static_cast<size_t>(BUTTON_EVENT_LONG_CLICK) + 1U;
    static constexpr size_t MAX_PENDING_EVENTS = 4U;

    static void taskEntry(void *arg);
    static uint32_t nowMs(void);
    static bool isValidMode(int mode);
    static int toGpioMode(int mode);

    esp_err_t ensureMutex(void);
    bool lock(void) const;
    void unlock(void) const;
    void resetRuntimeState(void);
    bool samplePressed(void) const;
    void queueEvent(ButtonEvent event,
                    ButtonEvent *events,
                    button_callback_t *callbacks,
                    size_t capacity,
                    size_t *count);
    void finalizeClicks(ButtonEvent *events,
                        button_callback_t *callbacks,
                        size_t capacity,
                        size_t *count);
    size_t process(ButtonEvent *events, button_callback_t *callbacks, size_t capacity);
    void dispatch(ButtonEvent *events, button_callback_t *callbacks, size_t count);

    int pin_ = -1;
    bool active_low_ = true;
    bool configured_ = false;
    bool started_ = false;
    uint32_t poll_ms_ = DEFAULT_POLL_MS;

    uint32_t debounce_ms_ = DEFAULT_DEBOUNCE_MS;
    uint32_t long_click_ms_ = DEFAULT_LONG_MS;
    uint32_t double_click_ms_ = DEFAULT_DOUBLE_MS;
    bool long_detect_retrigger_ = false;

    bool raw_pressed_ = false;
    bool stable_pressed_ = false;
    bool long_detected_ = false;
    uint8_t click_count_ = 0;
    uint8_t long_click_count_ = 0;
    uint32_t last_change_ms_ = 0;
    uint32_t press_start_ms_ = 0;
    uint32_t last_press_duration_ms_ = 0;
    uint32_t click_deadline_ms_ = 0;
    uint32_t last_long_report_ms_ = 0;
    ButtonEvent last_event_ = BUTTON_EVENT_NONE;
    button_callback_t callbacks_[CALLBACK_COUNT] = {};

    SemaphoreHandle_t mutex_ = nullptr;
    StaticSemaphore_t mutex_storage_ {};
    TaskHandle_t task_handle_ = nullptr;
    StaticTask_t task_storage_ {};
    StackType_t task_stack_[DEFAULT_TASK_STACK_WORDS] = {};
};

extern Button button;

} // namespace esp_button

using esp_button::Button;
using esp_button::button;
using esp_button::button_callback_t;
using esp_button::ButtonEvent;
using esp_button::ButtonInputMode;
using esp_button::BUTTON_EVENT_NONE;
using esp_button::BUTTON_EVENT_CHANGED;
using esp_button::BUTTON_EVENT_PRESSED;
using esp_button::BUTTON_EVENT_RELEASED;
using esp_button::BUTTON_EVENT_TAP;
using esp_button::BUTTON_EVENT_CLICK;
using esp_button::BUTTON_EVENT_DOUBLE_CLICK;
using esp_button::BUTTON_EVENT_TRIPLE_CLICK;
using esp_button::BUTTON_EVENT_LONG_DETECTED;
using esp_button::BUTTON_EVENT_LONG_CLICK;
using esp_button::BUTTON_INPUT;
using esp_button::BUTTON_INPUT_PULLUP;
using esp_button::BUTTON_INPUT_PULLDOWN;
using esp_button::BUTTON_INPUT_FLOATING;
