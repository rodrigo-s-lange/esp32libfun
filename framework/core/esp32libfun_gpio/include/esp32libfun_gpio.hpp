#pragma once

#include "esp_err.h"

#define ESP32LIBFUN_GPIO_VERSION "v0.1.2"
#define ESP32LIBFUN_GPIO_VERSION_MAJOR 0
#define ESP32LIBFUN_GPIO_VERSION_MINOR 1
#define ESP32LIBFUN_GPIO_VERSION_PATCH 2

namespace esp32libfun {

constexpr int INPUT                  = 0;
constexpr int INPUT_PULLUP           = 1;
constexpr int INPUT_PULLDOWN         = 2;
constexpr int OUTPUT                 = 3;
constexpr int INPUT_OUTPUT           = 4;
constexpr int INPUT_OUTPUT_OPENDRAIN = 5;
constexpr int OUTPUT_OPENDRAIN       = 6;

constexpr int GPIO_IRQ_DISABLE    = 0;
constexpr int GPIO_IRQ_RISING     = 1;
constexpr int GPIO_IRQ_FALLING    = 2;
constexpr int GPIO_IRQ_CHANGE     = 3;
constexpr int GPIO_IRQ_LOW_LEVEL  = 4;
constexpr int GPIO_IRQ_HIGH_LEVEL = 5;

/// Callback fired in task context by `Gpio::irq()`.
///
/// @param pin Pin that triggered the interrupt.
/// @param level Electrical level read right after the interrupt fired.
/// @param user_ctx Opaque pointer passed through from `Gpio::irq()`.
using gpio_callback_t = void (*)(int pin, bool level, void *user_ctx);

class Gpio {
public:
    static constexpr bool HIGH = true;
    static constexpr bool LOW = false;

    /// Configures one GPIO for input, output, or open-drain use.
    ///
    /// @param pin GPIO number to configure.
    /// @param direction One of `INPUT`, `INPUT_PULLUP`, `INPUT_PULLDOWN`,
    ///        `OUTPUT`, `INPUT_OUTPUT`, `INPUT_OUTPUT_OPENDRAIN`, or `OUTPUT_OPENDRAIN`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t cfg(int pin, int direction) const;
    /// Writes one logical level to a configured output-capable GPIO.
    ///
    /// @param pin GPIO to write.
    /// @param level Logical level to drive: `HIGH` or `LOW`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t write(int pin, bool level) const;
    /// Drives one configured output-capable GPIO high.
    ///
    /// @param pin GPIO to drive.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t high(int pin) const;
    /// Drives one configured output-capable GPIO low.
    ///
    /// @param pin GPIO to drive.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t low(int pin) const;
    /// Reads the electrical level currently seen on the pin.
    ///
    /// If the pin was configured as output-only and you need the last logical
    /// value written by the library, prefer state(pin) or configure the pin
    /// as INPUT_OUTPUT.
    ///
    /// @param pin GPIO to read.
    /// @return true when the pin currently reads high.
    [[nodiscard]] bool read(int pin) const;
    /// Returns the last logical level written by the library when tracked.
    ///
    /// Falls back to read(pin) when no output shadow is available.
    ///
    /// @param pin GPIO to check.
    /// @return true when the last known logical level is high.
    [[nodiscard]] bool state(int pin) const;
    /// Toggles one configured output-capable GPIO.
    ///
    /// @param pin GPIO to toggle.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t toggle(int pin) const;
    /// Enables or disables the optional GPIO AT command set.
    ///
    /// When enabled, this registers the GPIO sidecar commands such as
    /// `AT+GPIO=<pin>,<0|1>` and `AT+GPIO?<pin>`.
    ///
    /// @param enable true to register the sidecar commands, false to unregister them.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t at(bool enable = true) const;
    /// Returns true when the GPIO AT command set is registered.
    ///
    /// @return true when the sidecar commands are currently registered.
    [[nodiscard]] bool atEnabled(void) const;
    /// Registers one simple GPIO interrupt callback.
    ///
    /// The callback runs in a small background task owned by the GPIO module,
    /// not directly inside the ISR. This keeps the common usage path simple
    /// and makes it safe to call regular framework APIs from the callback.
    ///
    /// @param pin GPIO to watch.
    /// @param type One of the `GPIO_IRQ_*` trigger types.
    /// @param callback Function invoked in task context when the interrupt fires.
    /// @param user_ctx Opaque pointer passed through to `callback`.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t irq(int pin, int type, gpio_callback_t callback, void *user_ctx = nullptr) const;
    /// Disables and unregisters one GPIO interrupt callback.
    ///
    /// @param pin GPIO whose interrupt callback should be removed.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t irqOff(int pin) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t ensureInterruptRuntime(void);
    static bool isValidPin(int pin);
    static bool usesOutputShadow(int direction);
    static esp_err_t applyConfig(int pin, int direction);
};

/// Global GPIO convenience object.
extern Gpio gpio;

} // namespace esp32libfun

using esp32libfun::gpio;
using esp32libfun::INPUT;
using esp32libfun::INPUT_PULLUP;
using esp32libfun::INPUT_PULLDOWN;
using esp32libfun::OUTPUT;
using esp32libfun::INPUT_OUTPUT;
using esp32libfun::INPUT_OUTPUT_OPENDRAIN;
using esp32libfun::OUTPUT_OPENDRAIN;
using esp32libfun::GPIO_IRQ_DISABLE;
using esp32libfun::GPIO_IRQ_RISING;
using esp32libfun::GPIO_IRQ_FALLING;
using esp32libfun::GPIO_IRQ_CHANGE;
using esp32libfun::GPIO_IRQ_LOW_LEVEL;
using esp32libfun::GPIO_IRQ_HIGH_LEVEL;
using esp32libfun::gpio_callback_t;
