cmake_minimum_required(VERSION 3.16)

project(RadioHandler LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "RadioHandler requires Linux GPIO and UART interfaces.")
endif()

find_package(Threads REQUIRED)
find_path(PIGPIOD_IF2_INCLUDE_DIR NAMES pigpiod_if2.h)
find_library(PIGPIOD_IF2_LIBRARY NAMES pigpiod_if2)
if(NOT PIGPIOD_IF2_INCLUDE_DIR OR NOT PIGPIOD_IF2_LIBRARY)
    message(FATAL_ERROR
        "pigpiod_if2 is required. Install the pigpio development files or set "
        "PIGPIOD_IF2_INCLUDE_DIR and PIGPIOD_IF2_LIBRARY.")
endif()

# Supply radio_deframer, radio_queue, and their CCSDS dependencies from the
# consuming project; those files are not included in this repository.
set(RADIOHANDLER_SUPPORT_SOURCES "" CACHE STRING
    "Semicolon-separated supporting C source files to compile into RadioHandler")
set(RADIOHANDLER_SUPPORT_INCLUDE_DIRS "" CACHE STRING
    "Semicolon-separated include directories for the supporting headers")

add_library(RadioHandler STATIC
    src/radio.c
    include/radio.h
    ${RADIOHANDLER_SUPPORT_SOURCES}
)
add_library(RadioHandler::RadioHandler ALIAS RadioHandler)

target_compile_features(RadioHandler PRIVATE c_std_11)
target_compile_definitions(RadioHandler PRIVATE
    _DEFAULT_SOURCE
    _POSIX_C_SOURCE=200809L
)
target_include_directories(RadioHandler
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${PIGPIOD_IF2_INCLUDE_DIR}"
        ${RADIOHANDLER_SUPPORT_INCLUDE_DIRS}
)
target_link_libraries(RadioHandler PRIVATE
    Threads::Threads
    "${PIGPIOD_IF2_LIBRARY}"
)
