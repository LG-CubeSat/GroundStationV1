cmake_minimum_required(VERSION 3.16)
project(GroundStation LANGUAGES C)

add_library(SSDV STATIC lib/SSDV/rs8.c lib/SSDV/ssdv.c)

add_executable(GroundStationSSDV src/ssdv.c)
target_include_directories(GroundStationSSDV PRIVATE lib/SSDV lib/ccsds/include)
target_link_libraries(GroundStationSSDV PRIVATE SSDV)

add_executable(GroundStationCCSDS src/ccsds.c)
target_include_directories(GroundStationCCSDS PRIVATE lib/SSDV lib/ccsds/include)

add_executable(GroundStationSSDVEncoder slop/ssdv_encoder.c)
target_include_directories(GroundStationSSDVEncoder PRIVATE lib/SSDV)
target_link_libraries(GroundStationSSDVEncoder PRIVATE SSDV)
