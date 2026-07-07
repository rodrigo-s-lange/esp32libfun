cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../framework/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../framework/libs"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gpio_blink)
