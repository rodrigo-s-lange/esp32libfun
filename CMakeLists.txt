cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS
    "framework/core"
    "framework/libs"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32libfun)
