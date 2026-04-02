#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "esp32libfun_serial.hpp"

namespace {

static const char *TAG = "esp32libfun_serial";

StaticSemaphore_t s_serial_tx_mutex_storage = {};
StaticSemaphore_t s_serial_rx_mutex_storage = {};
StaticSemaphore_t s_serial_state_mutex_storage = {};

SemaphoreHandle_t s_serial_tx_mutex = nullptr;
SemaphoreHandle_t s_serial_rx_mutex = nullptr;
SemaphoreHandle_t s_serial_state_mutex = nullptr;

portMUX_TYPE s_serial_sync_lock = portMUX_INITIALIZER_UNLOCKED;

bool s_usb_serial_jtag_installed = false;
bool s_skip_next_lf = false;

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

const char *serial_backend_name(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    return "USB Serial/JTAG";
#elif CONFIG_ESP_CONSOLE_UART
    return "UART";
#else
    return "unsupported";
#endif
}

#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
bool serial_target_has_native_usb(void)
{
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2
    return true;
#else
    return false;
#endif
}
#endif

esp_err_t serial_write_bytes(const char *data, size_t length)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    return (usb_serial_jtag_write_bytes(data, length, 20 / portTICK_PERIOD_MS) >= 0) ? ESP_OK : ESP_FAIL;
#elif CONFIG_ESP_CONSOLE_UART
    return (uart_write_bytes((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, data, length) >= 0) ? ESP_OK : ESP_FAIL;
#else
    (void)data;
    (void)length;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

int serial_read_byte(char *ch)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    return usb_serial_jtag_read_bytes(ch, 1, 20 / portTICK_PERIOD_MS);
#elif CONFIG_ESP_CONSOLE_UART
    return uart_read_bytes((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, ch, 1, 20 / portTICK_PERIOD_MS);
#else
    (void)ch;
    return -1;
#endif
}

} // namespace

Serial serial;

esp_err_t Serial::ensureSyncPrimitives(void)
{
    portENTER_CRITICAL(&s_serial_sync_lock);
    if (s_serial_tx_mutex == nullptr) {
        s_serial_tx_mutex = xSemaphoreCreateMutexStatic(&s_serial_tx_mutex_storage);
    }
    if (s_serial_rx_mutex == nullptr) {
        s_serial_rx_mutex = xSemaphoreCreateMutexStatic(&s_serial_rx_mutex_storage);
    }
    if (s_serial_state_mutex == nullptr) {
        s_serial_state_mutex = xSemaphoreCreateMutexStatic(&s_serial_state_mutex_storage);
    }
    portEXIT_CRITICAL(&s_serial_sync_lock);

    if (s_serial_tx_mutex == nullptr || s_serial_rx_mutex == nullptr || s_serial_state_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t Serial::init(void)
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard state_guard(s_serial_state_mutex);
    if (!state_guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (initialized_) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    if (!s_usb_serial_jtag_installed) {
        usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        err = usb_serial_jtag_driver_install(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: %s", esp_err_to_name(err));
            return err;
        }
        s_usb_serial_jtag_installed = true;
    }
#elif CONFIG_ESP_CONSOLE_UART
    uart_config_t cfg = {};
    cfg.baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    err = uart_driver_install((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }
    uart_param_config((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, &cfg);
#endif

    initialized_ = true;
    ESP_LOGI(TAG, "initialized using %s backend", serial_backend_name());

#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    if (serial_target_has_native_usb()) {
        ESP_LOGW(TAG, "target has native USB; USB Serial/JTAG is usually the easiest console backend");
    }
#endif

    return ESP_OK;
}

esp_err_t Serial::deinit(void)
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard state_guard(s_serial_state_mutex);
    if (!state_guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!initialized_) {
        return ESP_OK;
    }

    LockGuard tx_guard(s_serial_tx_mutex);
    LockGuard rx_guard(s_serial_rx_mutex);
    if (!tx_guard.locked() || !rx_guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    if (s_usb_serial_jtag_installed) {
        usb_serial_jtag_driver_uninstall();
        s_usb_serial_jtag_installed = false;
    }
#elif CONFIG_ESP_CONSOLE_UART
    uart_driver_delete((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM);
#endif

    s_skip_next_lf = false;
    initialized_ = false;
    ESP_LOGI(TAG, "deinitialized");
    return ESP_OK;
}

bool Serial::isInitialized(void) const
{
    if (s_serial_state_mutex == nullptr) {
        return initialized_;
    }

    LockGuard guard(s_serial_state_mutex);
    if (!guard.locked()) {
        return initialized_;
    }

    return initialized_;
}

int Serial::readByte(char *ch) const
{
    if (ch == nullptr) {
        return -1;
    }

    if (ensureSyncPrimitives() != ESP_OK) {
        return -1;
    }

    LockGuard guard(s_serial_rx_mutex);
    if (!guard.locked()) {
        return -1;
    }

    return serial_read_byte(ch);
}

esp_err_t Serial::readLine(char *buffer, size_t length) const
{
    if (buffer == nullptr || length < 2) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_serial_rx_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    size_t index = 0;

    while (index + 1 < length) {
        char ch = 0;
        int n = serial_read_byte(&ch);
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (s_skip_next_lf) {
            s_skip_next_lf = false;
            if (ch == '\n') {
                continue;
            }
        }

        if (ch == '\r') {
            s_skip_next_lf = true;
            break;
        }

        if (ch == '\n') {
            break;
        }

        buffer[index++] = ch;
    }

    buffer[index] = '\0';
    return ESP_OK;
}

const char *Serial::backend(void) const
{
    return serial_backend_name();
}

void Serial::print(const char *fmt, ...) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return;
    }

    LockGuard guard(s_serial_tx_mutex);
    if (!guard.locked()) {
        return;
    }

    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    serial_write_bytes(buf, strlen(buf));
    static const char reset[] = "\033[0m";
    serial_write_bytes(reset, sizeof(reset) - 1);
}

void Serial::println(const char *fmt, ...) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return;
    }

    LockGuard guard(s_serial_tx_mutex);
    if (!guard.locked()) {
        return;
    }

    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    serial_write_bytes(buf, strlen(buf));
    static const char reset[] = "\033[0m\r\n";
    serial_write_bytes(reset, sizeof(reset) - 1);
}
