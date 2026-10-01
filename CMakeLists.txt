cmake_minimum_required(VERSION 3.16)
project(GroundStation LANGUAGES C)

add_executable(GroundStationSSDV src/ssdv.c)
add_executable(GroundStationCCSDS src/ccsds.c)
target_include_directories(GroundStationSSDV PRIVATE lib/SSDV lib/ccsds/include)
target_include_directories(GroundStationCCSDS PRIVATE lib/SSDV lib/ccsds/include)
