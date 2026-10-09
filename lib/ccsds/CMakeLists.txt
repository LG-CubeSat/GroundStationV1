add_subdirectory(ccsds)

add_library(frame_codec STATIC
    interfaces/frame.c
)

target_include_directories(frame_codec
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/interfaces
)

add_library(csp_interface STATIC
    csp/csp_if_spi.c
    csp/csp_network.c
)

target_include_directories(csp_interface
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/csp
)

target_link_libraries(csp_interface
    PUBLIC
        csp
        platform
)