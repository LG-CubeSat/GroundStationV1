cmake_minimum_required(VERSION 3.5)
project(GroundStation LANGUAGES C)

# Libraries
set(NNG_TESTS OFF CACHE BOOL "Build NNG tests" FORCE)
add_subdirectory(lib/nng)
add_subdirectory(lib/ccsds) # before radio so RadioHandler uses this ccsds instead of its own copy
add_subdirectory(lib/radio)

set(CIVETWEB_BUILD_TESTING OFF CACHE BOOL "Build CivetWeb tests")
set(CIVETWEB_ENABLE_SERVER_EXECUTABLE OFF CACHE BOOL "Build the standalone CivetWeb server")
set(CIVETWEB_ENABLE_ASAN OFF CACHE BOOL "Enable AddressSanitizer for CivetWeb")
# CivetWeb v1.16 declares policies older than CMake 4 supports.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
add_subdirectory(lib/civetweb)

add_library(SSDV STATIC lib/SSDV/rs8.c lib/SSDV/ssdv.c)
add_library(GroundStationSerial STATIC lib/serial.c)
target_include_directories(GroundStationSerial PUBLIC lib)
target_compile_features(GroundStationSerial PUBLIC c_std_11)

# Prod Executables
add_executable(GSMain src/main.c)
target_compile_features(GSMain PRIVATE c_std_11)
target_include_directories(GSMain PRIVATE lib/ccsds/include lib/nng/include)
target_link_libraries(GSMain PRIVATE ccsds RadioHandler nng GroundStationSerial SSDV)

# Web executable uses NNG and CivetWeb libraries
add_executable(GSEyecandy src/web.c)
target_compile_features(GSEyecandy PRIVATE c_std_11)
target_link_libraries(GSEyecandy PRIVATE nng civetweb-c-library GroundStationSerial)
