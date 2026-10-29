cmake_minimum_required(VERSION 3.16)
project(GroundStation LANGUAGES C)

# Libraries
add_subdirectory(lib/nng)
add_subdirectory(lib/radio)
add_subdirectory(lib/ccsds)
add_library(SSDV STATIC lib/SSDV/rs8.c lib/SSDV/ssdv.c)
add_library(GroundStationSerial STATIC lib/serial.c)
target_include_directories(GroundStationSerial PUBLIC lib)
target_compile_features(GroundStationSerial PUBLIC c_std_11)

# Prod Executables
add_executable(GSMain src/main.c)
target_compile_features(GSMain PRIVATE c_std_11)
target_include_directories(GSMain PRIVATE lib/ccsds/include lib/nng/include lib/radio)
target_link_libraries(GSMain PRIVATE ccsds radio nng GroundStationSerial SSDV)

# Web Executable uses ngg, and civetweb libraries
