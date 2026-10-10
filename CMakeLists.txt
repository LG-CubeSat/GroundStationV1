cmake_minimum_required(VERSION 3.16)
project(GroundStation LANGUAGES C)

# Libraries
add_subdirectory(lib/nng)
add_subdirectory(lib/QCBOR)
add_subdirectory(lib/radio)
add_library(SSDV STATIC lib/SSDV/rs8.c lib/SSDV/ssdv.c)

# Prod Executables
add_executable(GSMain src/main.c)
target_include_directories(GSMain PRIVATE lib/ccsds/include lib/nng/include lib/QCBOR/inc lib/radio lib/simpl_SSDV lib/SSDV)
target_link_libraries(GSMain PRIVATE SSDV)

# Temporary Dev Executables
add_executable(GroundStationSSDV src/ssdv.c)
target_include_directories(GroundStationSSDV PRIVATE lib/SSDV lib/ccsds/include lib/simpl_SSDV)
target_link_libraries(GroundStationSSDV PRIVATE SSDV)

add_executable(GroundStationCCSDS src/ccsds.c)
target_include_directories(GroundStationCCSDS PRIVATE lib/SSDV lib/ccsds/include)

add_executable(GroundStationSSDVEncoder slop/ssdv_encoder.c)
target_include_directories(GroundStationSSDVEncoder PRIVATE lib/SSDV lib/simpl_SSDV)
target_link_libraries(GroundStationSSDVEncoder PRIVATE SSDV)
