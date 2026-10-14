add_library(packet_police STATIC packet_police.c)
target_include_directories(packet_police PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/ccsds/include
    ${CMAKE_SOURCE_DIR}/lib/SSDV
)
target_link_libraries(packet_police PUBLIC SSDV)
