cmake_minimum_required(VERSION 3.16)
project(GroundStation LANGUAGES C)

# Libraries
add_subdirectory(lib/nng)
add_subdirectory(lib/radio)
add_subdirectory(lib/ccsds)
add_library(SSDV STATIC lib/SSDV/rs8.c lib/SSDV/ssdv.c)
add_library(GroundStationSerial STATIC lib/serial.c)

add_library(simpl_SSDV INTERFACE)
target_include_directories(simpl_SSDV INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/lib/simpl_SSDV)
target_link_libraries(simpl_SSDV INTERFACE SSDV)

# Prod Executables
add_executable(GSMain src/main.c)
target_include_directories(GSMain PRIVATE lib/ccsds/include lib/nng/include lib/radio)
target_link_libraries(GSMain PRIVATE ccsds radio nng)

# Web Executable uses ngg, and civetweb libraries

# Temporary Dev Executables
add_executable(GroundStationNNG src/nng.c)
target_link_libraries(GroundStationNNG PRIVATE nng)

add_executable(GroundStationSSDV src/ssdv.c)
target_include_directories(GroundStationSSDV PRIVATE lib/SSDV lib/ccsds/include lib/simpl_SSDV)
target_link_libraries(GroundStationSSDV PRIVATE simpl_SSDV)

add_executable(GroundStationCCSDS src/ccsds.c)
target_include_directories(GroundStationCCSDS PRIVATE lib/SSDV lib/ccsds/include lib/)
target_link_libraries(GroundStationCCSDS PRIVATE ccsds radio)

add_executable(GroundStationSSDVEncoder slop/ssdv_encoder.c)
target_include_directories(GroundStationSSDVEncoder PRIVATE lib/SSDV lib/simpl_SSDV)
target_link_libraries(GroundStationSSDVEncoder PRIVATE SSDV)

add_executable(GroundStationWrapPacket src/wrap.c)
target_include_directories(GroundStationWrapPacket PRIVATE lib/ccsds/include lib/nng/include lib/radio)
target_link_libraries(GroundStationWrapPacket PRIVATE ccsds radio nng)
