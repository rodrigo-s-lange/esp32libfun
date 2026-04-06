#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct httpd_req httpd_req_t;

namespace esp32libfun {

typedef esp_err_t (*web_handler_t)(httpd_req_t *req);

class WebServer {
public:
    static constexpr size_t MAX_ROUTES = 16;
    static constexpr size_t MAX_PATH_LEN = 64;

    esp_err_t begin(uint16_t port = 80) const;
    esp_err_t stop(void) const;

    esp_err_t get(const char *path, web_handler_t handler) const;
    esp_err_t post(const char *path, web_handler_t handler) const;
    esp_err_t notFound(web_handler_t handler) const;

    esp_err_t send(httpd_req_t *req, const char *content_type, const char *body) const;
    esp_err_t sendHtml(httpd_req_t *req, const char *html) const;
    esp_err_t sendJson(httpd_req_t *req, const char *json) const;
    esp_err_t sendStatus(httpd_req_t *req, const char *status, const char *content_type, const char *body) const;
    esp_err_t redirect(httpd_req_t *req, const char *location) const;
    esp_err_t readBody(httpd_req_t *req, char *buffer, size_t buffer_len, size_t *out_len = nullptr) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t registerRoute(int method, const char *path, web_handler_t handler);
    static esp_err_t applyRoutes(void);
};

extern WebServer web;

} // namespace esp32libfun

using esp32libfun::web;
