cmake_minimum_required(VERSION 3.16)
project(GroundStation LANGUAGES C)

add_library(SSDV STATIC lib/SSDV/rs8.c lib/SSDV/ssdv.c)

add_library(simpl_SSDV INTERFACE)
target_include_directories(simpl_SSDV INTERFACE lib)
target_link_libraries(simpl_SSDV INTERFACE SSDV)

add_executable(GroundStationSSDV src/ssdv.c)
target_include_directories(GroundStationSSDV PRIVATE lib/SSDV lib/ccsds/include lib/)
target_link_libraries(GroundStationSSDV PRIVATE simpl_SSDV)

add_executable(GroundStationCCSDS src/ccsds.c)
target_include_directories(GroundStationCCSDS PRIVATE lib/SSDV lib/ccsds/include lib/)

add_executable(GroundStationUplink src/ccsds_uplink.c)
target_include_directories(GroundStationUplink PRIVATE lib/SSDV lib/ccsds/include lib/)

add_executable(GroundStationSSDVEncoder slop/ssdv_encoder.c)
target_include_directories(GroundStationSSDVEncoder PRIVATE lib/SSDV)
target_link_libraries(GroundStationSSDVEncoder PRIVATE SSDV)
