# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

set(ESP_PIXELS_COMPOSER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../esp_pixels_composer"
    CACHE PATH "Composer checkout (bench baseline: v0.2.0)")
set(ESP_PIXELS_FX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../esp_pixels_fx"
    CACHE PATH "FX checkout (bench baseline: e7e0ba3)")

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/libs/esp_pixels"
    "${ESP_PIXELS_FX_DIR}"
    "${ESP_PIXELS_COMPOSER_DIR}"
    "${ESP_PIXELS_COMPOSER_DIR}/adapter_esp_pixels"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(framework_esp32libfun)
