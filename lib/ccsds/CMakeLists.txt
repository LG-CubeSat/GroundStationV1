# libaec 1.1.7 requires CMake 3.26 when used as a standalone project, while
# FlightSoftwareV1 intentionally supports CMake 3.16. Build only the four
# codec sources we need and generate the same two configuration headers here.
include(CheckCSourceCompiles)
include(TestBigEndian)

set(CCSDS_LIBAEC_ROOT ${CMAKE_SOURCE_DIR}/lib/libaec)
set(CCSDS_LIBAEC_GENERATED_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/libaec_include)

test_big_endian(WORDS_BIGENDIAN)
check_c_source_compiles(
    "int main(void) { return __builtin_clzll(1ULL); }"
    HAVE_DECL___BUILTIN_CLZLL
)
set(HAVE_SNPRINTF 1)

file(MAKE_DIRECTORY ${CCSDS_LIBAEC_GENERATED_INCLUDE})
configure_file(
    ${CCSDS_LIBAEC_ROOT}/include/config.h.in
    ${CCSDS_LIBAEC_GENERATED_INCLUDE}/config.h
)

# These variables are used by libaec.h.in and are directory-local here.
set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION_PATCH 7)
configure_file(
    ${CCSDS_LIBAEC_ROOT}/include/libaec.h.in
    ${CCSDS_LIBAEC_GENERATED_INCLUDE}/libaec.h
    @ONLY
)

add_library(ccsds_libaec STATIC
    ${CCSDS_LIBAEC_ROOT}/src/encode.c
    ${CCSDS_LIBAEC_ROOT}/src/encode_accessors.c
    ${CCSDS_LIBAEC_ROOT}/src/decode.c
    ${CCSDS_LIBAEC_ROOT}/src/vector.c
)

target_include_directories(ccsds_libaec
    PUBLIC ${CCSDS_LIBAEC_GENERATED_INCLUDE}
    PRIVATE ${CCSDS_LIBAEC_ROOT}/src
)

add_library(ccsds STATIC
    src/compression.c
    src/crc32.c
    src/profile.c
    src/space_packet.c
    src/status.c
    src/stream.c
)

target_include_directories(ccsds
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ccsds PRIVATE ccsds_libaec)

target_compile_options(ccsds PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)