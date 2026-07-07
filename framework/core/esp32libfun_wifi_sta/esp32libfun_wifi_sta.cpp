#include "esp32libfun_wifi_sta.hpp"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "lwip/ip4_addr.h"
#include "nvs_flash.h"

static const char *TAG = "ESP32LIBFUN_WIFI_STA";

namespace {

constexpr EventBits_t kWifiConnectedBit = BIT0;
constexpr EventBits_t kWifiStartedBit = BIT1;
constexpr uint32_t kWifiStartTimeoutMs = 1000;
constexpr size_t kSsidMaxLen = 32;
constexpr size_t kPasswordMaxLen = 64;
constexpr size_t kHostnameMaxLen = 32;
constexpr size_t kIpv4StringMaxLen = 16;

struct WifiStaState {
    bool initialized = false;
    bool started = false;
    bool connect_requested = false;
    bool hostname_set = false;
    bool ip_set = false;
    bool gateway_set = false;
    bool subnet_set = false;
    bool static_ip_active = false;
    char hostname[kHostnameMaxLen + 1] = {};
    char static_ip[kIpv4StringMaxLen] = {};
    char static_gateway[kIpv4StringMaxLen] = {};
    char static_subnet[kIpv4StringMaxLen] = {};
    char local_ip[kIpv4StringMaxLen] = "0.0.0.0";
};

WifiStaState s_wifi_state;
SemaphoreHandle_t s_wifi_mutex = nullptr;
EventGroupHandle_t s_wifi_event_group = nullptr;
esp_netif_t *s_wifi_netif = nullptr;
portMUX_TYPE s_wifi_spinlock = portMUX_INITIALIZER_UNLOCKED;
esp_event_handler_instance_t s_wifi_event_instance_any_id = nullptr;
esp_event_handler_instance_t s_ip_event_instance_got_ip = nullptr;

bool wifi_lock(TickType_t timeout = portMAX_DELAY)
{
    return (s_wifi_mutex != nullptr) && (xSemaphoreTake(s_wifi_mutex, timeout) == pdTRUE);
}

void wifi_unlock(void)
{
    if (s_wifi_mutex != nullptr) {
        xSemaphoreGive(s_wifi_mutex);
    }
}

void setLocalIpString(const char *value)
{
    taskENTER_CRITICAL(&s_wifi_spinlock);
    if (value == nullptr || value[0] == '\0') {
        strcpy(s_wifi_state.local_ip, "0.0.0.0");
    } else {
        strncpy(s_wifi_state.local_ip, value, sizeof(s_wifi_state.local_ip) - 1);
        s_wifi_state.local_ip[sizeof(s_wifi_state.local_ip) - 1] = '\0';
    }
    taskEXIT_CRITICAL(&s_wifi_spinlock);
}

const char *ip4AddrToString(const esp_ip4_addr_t *value, char *buffer, size_t buffer_len)
{
    if (buffer == nullptr || buffer_len == 0) {
        return "";
    }

    if (value == nullptr) {
        strncpy(buffer, "0.0.0.0", buffer_len - 1);
        buffer[buffer_len - 1] = '\0';
        return buffer;
    }

    esp_ip4addr_ntoa(value, buffer, buffer_len);
    return buffer;
}

void setConnectRequested(bool value)
{
    taskENTER_CRITICAL(&s_wifi_spinlock);
    s_wifi_state.connect_requested = value;
    taskEXIT_CRITICAL(&s_wifi_spinlock);
}

bool connectRequested(void)
{
    taskENTER_CRITICAL(&s_wifi_spinlock);
    const bool value = s_wifi_state.connect_requested;
    taskEXIT_CRITICAL(&s_wifi_spinlock);
    return value;
}

void wifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_wifi_state.started = true;
        xEventGroupSetBits(s_wifi_event_group, kWifiStartedBit);
        ESP_LOGI(TAG, "station started");

        if (connectRequested()) {
            const esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "connect on start failed: %s", esp_err_to_name(err));
            }
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {
        s_wifi_state.started = false;
        xEventGroupClearBits(s_wifi_event_group, kWifiStartedBit | kWifiConnectedBit);
        setLocalIpString("0.0.0.0");
        ESP_LOGI(TAG, "station stopped");
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, kWifiConnectedBit);
        setLocalIpString("0.0.0.0");
        const wifi_event_sta_disconnected_t *event =
            static_cast<const wifi_event_sta_disconnected_t *>(event_data);
        const unsigned reason = (event != nullptr) ? static_cast<unsigned>(event->reason) : 0U;
        ESP_LOGW(TAG, "station disconnected (reason=%u)", reason);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = static_cast<const ip_event_got_ip_t *>(event_data);
        char ip_string[kIpv4StringMaxLen] = {};
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_string, sizeof(ip_string));
        setLocalIpString(ip_string);
        xEventGroupSetBits(s_wifi_event_group, kWifiConnectedBit);
        ESP_LOGI(TAG, "connected with IP %s", ip_string);
    }
}

} // namespace

namespace esp32libfun {

esp_err_t WifiSta::ensureSyncPrimitives(void)
{
    if (s_wifi_mutex == nullptr) {
        s_wifi_mutex = xSemaphoreCreateMutex();
        if (s_wifi_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_wifi_event_group == nullptr) {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

esp_err_t WifiSta::validateIpString(const char *value)
{
    if (value == nullptr || value[0] == '\0') {
        return ESP_OK;
    }

    ip4_addr_t ip = {};
    return ip4addr_aton(value, &ip) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t WifiSta::copyString(char *dst, size_t dst_len, const char *src)
{
    if (dst == nullptr || dst_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (src == nullptr || src[0] == '\0') {
        dst[0] = '\0';
        return ESP_OK;
    }

    size_t len = strlen(src);
    if (len >= dst_len) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(dst, src, len + 1);
    return ESP_OK;
}

esp_err_t WifiSta::applyHostname(void)
{
    if (s_wifi_netif == nullptr || !s_wifi_state.hostname_set) {
        return ESP_OK;
    }

    return esp_netif_set_hostname(s_wifi_netif, s_wifi_state.hostname);
}

esp_err_t WifiSta::applyNetifConfig(void)
{
    if (s_wifi_netif == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = applyHostname();
    if (err != ESP_OK) {
        return err;
    }

    const bool use_static_ip = s_wifi_state.ip_set || s_wifi_state.gateway_set || s_wifi_state.subnet_set;
    const bool full_static_ip = s_wifi_state.ip_set && s_wifi_state.gateway_set && s_wifi_state.subnet_set;

    if (use_static_ip && !full_static_ip) {
        ESP_LOGE(TAG, "static IP requires ip, gateway and subnet together");
        return ESP_ERR_INVALID_STATE;
    }

    if (!full_static_ip) {
        if (!s_wifi_state.static_ip_active) {
            return ESP_OK;
        }

        err = esp_netif_dhcpc_start(s_wifi_netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            return err;
        }

        s_wifi_state.static_ip_active = false;
        return ESP_OK;
    }

    err = esp_netif_dhcpc_stop(s_wifi_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return err;
    }

    ip4_addr_t ip = {};
    ip4_addr_t gateway = {};
    ip4_addr_t subnet = {};

    if (!ip4addr_aton(s_wifi_state.static_ip, &ip) ||
        !ip4addr_aton(s_wifi_state.static_gateway, &gateway) ||
        !ip4addr_aton(s_wifi_state.static_subnet, &subnet)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_ip_info_t ip_info = {};
    ip_info.ip.addr = ip.addr;
    ip_info.gw.addr = gateway.addr;
    ip_info.netmask.addr = subnet.addr;
    err = esp_netif_set_ip_info(s_wifi_netif, &ip_info);
    if (err == ESP_OK) {
        s_wifi_state.static_ip_active = true;
    }
    return err;
}

esp_err_t WifiSta::initStack(void)
{
    if (s_wifi_state.initialized) {
        return ESP_OK;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (s_wifi_netif == nullptr) {
        s_wifi_netif = esp_netif_create_default_wifi_sta();
        if (s_wifi_netif == nullptr) {
            return ESP_FAIL;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifiEventHandler,
        nullptr,
        &s_wifi_event_instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifiEventHandler,
        nullptr,
        &s_ip_event_instance_got_ip));

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }

    s_wifi_state.initialized = true;
    return ESP_OK;
}

esp_err_t WifiSta::begin(const char *ssid, const char *password) const
{
    if (ssid == nullptr || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_len = strlen(ssid);
    const size_t password_len = (password != nullptr) ? strlen(password) : 0;
    if (ssid_len > kSsidMaxLen || password_len > kPasswordMaxLen) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = initStack();
    if (err != ESP_OK) {
        return err;
    }

    if (!wifi_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    xEventGroupClearBits(s_wifi_event_group, kWifiConnectedBit | kWifiStartedBit);
    ::setLocalIpString("0.0.0.0");
    ::setConnectRequested(false);

    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_STARTED) {
        wifi_unlock();
        return stop_err;
    }

    wifi_config_t cfg = {};
    memcpy(cfg.sta.ssid, ssid, ssid_len);
    if (password != nullptr && password_len > 0) {
        memcpy(cfg.sta.password, password, password_len);
    }
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        wifi_unlock();
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        wifi_unlock();
        return err;
    }

    err = applyNetifConfig();
    if (err != ESP_OK) {
        wifi_unlock();
        return err;
    }

    ::setConnectRequested(true);

    err = esp_wifi_start();
    if (err == ESP_OK) {
        const EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group,
            kWifiStartedBit,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(kWifiStartTimeoutMs));
        if ((bits & kWifiStartedBit) == 0) {
            err = ESP_ERR_TIMEOUT;
        }
    }

    wifi_unlock();
    return err;
}

esp_err_t WifiSta::clean(void) const
{
    esp_err_t err = initStack();
    if (err != ESP_OK) {
        return err;
    }

    if (!wifi_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    ::setConnectRequested(false);
    xEventGroupClearBits(s_wifi_event_group, kWifiConnectedBit);
    ::setLocalIpString("0.0.0.0");

    esp_err_t disconnect_err = esp_wifi_disconnect();
    if (disconnect_err != ESP_OK &&
        disconnect_err != ESP_ERR_WIFI_NOT_CONNECT &&
        disconnect_err != ESP_ERR_WIFI_NOT_STARTED) {
        err = disconnect_err;
    }

    esp_err_t stop_err = esp_wifi_stop();
    if (err == ESP_OK &&
        stop_err != ESP_OK &&
        stop_err != ESP_ERR_WIFI_NOT_STARTED) {
        err = stop_err;
    }

    wifi_config_t cfg = {};
    if (err == ESP_OK) {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    }

    if (err == ESP_OK) {
        s_wifi_state.started = false;
        s_wifi_state.hostname_set = false;
        s_wifi_state.ip_set = false;
        s_wifi_state.gateway_set = false;
        s_wifi_state.subnet_set = false;
        s_wifi_state.static_ip_active = false;
        s_wifi_state.hostname[0] = '\0';
        s_wifi_state.static_ip[0] = '\0';
        s_wifi_state.static_gateway[0] = '\0';
        s_wifi_state.static_subnet[0] = '\0';
    }

    wifi_unlock();
    return err;
}

esp_err_t WifiSta::disconnect(void) const
{
    if (!s_wifi_state.initialized || !s_wifi_state.started) {
        return ESP_OK;
    }

    ::setConnectRequested(false);
    xEventGroupClearBits(s_wifi_event_group, kWifiConnectedBit);
    ::setLocalIpString("0.0.0.0");
    esp_err_t err = esp_wifi_disconnect();
    return (err == ESP_ERR_WIFI_NOT_CONNECT || err == ESP_ERR_WIFI_NOT_STARTED) ? ESP_OK : err;
}

bool WifiSta::isConnected(void) const
{
    if (s_wifi_event_group == nullptr) {
        return false;
    }

    return (xEventGroupGetBits(s_wifi_event_group) & kWifiConnectedBit) != 0;
}

bool WifiSta::waitConnected(uint32_t timeout_ms) const
{
    if (s_wifi_event_group == nullptr) {
        return false;
    }

    const TickType_t timeout_ticks = (timeout_ms == portMAX_DELAY)
        ? portMAX_DELAY
        : pdMS_TO_TICKS(timeout_ms);

    return (xEventGroupWaitBits(
                s_wifi_event_group,
                kWifiConnectedBit,
                pdFALSE,
                pdFALSE,
                timeout_ticks) &
            kWifiConnectedBit) != 0;
}

esp_err_t WifiSta::ip(const char *ip) const
{
    esp_err_t err = validateIpString(ip);
    if (err != ESP_OK) {
        return err;
    }

    err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    if (!wifi_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    err = copyString(s_wifi_state.static_ip, sizeof(s_wifi_state.static_ip), ip);
    s_wifi_state.ip_set = (err == ESP_OK) && (ip != nullptr) && (ip[0] != '\0');
    wifi_unlock();
    return err;
}

esp_err_t WifiSta::gateway(const char *gateway) const
{
    esp_err_t err = validateIpString(gateway);
    if (err != ESP_OK) {
        return err;
    }

    err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    if (!wifi_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    err = copyString(s_wifi_state.static_gateway, sizeof(s_wifi_state.static_gateway), gateway);
    s_wifi_state.gateway_set = (err == ESP_OK) && (gateway != nullptr) && (gateway[0] != '\0');
    wifi_unlock();
    return err;
}

esp_err_t WifiSta::subnet(const char *subnet) const
{
    esp_err_t err = validateIpString(subnet);
    if (err != ESP_OK) {
        return err;
    }

    err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    if (!wifi_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    err = copyString(s_wifi_state.static_subnet, sizeof(s_wifi_state.static_subnet), subnet);
    s_wifi_state.subnet_set = (err == ESP_OK) && (subnet != nullptr) && (subnet[0] != '\0');
    wifi_unlock();
    return err;
}

esp_err_t WifiSta::hostname(const char *hostname) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    if (!wifi_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    err = copyString(s_wifi_state.hostname, sizeof(s_wifi_state.hostname), hostname);
    s_wifi_state.hostname_set = (err == ESP_OK) && (hostname != nullptr) && (hostname[0] != '\0');
    wifi_unlock();
    return err;
}

const char *WifiSta::localIP(void) const
{
    taskENTER_CRITICAL(&s_wifi_spinlock);
    static char ip_copy[kIpv4StringMaxLen] = {};
    memcpy(ip_copy, s_wifi_state.local_ip, sizeof(ip_copy));
    taskEXIT_CRITICAL(&s_wifi_spinlock);
    return ip_copy;
}

const char *WifiSta::gatewayIP(void) const
{
    static char gateway_copy[kIpv4StringMaxLen] = "0.0.0.0";
    if (!isConnected() || s_wifi_netif == nullptr) {
        strncpy(gateway_copy, "0.0.0.0", sizeof(gateway_copy) - 1);
        gateway_copy[sizeof(gateway_copy) - 1] = '\0';
        return gateway_copy;
    }

    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_get_ip_info(s_wifi_netif, &ip_info) != ESP_OK) {
        strncpy(gateway_copy, "0.0.0.0", sizeof(gateway_copy) - 1);
        gateway_copy[sizeof(gateway_copy) - 1] = '\0';
        return gateway_copy;
    }

    return ::ip4AddrToString(&ip_info.gw, gateway_copy, sizeof(gateway_copy));
}

const char *WifiSta::subnetMask(void) const
{
    static char subnet_copy[kIpv4StringMaxLen] = "0.0.0.0";
    if (!isConnected() || s_wifi_netif == nullptr) {
        strncpy(subnet_copy, "0.0.0.0", sizeof(subnet_copy) - 1);
        subnet_copy[sizeof(subnet_copy) - 1] = '\0';
        return subnet_copy;
    }

    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_get_ip_info(s_wifi_netif, &ip_info) != ESP_OK) {
        strncpy(subnet_copy, "0.0.0.0", sizeof(subnet_copy) - 1);
        subnet_copy[sizeof(subnet_copy) - 1] = '\0';
        return subnet_copy;
    }

    return ::ip4AddrToString(&ip_info.netmask, subnet_copy, sizeof(subnet_copy));
}

const char *WifiSta::dns1(void) const
{
    static char dns_copy[kIpv4StringMaxLen] = "0.0.0.0";
    if (!isConnected() || s_wifi_netif == nullptr) {
        strncpy(dns_copy, "0.0.0.0", sizeof(dns_copy) - 1);
        dns_copy[sizeof(dns_copy) - 1] = '\0';
        return dns_copy;
    }

    esp_netif_dns_info_t dns = {};
    if (esp_netif_get_dns_info(s_wifi_netif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK) {
        strncpy(dns_copy, "0.0.0.0", sizeof(dns_copy) - 1);
        dns_copy[sizeof(dns_copy) - 1] = '\0';
        return dns_copy;
    }

    return ::ip4AddrToString(&dns.ip.u_addr.ip4, dns_copy, sizeof(dns_copy));
}

const char *WifiSta::dns2(void) const
{
    static char dns_copy[kIpv4StringMaxLen] = "0.0.0.0";
    if (!isConnected() || s_wifi_netif == nullptr) {
        strncpy(dns_copy, "0.0.0.0", sizeof(dns_copy) - 1);
        dns_copy[sizeof(dns_copy) - 1] = '\0';
        return dns_copy;
    }

    esp_netif_dns_info_t dns = {};
    if (esp_netif_get_dns_info(s_wifi_netif, ESP_NETIF_DNS_BACKUP, &dns) != ESP_OK) {
        strncpy(dns_copy, "0.0.0.0", sizeof(dns_copy) - 1);
        dns_copy[sizeof(dns_copy) - 1] = '\0';
        return dns_copy;
    }

    return ::ip4AddrToString(&dns.ip.u_addr.ip4, dns_copy, sizeof(dns_copy));
}

WifiSta wifi;

} // namespace esp32libfun
