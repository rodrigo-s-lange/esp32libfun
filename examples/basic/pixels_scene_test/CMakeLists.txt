cmake_minimum_required(VERSION 3.22)
get_filename_component(BENCH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
get_filename_component(COMPOSER_ROOT "${BENCH_ROOT}/../esp_pixels_composer" ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${BENCH_ROOT}/framework/core"
    "${BENCH_ROOT}/../esp_pixels_fx" "${COMPOSER_ROOT}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pixels_scene_test)
