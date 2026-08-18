#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct httpd_req httpd_req_t;

#define ESP32LIBFUN_WEBSERVER_VERSION "v0.2.0"
#define ESP32LIBFUN_WEBSERVER_VERSION_MAJOR 0
#define ESP32LIBFUN_WEBSERVER_VERSION_MINOR 2
#define ESP32LIBFUN_WEBSERVER_VERSION_PATCH 0

namespace esp32libfun {

/// HTTP route handler compatible with the ESP-IDF HTTP server.
///
/// @param req Incoming request handle.
/// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
typedef esp_err_t (*web_handler_t)(httpd_req_t *req);

/// Small route-oriented wrapper around the ESP-IDF HTTP server.
class WebServer {
public:
    static constexpr size_t MAX_ROUTES = 16;
    static constexpr size_t MAX_PATH_LEN = 64;

    /// Starts the HTTP server and applies all registered routes.
    ///
    /// @param port TCP port to listen on.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t begin(uint16_t port = 80) const;
    /// Stops the HTTP server while keeping registered routes in memory.
    ///
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t stop(void) const;

    /// Registers one GET route.
    ///
    /// @param path Route path, e.g. `"/status"`.
    /// @param handler Function invoked when the route matches.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t get(const char *path, web_handler_t handler) const;
    /// Registers one POST route.
    ///
    /// @param path Route path, e.g. `"/config"`.
    /// @param handler Function invoked when the route matches.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t post(const char *path, web_handler_t handler) const;
    /// Registers one custom 404 handler.
    ///
    /// @param handler Function invoked when no route matches.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t notFound(web_handler_t handler) const;

    /// Sends one response with an explicit content type.
    ///
    /// @param req Request to respond to.
    /// @param content_type MIME type for the response, e.g. `"text/plain"`.
    /// @param body Response body.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t send(httpd_req_t *req, const char *content_type, const char *body) const;
    /// Sends one HTML response.
    ///
    /// @param req Request to respond to.
    /// @param html HTML body.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t sendHtml(httpd_req_t *req, const char *html) const;
    /// Sends one JSON response.
    ///
    /// @param req Request to respond to.
    /// @param json JSON body.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t sendJson(httpd_req_t *req, const char *json) const;
    /// Sends one response after setting the HTTP status string.
    ///
    /// @param req Request to respond to.
    /// @param status HTTP status string, e.g. `"404 Not Found"`.
    /// @param content_type MIME type for the response.
    /// @param body Response body.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t sendStatus(httpd_req_t *req, const char *status, const char *content_type, const char *body) const;
    /// Sends a 303 redirect response.
    ///
    /// @param req Request to respond to.
    /// @param location Target URL for the `Location` header.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t redirect(httpd_req_t *req, const char *location) const;
    /// Reads the full request body into a caller-owned buffer.
    ///
    /// @param req Request whose body should be read.
    /// @param buffer Buffer that receives the body bytes.
    /// @param buffer_len Capacity of `buffer`, in bytes.
    /// @param out_len Receives the number of bytes actually read, or `nullptr` to ignore.
    /// @return `ESP_OK` on success, or an `esp_err_t` describing the failure.
    esp_err_t readBody(httpd_req_t *req, char *buffer, size_t buffer_len, size_t *out_len = nullptr) const;

private:
    static esp_err_t ensureSyncPrimitives(void);
    static esp_err_t registerRoute(int method, const char *path, web_handler_t handler);
    static esp_err_t applyRoutes(void);
};

/// Global web server convenience object.
extern WebServer web;

} // namespace esp32libfun

using esp32libfun::web;
