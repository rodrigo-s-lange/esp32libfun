#include "esp32libfun_lan8720.hpp"

#include <string.h>

#include "driver/gpio.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
// esp_eth_phy_lan87xx.h não está disponível no ESP-IDF 6.0 para ESP32-S3
// Usaremos a API genérica de PHY
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_netif_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"

namespace esp32libfun {

namespace {

const char *TAG = "ESP32LIBFUN_LAN8720";

constexpr EventBits_t kStartedBit = BIT0;
constexpr EventBits_t kConnectedBit = BIT1;
constexpr size_t kHostnameMaxLen = 32;
constexpr size_t kIpv4StringMaxLen = 16;
constexpr size_t kMacLen = 6;
constexpr uint32_t kLinkCheckPeriodMs = 2000;
constexpr uint32_t kPhyPowerStabilizeMs = 50;
constexpr uint32_t kRxTaskStackSize = 6144;
constexpr uint32_t kRxTaskPriority = 19;

struct Lan8720State {
    bool initialized = false;
    bool connected = false;
    bool ip_ready = false;
    bool hostname_set = false;
    bool mac_valid = false;
    bool static_ip_active = false;
    bool ip_set = false;
    bool gateway_set = false;
    bool subnet_set = false;
    int mdc_pin = -1;
    int mdio_pin = -1;
    int phy_addr = Lan8720::DEFAULT_PHY_ADDR;
    int rst_pin = Lan8720::DEFAULT_RESET_PIN;
    int clock_mode = LAN8720_CLK_EXT_IN;
    int clock_gpio = Lan8720::DEFAULT_CLOCK_GPIO;
    int clock_enable_pin = Lan8720::DEFAULT_CLOCK_ENABLE_PIN;
    int intr_priority = 0;
    char hostname[kHostnameMaxLen + 1] = {};
    char static_ip[kIpv4StringMaxLen] = {};
    char static_gateway[kIpv4StringMaxLen] = {};
    char static_subnet[kIpv4StringMaxLen] = {};
    char local_ip[kIpv4StringMaxLen] = "0.0.0.0";
    char gateway[kIpv4StringMaxLen] = "0.0.0.0";
    char subnet[kIpv4StringMaxLen] = "0.0.0.0";
    uint8_t mac[kMacLen] = {};
};

Lan8720State s_state = {};

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

esp_err_t normalizeGlobalInitErr(esp_err_t err)
{
    return (err == ESP_ERR_INVALID_STATE) ? ESP_OK : err;
}

const char *dhcpStatusName(esp_netif_dhcp_status_t status)
{
    switch (status) {
        case ESP_NETIF_DHCP_INIT:
            return "init";
        case ESP_NETIF_DHCP_STARTED:
            return "started";
        case ESP_NETIF_DHCP_STOPPED:
            return "stopped";
        default:
            return "unknown";
    }
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

esp_err_t prepareClockEnableLocked(void)
{
    if (s_state.clock_enable_pin < 0) {
        return ESP_OK;
    }

    gpio_config_t clk_en_cfg = {};
    clk_en_cfg.pin_bit_mask = 1ULL << s_state.clock_enable_pin;
    clk_en_cfg.mode = GPIO_MODE_OUTPUT;
    clk_en_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    clk_en_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    clk_en_cfg.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&clk_en_cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(static_cast<gpio_num_t>(s_state.clock_enable_pin), 1);
    if (err != ESP_OK) {
        return err;
    }

    // Some boards use this GPIO as PHY power so the external RMII clock only
    // appears after boot straps are already sampled. Give the PHY time to rise.
    vTaskDelay(pdMS_TO_TICKS(kPhyPowerStabilizeMs));
    return ESP_OK;
}

esp_err_t refreshMacLocked(void)
{
    if (s_eth_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t mac[kMacLen] = {};
    esp_err_t err = esp_eth_ioctl(s_eth_handle, ETH_CMD_G_MAC_ADDR, mac);
    if (err != ESP_OK) {
        return err;
    }

    memcpy(s_state.mac, mac, sizeof(mac));
    s_state.mac_valid = true;
    return ESP_OK;
}

esp_err_t ensureDhcpClientStartedLocked(const char *reason)
{
    if (s_eth_netif == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const bool use_static_ip = s_state.ip_set || s_state.gateway_set || s_state.subnet_set;
    if (use_static_ip) {
        return ESP_OK;
    }

    esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_STATUS_MAX;
    esp_err_t err = esp_netif_dhcpc_get_status(s_eth_netif, &status);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s dhcp status read failed: %s", reason, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "%s dhcp status=%s", reason, dhcpStatusName(status));

    if (status == ESP_NETIF_DHCP_INIT || status == ESP_NETIF_DHCP_STOPPED) {
        err = esp_netif_dhcpc_start(s_eth_netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            ESP_LOGW(TAG, "%s dhcp start failed: %s", reason, esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "%s dhcp start requested", reason);
    }

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
            refreshMacLocked();
            ensureDhcpClientStartedLocked("link-up");
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

    struct netif *lwip_netif = static_cast<struct netif *>(esp_netif_get_netif_impl(s_eth_netif));
    if (lwip_netif != nullptr) {
        tcpip_callback(
            [](void *ctx) {
                struct netif *netif = static_cast<struct netif *>(ctx);
                if (netif != nullptr) {
                    // Sent twice: some managed switches need a second gratuitous
                    // ARP to flush their table after a link-up event.
                    etharp_gratuitous(netif);
                    etharp_gratuitous(netif);
                }
            },
            lwip_netif);
    }
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
        esp_netif_tx_rx_event_disable(s_eth_netif);
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

    if (s_state.rst_pin >= 0) {
        gpio_reset_pin(static_cast<gpio_num_t>(s_state.rst_pin));
    }
    if (s_state.clock_enable_pin >= 0) {
        gpio_reset_pin(static_cast<gpio_num_t>(s_state.clock_enable_pin));
    }

    xEventGroupClearBits(s_event_group, kStartedBit | kConnectedBit);
    // s_state = {} zero-fills the struct — the in-class "0.0.0.0" initializers
    // only apply at construction, not on assignment. clearIpInfo() must follow
    // to restore the readable default strings in the IP fields.
    s_state = {};
    clearIpInfo();
}

} // namespace

esp_err_t Lan8720::ensureSyncPrimitives(void)
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

esp_err_t Lan8720::copyString(char *dst, size_t dst_len, const char *src)
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

esp_err_t Lan8720::validateIpString(const char *value)
{
    if (value == nullptr || value[0] == '\0') {
        return ESP_OK;
    }

    ip4_addr_t addr = {};
    return ip4addr_aton(value, &addr) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t Lan8720::applyHostname(void)
{
    if (s_eth_netif == nullptr || !s_state.hostname_set) {
        return ESP_OK;
    }

    return esp_netif_set_hostname(s_eth_netif, s_state.hostname);
}

esp_err_t Lan8720::applyNetifConfig(void)
{
    if (s_eth_netif == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = applyHostname();
    if (err != ESP_OK) {
        return err;
    }

    const bool use_static_ip = s_state.ip_set || s_state.gateway_set || s_state.subnet_set;
    const bool full_static_ip = s_state.ip_set && s_state.gateway_set && s_state.subnet_set;

    if (use_static_ip && !full_static_ip) {
        ESP_LOGE(TAG, "static IP requires ip, gateway and subnet together");
        return ESP_ERR_INVALID_STATE;
    }

    if (!full_static_ip) {
        if (!s_state.static_ip_active) {
            return ESP_OK;
        }

        err = esp_netif_dhcpc_start(s_eth_netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            return err;
        }

        s_state.static_ip_active = false;
        return ESP_OK;
    }

    err = esp_netif_dhcpc_stop(s_eth_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return err;
    }

    ip4_addr_t ip = {};
    ip4_addr_t gateway = {};
    ip4_addr_t subnet = {};

    if (!ip4addr_aton(s_state.static_ip, &ip) ||
        !ip4addr_aton(s_state.static_gateway, &gateway) ||
        !ip4addr_aton(s_state.static_subnet, &subnet)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_ip_info_t ip_info = {};
    ip_info.ip.addr = ip.addr;
    ip_info.gw.addr = gateway.addr;
    ip_info.netmask.addr = subnet.addr;
    err = esp_netif_set_ip_info(s_eth_netif, &ip_info);
    if (err == ESP_OK) {
        s_state.static_ip_active = true;
        esp_ip4addr_ntoa(&ip_info.ip, s_state.local_ip, sizeof(s_state.local_ip));
        esp_ip4addr_ntoa(&ip_info.gw, s_state.gateway, sizeof(s_state.gateway));
        esp_ip4addr_ntoa(&ip_info.netmask, s_state.subnet, sizeof(s_state.subnet));
        s_state.ip_ready = true;
    }
    return err;
}

esp_err_t Lan8720::begin(int mdc_pin,
                         int mdio_pin,
                         int phy_addr,
                         int rst_pin,
                         int clock_mode,
                         int clock_gpio,
                         int clock_enable_pin,
                         int intr_priority) const
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(mdc_pin)) ||
        !GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(mdio_pin)) ||
        (rst_pin >= 0 && !GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(rst_pin))) ||
        (clock_mode != LAN8720_CLK_EXT_IN && clock_mode != LAN8720_CLK_OUT) ||
        !GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(clock_gpio)) ||
        (clock_enable_pin >= 0 && !GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(clock_enable_pin))) ||
        (phy_addr < ESP_ETH_PHY_ADDR_AUTO || phy_addr > 31)) {
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
            s_state.mdc_pin == mdc_pin &&
            s_state.mdio_pin == mdio_pin &&
            s_state.phy_addr == phy_addr &&
            s_state.rst_pin == rst_pin &&
            s_state.clock_mode == clock_mode &&
            s_state.clock_gpio == clock_gpio &&
            s_state.clock_enable_pin == clock_enable_pin &&
            s_state.intr_priority == intr_priority;
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

    s_state.mdc_pin = mdc_pin;
    s_state.mdio_pin = mdio_pin;
    s_state.phy_addr = phy_addr;
    s_state.rst_pin = rst_pin;
    s_state.clock_mode = clock_mode;
    s_state.clock_gpio = clock_gpio;
    s_state.clock_enable_pin = clock_enable_pin;
    s_state.intr_priority = intr_priority;

    err = prepareClockEnableLocked();
    if (err != ESP_OK) {
        cleanupLocked();
        return err;
    }

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.rx_task_stack_size = kRxTaskStackSize;
    mac_config.rx_task_prio = kRxTaskPriority;
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.smi_gpio.mdc_num = mdc_pin;
    emac_config.smi_gpio.mdio_num = mdio_pin;
    emac_config.interface = EMAC_DATA_INTERFACE_RMII;
    emac_config.clock_config.rmii.clock_mode = (clock_mode == LAN8720_CLK_OUT) ? EMAC_CLK_OUT : EMAC_CLK_EXT_IN;
    emac_config.clock_config.rmii.clock_gpio = clock_gpio;
    emac_config.intr_priority = intr_priority;

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = phy_addr;
    phy_config.reset_gpio_num = rst_pin;

    s_eth_mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (s_eth_mac == nullptr) {
        cleanupLocked();
        return ESP_ERR_NO_MEM;
    }

    // No ESP-IDF 6.0, usamos a API genérica para PHY
    s_eth_phy = esp_eth_phy_new_generic(&phy_config);
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

    err = applyNetifConfig();
    if (err != ESP_OK) {
        cleanupLocked();
        return err;
    }

    err = refreshMacLocked();
    if (err != ESP_OK) {
        cleanupLocked();
        return err;
    }

    xEventGroupClearBits(s_event_group, kStartedBit | kConnectedBit);
    clearIpInfo();
    s_state.initialized = true;

    ESP_LOGI(TAG,
             "initialized MDC=%d MDIO=%d PHY=%d RST=%d clk_mode=%s clk_gpio=%d phy_power=%d",
             mdc_pin,
             mdio_pin,
             phy_addr,
             rst_pin,
             (clock_mode == LAN8720_CLK_OUT) ? "out" : "ext-in",
             clock_gpio,
             clock_enable_pin);
    return ESP_OK;
}

esp_err_t Lan8720::start(void) const
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

esp_err_t Lan8720::stop(void) const
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

esp_err_t Lan8720::end(void) const
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

bool Lan8720::ready(void) const
{
    if (s_mutex == nullptr) {
        return false;
    }

    LockGuard guard(s_mutex);
    return guard.locked() && s_state.initialized;
}

bool Lan8720::started(void) const
{
    if (s_event_group == nullptr) {
        return false;
    }

    return (xEventGroupGetBits(s_event_group) & kStartedBit) != 0;
}

bool Lan8720::connected(void) const
{
    if (s_event_group == nullptr) {
        return false;
    }

    return (xEventGroupGetBits(s_event_group) & kConnectedBit) != 0;
}

bool Lan8720::hasIp(void) const
{
    if (s_mutex == nullptr) {
        return false;
    }

    LockGuard guard(s_mutex);
    return guard.locked() && s_state.ip_ready;
}

bool Lan8720::waitConnected(uint32_t timeout_ms) const
{
    if (s_event_group == nullptr) {
        return false;
    }

    // UINT32_MAX is the "wait forever" sentinel in the ms domain;
    // portMAX_DELAY is the equivalent in the ticks domain.
    const TickType_t timeout_ticks = (timeout_ms == UINT32_MAX)
        ? portMAX_DELAY
        : pdMS_TO_TICKS(timeout_ms);

    return (xEventGroupWaitBits(s_event_group, kConnectedBit, pdFALSE, pdFALSE, timeout_ticks) & kConnectedBit) != 0;
}

esp_err_t Lan8720::renew(void) const
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

esp_err_t Lan8720::ip(const char *ip) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    err = validateIpString(ip);
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    err = copyString(s_state.static_ip, sizeof(s_state.static_ip), ip);
    s_state.ip_set = (err == ESP_OK) && (ip != nullptr) && (ip[0] != '\0');
    if (err != ESP_OK || s_eth_netif == nullptr) {
        return err;
    }

    return applyNetifConfig();
}

esp_err_t Lan8720::gateway(const char *gateway) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    err = validateIpString(gateway);
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    err = copyString(s_state.static_gateway, sizeof(s_state.static_gateway), gateway);
    s_state.gateway_set = (err == ESP_OK) && (gateway != nullptr) && (gateway[0] != '\0');
    if (err != ESP_OK || s_eth_netif == nullptr) {
        return err;
    }

    return applyNetifConfig();
}

esp_err_t Lan8720::subnet(const char *subnet) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    err = validateIpString(subnet);
    if (err != ESP_OK) {
        return err;
    }

    LockGuard guard(s_mutex);
    if (!guard.locked()) {
        return ESP_ERR_TIMEOUT;
    }

    err = copyString(s_state.static_subnet, sizeof(s_state.static_subnet), subnet);
    s_state.subnet_set = (err == ESP_OK) && (subnet != nullptr) && (subnet[0] != '\0');
    if (err != ESP_OK || s_eth_netif == nullptr) {
        return err;
    }

    return applyNetifConfig();
}

esp_err_t Lan8720::hostname(const char *value) const
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
    if (s_eth_netif != nullptr) {
        return applyNetifConfig();
    }

    return ESP_OK;
}

esp_err_t Lan8720::mac(uint8_t out[6]) const
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

const char *Lan8720::localIP(void) const
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

const char *Lan8720::gateway(void) const
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

const char *Lan8720::subnet(void) const
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

int Lan8720::phyAddr(void) const
{
    if (s_mutex == nullptr) {
        return DEFAULT_PHY_ADDR;
    }

    LockGuard guard(s_mutex);
    return guard.locked() ? s_state.phy_addr : DEFAULT_PHY_ADDR;
}

int Lan8720::clockMode(void) const
{
    if (s_mutex == nullptr) {
        return LAN8720_CLK_EXT_IN;
    }

    LockGuard guard(s_mutex);
    return guard.locked() ? s_state.clock_mode : LAN8720_CLK_EXT_IN;
}

int Lan8720::clockGpio(void) const
{
    if (s_mutex == nullptr) {
        return DEFAULT_CLOCK_GPIO;
    }

    LockGuard guard(s_mutex);
    return guard.locked() ? s_state.clock_gpio : DEFAULT_CLOCK_GPIO;
}

int Lan8720::clockEnablePin(void) const
{
    if (s_mutex == nullptr) {
        return DEFAULT_CLOCK_ENABLE_PIN;
    }

    LockGuard guard(s_mutex);
    return guard.locked() ? s_state.clock_enable_pin : DEFAULT_CLOCK_ENABLE_PIN;
}

Lan8720 lan8720;

} // namespace esp32libfun
