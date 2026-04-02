#include "esp32libfun_w5500.hpp"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "lwip/inet.h"

namespace esp32libfun {

namespace {

static const char *TAG = "ESP32LIBFUN_W5500";

constexpr EventBits_t kStartedBit = BIT0;
constexpr EventBits_t kConnectedBit = BIT1;
constexpr size_t kHostnameMaxLen = 32;
constexpr size_t kIpv4StringMaxLen = 16;
constexpr size_t kMacLen = 6;
constexpr uint32_t kLinkCheckPeriodMs = 2000;
constexpr uint32_t kResetAssertMs = 20;
constexpr uint32_t kResetReleaseMs = 200;
constexpr uint32_t kRxTaskStackSize = 4096;

struct W5500State {
    bool initialized = false;
    bool connected = false;
    bool ip_ready = false;
    bool hostname_set = false;
    bool mac_valid = false;
    int miso_pin = -1;
    int mosi_pin = -1;
    int sclk_pin = -1;
    int cs_pin = -1;
    int int_pin = -1;
    int rst_pin = -1;
    int host = W5500::DEFAULT_HOST;
    uint32_t clock_hz = W5500::DEFAULT_CLOCK_HZ;
    size_t queue_size = W5500::DEFAULT_QUEUE_SIZE;
    uint32_t poll_period_ms = W5500::DEFAULT_POLL_PERIOD_MS;
    char hostname[kHostnameMaxLen + 1] = {};
    char local_ip[kIpv4StringMaxLen] = "0.0.0.0";
    char gateway[kIpv4StringMaxLen] = "0.0.0.0";
    char subnet[kIpv4StringMaxLen] = "0.0.0.0";
    uint8_t mac[kMacLen] = {};
};

W5500State s_state = {};

StaticSemaphore_t s_mutex_storage = {};
SemaphoreHandle_t s_mutex = nullptr;
StaticEventGroup_t s_event_group_storage = {};
EventGroupHandle_t s_event_group = nullptr;
portMUX_TYPE s_sync_lock = portMUX_INITIALIZER_UNLOCKED;

esp_eth_handle_t s_eth_handle = nullptr;
esp_eth_mac_t *s_eth_mac = nullptr;
esp_eth_phy_t *s_eth_phy = nullptr;
esp_netif_t *s_eth_netif = nullptr;
esp_eth_netif_glue_handle_t s_eth_glue = nullptr;
esp_event_handler_instance_t s_eth_event_instance = nullptr;
esp_event_handler_instance_t s_ip_event_instance = nullptr;

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

bool isValidInputPin(int pin)
{
    return GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(pin));
}

bool isValidMisoPin(int pin)
{
    return isValidInputPin(pin);
}

bool isValidCleanupHost(int host)
{
    switch (host) {
        case SPI2_HOST:
#if SOC_SPI_PERIPH_NUM > 2
        case SPI3_HOST:
#endif
            return true;
        default:
            return false;
    }
}

esp_err_t normalizeGlobalInitErr(esp_err_t err)
{
    return (err == ESP_ERR_INVALID_STATE) ? ESP_OK : err;
}

esp_err_t normalizeIsrInstallErr(esp_err_t err)
{
    return (err == ESP_ERR_INVALID_STATE) ? ESP_OK : err;
}

void clearIpInfo(void)
{
    strcpy(s_state.local_ip, "0.0.0.0");
    strcpy(s_state.gateway, "0.0.0.0");
    strcpy(s_state.subnet, "0.0.0.0");
    s_state.ip_ready = false;
}

void setConnectedState(bool connected)
{
    s_state.connected = connected;
    if (connected) {
        xEventGroupSetBits(s_event_group, kConnectedBit);
    } else {
        xEventGroupClearBits(s_event_group, kConnectedBit);
        clearIpInfo();
    }
}

void setStartedState(bool started)
{
    if (started) {
        xEventGroupSetBits(s_event_group, kStartedBit);
    } else {
        xEventGroupClearBits(s_event_group, kStartedBit | kConnectedBit);
        s_state.connected = false;
        clearIpInfo();
    }
}

esp_err_t hardwareResetLocked(void)
{
    if (s_state.rst_pin < 0) {
        return ESP_OK;
    }

    gpio_config_t rst_cfg = {};
    rst_cfg.pin_bit_mask = 1ULL << s_state.rst_pin;
    rst_cfg.mode = GPIO_MODE_OUTPUT;
    rst_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    rst_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rst_cfg.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&rst_cfg);
    if (err != ESP_OK) {
        return err;
    }

    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(s_state.rst_pin), 1), TAG, "rst high failed");
    vTaskDelay(pdMS_TO_TICKS(1));
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(s_state.rst_pin), 0), TAG, "rst low failed");
    vTaskDelay(pdMS_TO_TICKS(kResetAssertMs));
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(s_state.rst_pin), 1), TAG, "rst release failed");
    vTaskDelay(pdMS_TO_TICKS(kResetReleaseMs));
    return ESP_OK;
}

esp_err_t deriveLocalMacLocked(void)
{
    uint8_t base_mac[kMacLen] = {};
    esp_err_t err = esp_efuse_mac_get_default(base_mac);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t local_mac[kMacLen] = {};
    err = esp_derive_local_mac(local_mac, base_mac);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, local_mac);
    if (err != ESP_OK) {
        return err;
    }

    memcpy(s_state.mac, local_mac, sizeof(local_mac));
    s_state.mac_valid = true;
    return ESP_OK;
}

void ethEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void) arg;

    if (event_base != ETH_EVENT || event_data == nullptr) {
        return;
    }

    esp_eth_handle_t event_handle = *(esp_eth_handle_t *) event_data;
    if (event_handle != s_eth_handle) {
        return;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return;
    }

    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            setConnectedState(true);
            ESP_LOGI(TAG, "link up");
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            setConnectedState(false);
            ESP_LOGW(TAG, "link down");
            break;
        case ETHERNET_EVENT_START:
            setStartedState(true);
            ESP_LOGI(TAG, "started");
            break;
        case ETHERNET_EVENT_STOP:
            setStartedState(false);
            ESP_LOGI(TAG, "stopped");
            break;
        default:
            break;
    }
}

void gotIpEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void) arg;

    if (event_base != IP_EVENT || event_id != IP_EVENT_ETH_GOT_IP || event_data == nullptr) {
        return;
    }

    ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(event_data);
    if (event->esp_netif != s_eth_netif) {
        return;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return;
    }

    esp_ip4addr_ntoa(&event->ip_info.ip, s_state.local_ip, sizeof(s_state.local_ip));
    esp_ip4addr_ntoa(&event->ip_info.gw, s_state.gateway, sizeof(s_state.gateway));
    esp_ip4addr_ntoa(&event->ip_info.netmask, s_state.subnet, sizeof(s_state.subnet));
    s_state.ip_ready = true;

    ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&event->ip_info.ip));
}

void cleanupLocked(void)
{
    if (s_ip_event_instance != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, s_ip_event_instance);
        s_ip_event_instance = nullptr;
    }

    if (s_eth_event_instance != nullptr) {
        esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, s_eth_event_instance);
        s_eth_event_instance = nullptr;
    }

    if (s_eth_glue != nullptr) {
        esp_eth_del_netif_glue(s_eth_glue);
        s_eth_glue = nullptr;
    }

    if (s_eth_netif != nullptr) {
        esp_netif_destroy(s_eth_netif);
        s_eth_netif = nullptr;
    }

    if (s_eth_handle != nullptr) {
        esp_eth_driver_uninstall(s_eth_handle);
        s_eth_handle = nullptr;
    }

    if (s_eth_phy != nullptr) {
        s_eth_phy->del(s_eth_phy);
        s_eth_phy = nullptr;
    }

    if (s_eth_mac != nullptr) {
        s_eth_mac->del(s_eth_mac);
        s_eth_mac = nullptr;
    }

    if (isValidCleanupHost(s_state.host)) {
        esp_err_t err = spi_bus_free(static_cast<spi_host_device_t>(s_state.host));
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "spi_bus_free(host=%d) failed: %s", s_state.host, esp_err_to_name(err));
        }
    }

    if (s_state.rst_pin >= 0) {
        gpio_reset_pin(static_cast<gpio_num_t>(s_state.rst_pin));
    }

    xEventGroupClearBits(s_event_group, kStartedBit | kConnectedBit);
    s_state = {};
    clearIpInfo();
}

} // namespace

esp_err_t W5500::ensureSyncPrimitives(void)
{
    portENTER_CRITICAL(&s_sync_lock);
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    }
    if (s_event_group == nullptr) {
        s_event_group = xEventGroupCreateStatic(&s_event_group_storage);
    }
    portEXIT_CRITICAL(&s_sync_lock);

    if (s_mutex == nullptr || s_event_group == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

bool W5500::isValidHost(int host)
{
    switch (host) {
        case SPI2_HOST:
#if SOC_SPI_PERIPH_NUM > 2
        case SPI3_HOST:
#endif
            return true;
        default:
            return false;
    }
}

esp_err_t W5500::copyString(char *dst, size_t dst_len, const char *src)
{
    if (dst == nullptr || dst_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (src == nullptr || src[0] == '\0') {
        dst[0] = '\0';
        return ESP_OK;
    }

    const size_t len = strlen(src);
    if (len >= dst_len) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(dst, src, len + 1);
    return ESP_OK;
}

esp_err_t W5500::begin(int miso_pin,
                       int mosi_pin,
                       int sclk_pin,
                       int cs_pin,
                       int int_pin,
                       int rst_pin,
                       int host,
                       uint32_t clock_hz,
                       size_t queue_size,
                       uint32_t poll_period_ms) const
{
    if (!isValidHost(host) ||
        !isValidMisoPin(miso_pin) ||
        !isValidOutputPin(mosi_pin) ||
        !isValidOutputPin(sclk_pin) ||
        !isValidOutputPin(cs_pin) ||
        (int_pin >= 0 && !isValidInputPin(int_pin)) ||
        (rst_pin >= 0 && !isValidOutputPin(rst_pin)) ||
        clock_hz == 0 ||
        queue_size == 0 ||
        (int_pin < 0 && poll_period_ms == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_state.initialized) {
        const bool same_config =
            s_state.miso_pin == miso_pin &&
            s_state.mosi_pin == mosi_pin &&
            s_state.sclk_pin == sclk_pin &&
            s_state.cs_pin == cs_pin &&
            s_state.int_pin == int_pin &&
            s_state.rst_pin == rst_pin &&
            s_state.host == host &&
            s_state.clock_hz == clock_hz &&
            s_state.queue_size == queue_size &&
            s_state.poll_period_ms == poll_period_ms;
        return same_config ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    err = normalizeGlobalInitErr(esp_netif_init());
    if (err != ESP_OK) {
        return err;
    }

    err = normalizeGlobalInitErr(esp_event_loop_create_default());
    if (err != ESP_OK) {
        return err;
    }

    err = normalizeIsrInstallErr(gpio_install_isr_service(0));
    if (err != ESP_OK) {
        return err;
    }

    s_state.miso_pin = miso_pin;
    s_state.mosi_pin = mosi_pin;
    s_state.sclk_pin = sclk_pin;
    s_state.cs_pin = cs_pin;
    s_state.int_pin = int_pin;
    s_state.rst_pin = rst_pin;
    s_state.host = host;
    s_state.clock_hz = clock_hz;
    s_state.queue_size = queue_size;
    s_state.poll_period_ms = poll_period_ms;

    err = hardwareResetLocked();
    if (err != ESP_OK) {
        cleanupLocked();
        return err;
    }

    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num = miso_pin;
    buscfg.mosi_io_num = mosi_pin;
    buscfg.sclk_io_num = sclk_pin;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;

    err = spi_bus_initialize(static_cast<spi_host_device_t>(host), &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        cleanupLocked();
        return err;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.mode = SPI_MODE_0;
    devcfg.clock_speed_hz = static_cast<int>(clock_hz);
    devcfg.spics_io_num = cs_pin;
    devcfg.queue_size = static_cast<int>(queue_size);

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(static_cast<spi_host_device_t>(host), &devcfg);
    w5500_config.int_gpio_num = int_pin;
    w5500_config.poll_period_ms = (int_pin >= 0) ? 0 : poll_period_ms;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.rx_task_stack_size = kRxTaskStackSize;

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = -1;

    s_eth_mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    if (s_eth_mac == nullptr) {
        cleanupLocked();
        return ESP_ERR_NO_MEM;
    }

    s_eth_phy = esp_eth_phy_new_w5500(&phy_config);
    if (s_eth_phy == nullptr) {
        cleanupLocked();
        return ESP_ERR_NO_MEM;
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(s_eth_mac, s_eth_phy);
    eth_config.check_link_period_ms = kLinkCheckPeriodMs;

    err = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (err != ESP_OK) {
        cleanupLocked();
        return err;
    }

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    if (s_eth_netif == nullptr) {
        cleanupLocked();
        return ESP_ERR_NO_MEM;
    }

    err = deriveLocalMacLocked();
    if (err != ESP_OK) {
        cleanupLocked();
        return err;
    }

    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    if (s_eth_glue == nullptr) {
        cleanupLocked();
        return ESP_ERR_NO_MEM;
    }

    err = esp_netif_attach(s_eth_netif, s_eth_glue);
    if (err != ESP_OK) {
        cleanupLocked();
        return err;
    }

    err = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, ethEventHandler, nullptr, &s_eth_event_instance);
    if (err != ESP_OK) {
        cleanupLocked();
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, gotIpEventHandler, nullptr, &s_ip_event_instance);
    if (err != ESP_OK) {
        cleanupLocked();
        return err;
    }

    if (s_state.hostname_set) {
        err = esp_netif_set_hostname(s_eth_netif, s_state.hostname);
        if (err != ESP_OK) {
            cleanupLocked();
            return err;
        }
    }

    xEventGroupClearBits(s_event_group, kStartedBit | kConnectedBit);
    clearIpInfo();
    s_state.initialized = true;

    ESP_LOGI(TAG,
             "initialized on host %d MISO=%d MOSI=%d SCLK=%d CS=%d INT=%d RST=%d clock=%luHz mode=%s",
             host,
             miso_pin,
             mosi_pin,
             sclk_pin,
             cs_pin,
             int_pin,
             rst_pin,
             static_cast<unsigned long>(clock_hz),
             (int_pin >= 0) ? "interrupt" : "poll");
    return ESP_OK;
}

esp_err_t W5500::start(void) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_state.initialized || s_eth_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if ((xEventGroupGetBits(s_event_group) & kStartedBit) != 0) {
        return ESP_OK;
    }

    return esp_eth_start(s_eth_handle);
}

esp_err_t W5500::stop(void) const
{
    if (s_mutex == nullptr) {
        return ESP_OK;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_state.initialized || s_eth_handle == nullptr) {
        return ESP_OK;
    }

    if ((xEventGroupGetBits(s_event_group) & kStartedBit) == 0) {
        return ESP_OK;
    }

    return esp_eth_stop(s_eth_handle);
}

esp_err_t W5500::end(void) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_state.initialized) {
        return ESP_OK;
    }

    if (s_eth_handle != nullptr && (xEventGroupGetBits(s_event_group) & kStartedBit) != 0) {
        esp_err_t err = esp_eth_stop(s_eth_handle);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
    }

    cleanupLocked();
    return ESP_OK;
}

bool W5500::ready(void) const
{
    if (s_mutex == nullptr) {
        return false;
    }

    LockGuard guard(s_mutex);
    return guard.locked() && s_state.initialized;
}

bool W5500::started(void) const
{
    if (s_event_group == nullptr) {
        return false;
    }

    return (xEventGroupGetBits(s_event_group) & kStartedBit) != 0;
}

bool W5500::connected(void) const
{
    if (s_event_group == nullptr) {
        return false;
    }

    return (xEventGroupGetBits(s_event_group) & kConnectedBit) != 0;
}

bool W5500::hasIp(void) const
{
    if (s_mutex == nullptr) {
        return false;
    }

    LockGuard guard(s_mutex);
    return guard.locked() && s_state.ip_ready;
}

bool W5500::waitConnected(uint32_t timeout_ms) const
{
    if (s_event_group == nullptr) {
        return false;
    }

    const TickType_t timeout_ticks = (timeout_ms == portMAX_DELAY)
        ? portMAX_DELAY
        : pdMS_TO_TICKS(timeout_ms);

    return (xEventGroupWaitBits(s_event_group, kConnectedBit, pdFALSE, pdFALSE, timeout_ticks) & kConnectedBit) != 0;
}

esp_err_t W5500::renew(void) const
{
    if (ensureSyncPrimitives() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_state.initialized || s_eth_netif == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_netif_dhcpc_stop(s_eth_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return err;
    }

    err = esp_netif_dhcpc_start(s_eth_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        return err;
    }

    return ESP_OK;
}

esp_err_t W5500::hostname(const char *value) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    err = copyString(s_state.hostname, sizeof(s_state.hostname), value);
    if (err != ESP_OK) {
        return err;
    }

    s_state.hostname_set = (value != nullptr) && (value[0] != '\0');
    if (s_eth_netif != nullptr && s_state.hostname_set) {
        return esp_netif_set_hostname(s_eth_netif, s_state.hostname);
    }

    return ESP_OK;
}

esp_err_t W5500::mac(uint8_t out[6]) const
{
    if (out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_state.mac_valid) {
        return ESP_ERR_INVALID_STATE;
    }

    memcpy(out, s_state.mac, kMacLen);
    return ESP_OK;
}

const char *W5500::localIP(void) const
{
    static char ip_copy[kIpv4StringMaxLen] = "0.0.0.0";

    if (s_mutex == nullptr) {
        return ip_copy;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return ip_copy;
    }

    memcpy(ip_copy, s_state.local_ip, sizeof(ip_copy));
    return ip_copy;
}

const char *W5500::gateway(void) const
{
    static char gateway_copy[kIpv4StringMaxLen] = "0.0.0.0";

    if (s_mutex == nullptr) {
        return gateway_copy;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return gateway_copy;
    }

    memcpy(gateway_copy, s_state.gateway, sizeof(gateway_copy));
    return gateway_copy;
}

const char *W5500::subnet(void) const
{
    static char subnet_copy[kIpv4StringMaxLen] = "0.0.0.0";

    if (s_mutex == nullptr) {
        return subnet_copy;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return subnet_copy;
    }

    memcpy(subnet_copy, s_state.subnet, sizeof(subnet_copy));
    return subnet_copy;
}

int W5500::host(void) const
{
    if (s_mutex == nullptr) {
        return DEFAULT_HOST;
    }

    LockGuard guard(s_mutex);
    return guard.locked() ? s_state.host : DEFAULT_HOST;
}

uint32_t W5500::clockHz(void) const
{
    if (s_mutex == nullptr) {
        return DEFAULT_CLOCK_HZ;
    }

    LockGuard guard(s_mutex);
    return guard.locked() ? s_state.clock_hz : DEFAULT_CLOCK_HZ;
}

W5500 w5500;

} // namespace esp32libfun
