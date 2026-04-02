#include "esp32libfun_spi.hpp"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"

#include "esp_log.h"

namespace esp32libfun {

namespace {

static const char *TAG = "ESP32LIBFUN_SPI";

#if SOC_SPI_PERIPH_NUM > 2
constexpr size_t kMaxBuses = 2;
#else
constexpr size_t kMaxBuses = 1;
#endif

struct BusState {
    bool active = false;
    int sclk_pin = -1;
    int mosi_pin = -1;
    int miso_pin = -1;
    size_t max_transfer_sz = 0;
    uint32_t ref_count = 0;
};

struct DeviceState {
    bool used = false;
    int port = -1;
    int cs_pin = -1;
    uint32_t clock_hz = 0;
    int mode = SPI_MODE_0;
    size_t queue_size = 1;
    uint32_t flags = 0;
    uint32_t ref_count = 0;
    spi_device_handle_t handle = nullptr;
};

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

BusState s_buses[kMaxBuses] = {};
DeviceState s_devices[Spi::MAX_DEVICES] = {};

StaticSemaphore_t s_spi_mutex_storage = {};
SemaphoreHandle_t s_spi_mutex = nullptr;
portMUX_TYPE s_spi_sync_lock = portMUX_INITIALIZER_UNLOCKED;

bool isValidBusPin(int pin)
{
    return GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(pin));
}

bool isValidMisoPin(int pin)
{
    return pin < 0 || GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(pin));
}

bool isValidCsPin(int pin)
{
    return GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(pin));
}

esp_err_t transmitLocked(spi_device_handle_t handle,
                         const uint8_t *tx_data,
                         uint8_t *rx_data,
                         size_t len,
                         uint32_t flags = 0)
{
    if (handle == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t t = {};
    t.length = len * 8;
    t.rxlength = (rx_data != nullptr) ? (len * 8) : 0;
    t.tx_buffer = tx_data;
    t.rx_buffer = rx_data;
    t.flags = flags;
    return spi_device_polling_transmit(handle, &t);
}

} // namespace

esp_err_t Spi::ensureSyncPrimitives(void)
{
    portENTER_CRITICAL(&s_spi_sync_lock);
    if (s_spi_mutex == nullptr) {
        s_spi_mutex = xSemaphoreCreateMutexStatic(&s_spi_mutex_storage);
    }
    portEXIT_CRITICAL(&s_spi_sync_lock);

    return (s_spi_mutex != nullptr) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool Spi::isValidHost(int port)
{
    switch (port) {
        case SPI2_HOST:
#if SOC_SPI_PERIPH_NUM > 2
        case SPI3_HOST:
#endif
            return true;
        default:
            return false;
    }
}

int Spi::hostIndex(int port)
{
    switch (port) {
        case SPI2_HOST:
            return 0;
#if SOC_SPI_PERIPH_NUM > 2
        case SPI3_HOST:
            return 1;
#endif
        default:
            return -1;
    }
}

int Spi::findDeviceIndex(int cs_pin, int port)
{
    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        if (s_devices[i].used &&
            s_devices[i].port == port &&
            s_devices[i].cs_pin == cs_pin) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

esp_err_t Spi::begin(int sclk_pin, int mosi_pin, int miso_pin, int port, size_t max_transfer_sz) const
{
    if (!isValidHost(port) ||
        !isValidBusPin(sclk_pin) ||
        !isValidBusPin(mosi_pin) ||
        !isValidMisoPin(miso_pin) ||
        max_transfer_sz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int bus_index = hostIndex(port);
    if (bus_index < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_spi_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    BusState &bus = s_buses[bus_index];
    if (bus.active) {
        const bool same_config =
            (bus.sclk_pin == sclk_pin) &&
            (bus.mosi_pin == mosi_pin) &&
            (bus.miso_pin == miso_pin) &&
            (bus.max_transfer_sz == max_transfer_sz);
        if (!same_config) {
            return ESP_ERR_INVALID_STATE;
        }

        ++bus.ref_count;
        ESP_LOGD(TAG, "bus %d ref_count=%lu", port, static_cast<unsigned long>(bus.ref_count));
        return ESP_OK;
    }

    spi_bus_config_t cfg = {};
    cfg.sclk_io_num = sclk_pin;
    cfg.mosi_io_num = mosi_pin;
    cfg.miso_io_num = miso_pin;
    cfg.quadwp_io_num = -1;
    cfg.quadhd_io_num = -1;
    cfg.data4_io_num = -1;
    cfg.data5_io_num = -1;
    cfg.data6_io_num = -1;
    cfg.data7_io_num = -1;
    cfg.max_transfer_sz = static_cast<int>(max_transfer_sz);

    err = spi_bus_initialize(static_cast<spi_host_device_t>(port), &cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize(port=%d) failed: %s", port, esp_err_to_name(err));
        return err;
    }

    bus.active = true;
    bus.sclk_pin = sclk_pin;
    bus.mosi_pin = mosi_pin;
    bus.miso_pin = miso_pin;
    bus.max_transfer_sz = max_transfer_sz;
    bus.ref_count = 1;
    ESP_LOGI(TAG, "bus %d initialized on SCLK=%d MOSI=%d MISO=%d max=%lu",
             port,
             sclk_pin,
             mosi_pin,
             miso_pin,
             static_cast<unsigned long>(max_transfer_sz));
    return ESP_OK;
}

esp_err_t Spi::end(int port) const
{
    if (!isValidHost(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    const int bus_index = hostIndex(port);
    if (bus_index < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_spi_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    BusState &bus = s_buses[bus_index];
    if (!bus.active) {
        return ESP_OK;
    }

    if (bus.ref_count > 1) {
        --bus.ref_count;
        ESP_LOGD(TAG, "bus %d ref_count=%lu", port, static_cast<unsigned long>(bus.ref_count));
        return ESP_OK;
    }

    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        if (!s_devices[i].used || s_devices[i].port != port) {
            continue;
        }

        ESP_LOGE(TAG, "bus %d still has device CS=%d registered", port, s_devices[i].cs_pin);
        return ESP_ERR_INVALID_STATE;
    }

    err = spi_bus_free(static_cast<spi_host_device_t>(port));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_free(port=%d) failed: %s", port, esp_err_to_name(err));
        return err;
    }

    bus = {};
    ESP_LOGI(TAG, "bus %d deinitialized", port);
    return ESP_OK;
}

bool Spi::ready(int port) const
{
    if (!isValidHost(port) || s_spi_mutex == nullptr) {
        return false;
    }

    const int bus_index = hostIndex(port);
    if (bus_index < 0) {
        return false;
    }

    LockGuard guard(s_spi_mutex);
    if (!guard.locked()) {
        return false;
    }

    return s_buses[bus_index].active;
}

bool Spi::has(int cs_pin, int port) const
{
    if (!isValidHost(port) || s_spi_mutex == nullptr) {
        return false;
    }

    LockGuard guard(s_spi_mutex);
    if (!guard.locked()) {
        return false;
    }

    return findDeviceIndex(cs_pin, port) >= 0;
}

esp_err_t Spi::add(int cs_pin, uint32_t clock_hz, int mode, int port, size_t queue_size, uint32_t flags) const
{
    if (!isValidHost(port) ||
        !isValidCsPin(cs_pin) ||
        clock_hz == 0 ||
        mode < SPI_MODE_0 ||
        mode > SPI_MODE_3 ||
        queue_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int bus_index = hostIndex(port);
    if (bus_index < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_spi_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_buses[bus_index].active) {
        return ESP_ERR_INVALID_STATE;
    }

    const int existing_index = findDeviceIndex(cs_pin, port);
    if (existing_index >= 0) {
        DeviceState &device = s_devices[existing_index];
        const bool same_config =
            (device.clock_hz == clock_hz) &&
            (device.mode == mode) &&
            (device.queue_size == queue_size) &&
            (device.flags == flags);
        if (!same_config) {
            return ESP_ERR_INVALID_STATE;
        }

        ++device.ref_count;
        ESP_LOGD(TAG, "device CS=%d on bus %d ref_count=%lu", cs_pin, port, static_cast<unsigned long>(device.ref_count));
        return ESP_OK;
    }

    size_t free_index = MAX_DEVICES;
    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        if (!s_devices[i].used) {
            free_index = i;
            break;
        }
    }
    if (free_index >= MAX_DEVICES) {
        return ESP_ERR_NO_MEM;
    }

    spi_device_interface_config_t cfg = {};
    cfg.clock_speed_hz = static_cast<int>(clock_hz);
    cfg.mode = mode;
    cfg.spics_io_num = cs_pin;
    cfg.queue_size = static_cast<int>(queue_size);
    cfg.flags = flags;

    spi_device_handle_t handle = nullptr;
    err = spi_bus_add_device(static_cast<spi_host_device_t>(port), &cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to add device CS=%d on bus %d: %s", cs_pin, port, esp_err_to_name(err));
        return err;
    }

    DeviceState &device = s_devices[free_index];
    device.used = true;
    device.port = port;
    device.cs_pin = cs_pin;
    device.clock_hz = clock_hz;
    device.mode = mode;
    device.queue_size = queue_size;
    device.flags = flags;
    device.ref_count = 1;
    device.handle = handle;
    return ESP_OK;
}

esp_err_t Spi::remove(int cs_pin, int port) const
{
    if (!isValidHost(port)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_spi_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int index = findDeviceIndex(cs_pin, port);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (s_devices[index].ref_count > 1) {
        --s_devices[index].ref_count;
        ESP_LOGD(TAG, "device CS=%d on bus %d ref_count=%lu", cs_pin, port, static_cast<unsigned long>(s_devices[index].ref_count));
        return ESP_OK;
    }

    err = spi_bus_remove_device(s_devices[index].handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to remove device CS=%d from bus %d: %s", cs_pin, port, esp_err_to_name(err));
        return err;
    }

    s_devices[index] = {};
    return ESP_OK;
}

esp_err_t Spi::transfer(int cs_pin, const uint8_t *tx_data, uint8_t *rx_data, size_t len, int port) const
{
    if (!isValidHost(port) || len == 0 || (tx_data == nullptr && rx_data == nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_spi_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int index = findDeviceIndex(cs_pin, port);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    return transmitLocked(s_devices[index].handle, tx_data, rx_data, len);
}

esp_err_t Spi::write(int cs_pin, const uint8_t *data, size_t len, int port) const
{
    if (data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return transfer(cs_pin, data, nullptr, len, port);
}

esp_err_t Spi::read(int cs_pin, uint8_t *data, size_t len, int port) const
{
    if (data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return transfer(cs_pin, nullptr, data, len, port);
}

esp_err_t Spi::cmd(int cs_pin, uint8_t value, int port) const
{
    return write(cs_pin, &value, 1, port);
}

esp_err_t Spi::regWrite(int cs_pin, uint8_t reg, const uint8_t *data, size_t len, int port) const
{
    if (!isValidHost(port) || (len > 0 && data == nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_spi_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int index = findDeviceIndex(cs_pin, port);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    spi_device_handle_t handle = s_devices[index].handle;
    if (len == 0) {
        return transmitLocked(handle, &reg, nullptr, 1);
    }

    err = spi_device_acquire_bus(handle, portMAX_DELAY);
    if (err != ESP_OK) {
        return err;
    }

    err = transmitLocked(handle, &reg, nullptr, 1, SPI_TRANS_CS_KEEP_ACTIVE);
    if (err == ESP_OK) {
        err = transmitLocked(handle, data, nullptr, len);
    }

    spi_device_release_bus(handle);
    return err;
}

esp_err_t Spi::regWrite8(int cs_pin, uint8_t reg, uint8_t value, int port) const
{
    return regWrite(cs_pin, reg, &value, 1, port);
}

esp_err_t Spi::regRead(int cs_pin, uint8_t reg, uint8_t *data, size_t len, int port) const
{
    if (!isValidHost(port) || data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_spi_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    const int index = findDeviceIndex(cs_pin, port);
    if (index < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    spi_device_handle_t handle = s_devices[index].handle;
    err = spi_device_acquire_bus(handle, portMAX_DELAY);
    if (err != ESP_OK) {
        return err;
    }

    err = transmitLocked(handle, &reg, nullptr, 1, SPI_TRANS_CS_KEEP_ACTIVE);
    if (err == ESP_OK) {
        err = transmitLocked(handle, nullptr, data, len);
    }

    spi_device_release_bus(handle);
    return err;
}

esp_err_t Spi::regRead8(int cs_pin, uint8_t reg, uint8_t *value, int port) const
{
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    return regRead(cs_pin, reg, value, 1, port);
}

Spi spi;

} // namespace esp32libfun
