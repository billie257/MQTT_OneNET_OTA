# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "V3.1.1")

set(APP_SW_VERSION    "3.1.1")
set(APP_SW_VERSION_MAJOR 1)
set(APP_SW_VERSION_MINOR 2)
set(APP_SW_VERSION_PATCH 3)

set(FW_VERSION        "3.1.1")
set(FW_VERSION_MAJOR  0)
set(FW_VERSION_MINOR  9)
set(FW_VERSION_PATCH  5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(idf-mqtt-test)
