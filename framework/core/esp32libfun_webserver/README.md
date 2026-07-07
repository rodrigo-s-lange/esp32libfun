# esp32libfun_webserver

Thin route-oriented wrapper around the ESP-IDF HTTP server.

## Scope

- start and stop one HTTP server
- register simple GET and POST routes
- register a custom 404 handler
- send plain, HTML, JSON, status, and redirect responses
- read request bodies into caller-owned buffers

This module does not manage Wi-Fi or Ethernet. Bring up networking first with
`esp32libfun_wifi_sta`, `esp32libfun_w5500`, `esp32libfun_lan8720`, or direct
ESP-IDF code.

## Enable It

```text
CONFIG_ESP32LIBFUN_WEBSERVER=y
```

## Public API

- `web.begin(port)`: starts the HTTP server.
- `web.stop()`: stops the HTTP server.
- `web.get(path, handler)`: registers one GET route.
- `web.post(path, handler)`: registers one POST route.
- `web.notFound(handler)`: registers one custom 404 handler.
- `web.send(req, content_type, body)`: sends a response.
- `web.sendHtml(req, html)`: sends HTML.
- `web.sendJson(req, json)`: sends JSON.
- `web.sendStatus(req, status, content_type, body)`: sends a status response.
- `web.redirect(req, location)`: sends a 303 redirect.
- `web.readBody(req, buffer, buffer_len, out_len)`: reads the request body.

## Usage

```cpp
#include "esp32libfun.hpp"

static esp_err_t handleRoot(httpd_req_t *req)
{
    return web.sendHtml(req, "<h1>esp32libfun</h1>");
}

extern "C" void app_main(void)
{
    esp32libfun_init();

    ESP_ERROR_CHECK(web.get("/", handleRoot));
    ESP_ERROR_CHECK(web.begin(80));
}
```

## Example

- Webserver buildable example: pending
