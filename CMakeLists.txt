# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
#cmake_minimum_required(VERSION 3.5)
cmake_minimum_required(VERSION 3.8)
set(CMAKE_CXX_STANDARD 17)

add_definitions(-DLV_LVGL_H_INCLUDE_SIMPLE)

# Check if the environment variable BOARD is defined
if(DEFINED ENV{BOARD})
    add_compile_definitions($ENV{BOARD})
else()
    message(FATAL_ERROR "Environment variable BOARD is not set")
endif()

message(STATUS "Selected board: $ENV{BOARD}")

# Hash Commander OS: HCOS_DISPLAY=BIG builds for the YYSLUPING big-screen NerdQAxe++ (480x320 display, 32 MB BOYA
# flash, its own partition table). Without it the build is the stock NerdQAxe++ (320x170, 16 MB) as before.
if(DEFINED ENV{HCOS_DISPLAY} AND "$ENV{HCOS_DISPLAY}" STREQUAL "BIG")
    if(NOT "$ENV{BOARD}" STREQUAL "NERDQAXEPLUS2")
        message(FATAL_ERROR "HCOS_DISPLAY=BIG requires BOARD=NERDQAXEPLUS2")
    endif()
    add_compile_definitions(HCOS_BIG)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.hcos-big.defaults")
    message(STATUS "Hash Commander OS: big screen 480x320")
else()
    set(EXCLUDE_COMPONENTS yysluping_boya_flash)
endif()

set(ENV{SOURCE_DATE_EPOCH} 0)


add_compile_definitions(LV_MEM_CUSTOM=1)
add_compile_definitions(LV_MEM_CUSTOM_INCLUDE=<lvgl_psram.h>)
add_compile_definitions(LV_MEM_CUSTOM_ALLOC=lv_psram_alloc)
add_compile_definitions(LV_MEM_CUSTOM_FREE=lv_psram_free)
add_compile_definitions(LV_MEM_CUSTOM_REALLOC=lv_psram_realloc)

#add_compile_definitions(CONFIG_FREERTOS_USE_TRACE_FACILITY=1)
#add_compile_definitions(CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS=1)

# adds -I./main; needed for components to have
# access to main include files
include_directories("./main")

# this are only header files
include_directories("./components/arduinojson")

# (Not part of the boilerplate)
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
# set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)


project(esp-miner)

# Existing sdkconfig files override sdkconfig.defaults, including revoked roots.
if(NOT CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
   OR NOT CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE
   OR NOT CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE
   OR CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEPRECATED_LIST
   OR NOT CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH STREQUAL "certificates/cacert-2026-10-20.pem")
    message(FATAL_ERROR "NerdQAxe requires its pinned CA bundle. Update the certificate bundle settings in sdkconfig to match sdkconfig.defaults; see certificates/README.md.")
endif()
