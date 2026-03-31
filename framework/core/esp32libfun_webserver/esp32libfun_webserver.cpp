#include "esp32libfun_webserver.hpp"

#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ESP32LIBFUN_WEBSERVER";

namespace {

struct WebRoute {
    bool used = false;
    httpd_method_t method = HTTP_GET;
    char path[esp32libfun::WebServer::MAX_PATH_LEN] = {};
    esp32libfun::web_handler_t handler = nullptr;
};

WebRoute s_routes[esp32libfun::WebServer::MAX_ROUTES] = {};
size_t s_route_count = 0;
httpd_handle_t s_server = nullptr;
SemaphoreHandle_t s_web_mutex = nullptr;
esp32libfun::web_handler_t s_not_found_handler = nullptr;

bool web_lock(TickType_t timeout = portMAX_DELAY)
{
    return (s_web_mutex != nullptr) && (xSemaphoreTake(s_web_mutex, timeout) == pdTRUE);
}

void web_unlock(void)
{
    if (s_web_mutex != nullptr) {
        xSemaphoreGive(s_web_mutex);
    }
}

esp_err_t dispatchRoute(httpd_req_t *req)
{
    WebRoute *route = static_cast<WebRoute *>(req->user_ctx);
    if (route == nullptr || route->handler == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Route handler not available");
    }

    return route->handler(req);
}

esp_err_t dispatchNotFound(httpd_req_t *req, httpd_err_code_t error)
{
    (void)error;

    if (s_not_found_handler != nullptr) {
        return s_not_found_handler(req);
    }

    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
}

} // namespace

namespace esp32libfun {

esp_err_t WebServer::ensureSyncPrimitives(void)
{
    if (s_web_mutex == nullptr) {
        s_web_mutex = xSemaphoreCreateMutex();
        if (s_web_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

esp_err_t WebServer::registerRoute(int method, const char *path, web_handler_t handler)
{
    if (path == nullptr || path[0] == '\0' || handler == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    const size_t path_len = strlen(path);
    if (path_len >= MAX_PATH_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!web_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    for (size_t i = 0; i < s_route_count; ++i) {
        if (s_routes[i].used &&
            s_routes[i].method == method &&
            strcmp(s_routes[i].path, path) == 0) {
            web_unlock();
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (s_route_count >= MAX_ROUTES) {
        web_unlock();
        return ESP_ERR_NO_MEM;
    }

    WebRoute &route = s_routes[s_route_count];
    route.used = true;
    route.method = static_cast<httpd_method_t>(method);
    memcpy(route.path, path, path_len + 1);
    route.handler = handler;

    if (s_server != nullptr) {
        httpd_uri_t uri = {};
        uri.uri = route.path;
        uri.method = route.method;
        uri.handler = dispatchRoute;
        uri.user_ctx = &route;
        err = httpd_register_uri_handler(s_server, &uri);
        if (err != ESP_OK) {
            route = {};
            web_unlock();
            return err;
        }
    }

    ++s_route_count;
    web_unlock();
    return ESP_OK;
}

esp_err_t WebServer::applyRoutes(void)
{
    if (s_server == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < s_route_count; ++i) {
        if (!s_routes[i].used) {
            continue;
        }

        httpd_uri_t uri = {};
        uri.uri = s_routes[i].path;
        uri.method = s_routes[i].method;
        uri.handler = dispatchRoute;
        uri.user_ctx = &s_routes[i];

        esp_err_t err = httpd_register_uri_handler(s_server, &uri);
        if (err != ESP_OK) {
            return err;
        }
    }

    return httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, dispatchNotFound);
}

esp_err_t WebServer::begin(uint16_t port) const
{
    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    if (!web_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_server != nullptr) {
        web_unlock();
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = MAX_ROUTES;

    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        web_unlock();
        return err;
    }

    err = applyRoutes();
    if (err != ESP_OK) {
        httpd_stop(s_server);
        s_server = nullptr;
        web_unlock();
        return err;
    }

    ESP_LOGI(TAG, "web server started on port %u", static_cast<unsigned>(port));
    web_unlock();
    return ESP_OK;
}

esp_err_t WebServer::stop(void) const
{
    if (s_web_mutex == nullptr) {
        return ESP_OK;
    }

    if (!web_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_server == nullptr) {
        web_unlock();
        return ESP_OK;
    }

    esp_err_t err = httpd_stop(s_server);
    if (err == ESP_OK) {
        s_server = nullptr;
    }

    web_unlock();
    return err;
}

esp_err_t WebServer::get(const char *path, web_handler_t handler) const
{
    return registerRoute(HTTP_GET, path, handler);
}

esp_err_t WebServer::post(const char *path, web_handler_t handler) const
{
    return registerRoute(HTTP_POST, path, handler);
}

esp_err_t WebServer::notFound(web_handler_t handler) const
{
    if (handler == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensureSyncPrimitives();
    if (err != ESP_OK) {
        return err;
    }

    if (!web_lock()) {
        return ESP_ERR_TIMEOUT;
    }

    s_not_found_handler = handler;
    if (s_server != nullptr) {
        err = httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, dispatchNotFound);
    } else {
        err = ESP_OK;
    }

    web_unlock();
    return err;
}

esp_err_t WebServer::send(httpd_req_t *req, const char *content_type, const char *body) const
{
    if (req == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = httpd_resp_set_type(req, (content_type != nullptr) ? content_type : "text/plain");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set content type: %s", esp_err_to_name(err));
        return err;
    }

    return httpd_resp_send(req, (body != nullptr) ? body : "", HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::sendHtml(httpd_req_t *req, const char *html) const
{
    return send(req, "text/html; charset=utf-8", html);
}

esp_err_t WebServer::sendJson(httpd_req_t *req, const char *json) const
{
    return send(req, "application/json", json);
}

esp_err_t WebServer::sendStatus(httpd_req_t *req, const char *status, const char *content_type, const char *body) const
{
    if (req == nullptr || status == nullptr || status[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = httpd_resp_set_status(req, status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set status: %s", esp_err_to_name(err));
        return err;
    }

    return send(req, content_type, body);
}

esp_err_t WebServer::redirect(httpd_req_t *req, const char *location) const
{
    if (req == nullptr || location == nullptr || location[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = httpd_resp_set_status(req, "303 See Other");
    if (err != ESP_OK) {
        return err;
    }

    err = httpd_resp_set_hdr(req, "Location", location);
    if (err != ESP_OK) {
        return err;
    }

    return sendHtml(req, "<html><body><p>Redirecting...</p></body></html>");
}

esp_err_t WebServer::readBody(httpd_req_t *req, char *buffer, size_t buffer_len, size_t *out_len) const
{
    if (req == nullptr || buffer == nullptr || buffer_len < 2) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t content_len = static_cast<size_t>(req->content_len);
    if (content_len + 1 > buffer_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t total = 0;
    while (total < content_len) {
        const int received = httpd_req_recv(req, buffer + total, content_len - total);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (received <= 0) {
            return ESP_FAIL;
        }
        total += static_cast<size_t>(received);
    }

    buffer[total] = '\0';
    if (out_len != nullptr) {
        *out_len = total;
    }

    return ESP_OK;
}

WebServer web;

} // namespace esp32libfun
