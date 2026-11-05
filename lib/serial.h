#ifndef GROUNDSTATION_SERIAL_H
#define GROUNDSTATION_SERIAL_H

#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Ground station serialization requires a little-endian target"
#endif
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64) || \
                            defined(_M_ARM) || defined(_M_ARM64))
#else
#error "Cannot verify target endianness for ground station serialization"
#endif

_Static_assert(CHAR_BIT == 8, "Serialization requires 8-bit bytes");
_Static_assert(sizeof(uint32_t) == 4, "Serialization requires 4-byte uint32_t");

#define GROUNDSTATION_SSDV_PACKET_SIZE 256U
#define GROUNDSTATION_COMMAND_TEXT_CAPACITY 64U

#pragma pack(push, 1)
typedef struct {
    uint32_t test_run;
    uint32_t sample;
    float temperature_c;
    float battery_v;
    int32_t altitude_m;
} groundstation_telemetry_packet_t;

typedef struct {
    uint8_t data[GROUNDSTATION_SSDV_PACKET_SIZE];
} groundstation_ssdv_packet_t;

typedef struct {
    uint32_t request_id;
    char command[GROUNDSTATION_COMMAND_TEXT_CAPACITY];
} groundstation_command_packet_t;

typedef union {
    groundstation_telemetry_packet_t telemetry;
    groundstation_ssdv_packet_t ssdv;
    groundstation_command_packet_t command;
} groundstation_packet_t;
#pragma pack(pop)

#define GROUNDSTATION_PACKET_DATA_CAPACITY sizeof(groundstation_packet_t)

_Static_assert(sizeof(float) == 4U && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
               FLT_MAX_EXP == 128, "Payloads require binary32 floats");
_Static_assert(sizeof(groundstation_telemetry_packet_t) == 20U, "Telemetry layout changed");
_Static_assert(sizeof(groundstation_ssdv_packet_t) == 256U, "SSDV layout changed");
_Static_assert(sizeof(groundstation_command_packet_t) == 68U, "Command layout changed");
_Static_assert(sizeof(groundstation_packet_t) == 256U, "Packet storage size changed");
_Static_assert(_Alignof(groundstation_telemetry_packet_t) == 1, "Telemetry must be packed");
_Static_assert(_Alignof(groundstation_command_packet_t) == 1, "Commands must be packed");
_Static_assert(_Alignof(groundstation_ssdv_packet_t) == 1, "SSDV must be packed");
_Static_assert(offsetof(groundstation_telemetry_packet_t, test_run) == 0U, "Telemetry run offset changed");
_Static_assert(offsetof(groundstation_telemetry_packet_t, sample) == 4U, "Telemetry sample offset changed");
_Static_assert(offsetof(groundstation_telemetry_packet_t, temperature_c) == 8U, "Telemetry temperature offset changed");
_Static_assert(offsetof(groundstation_telemetry_packet_t, battery_v) == 12U, "Telemetry battery offset changed");
_Static_assert(offsetof(groundstation_telemetry_packet_t, altitude_m) == 16U, "Telemetry altitude offset changed");
_Static_assert(offsetof(groundstation_command_packet_t, request_id) == 0U, "Command request offset changed");
_Static_assert(offsetof(groundstation_command_packet_t, command) == 4U, "Command text offset changed");

typedef enum {
    SERIAL_OK = 0,
    SERIAL_ERROR_NULL = -1,
    SERIAL_ERROR_SIZE = -2,
    SERIAL_ERROR_BUFFER_TOO_SMALL = -3
} serial_status_t;

serial_status_t serialize(const void *data, size_t size, void *buffer, size_t capacity, size_t *written);

serial_status_t deserialize(const void *buffer, size_t length, void *data, size_t size);

const char *serial_status_string(serial_status_t status);

serial_status_t serialize_telemetry(const groundstation_telemetry_packet_t *value,
                                  void *buffer, size_t capacity, size_t *written);
serial_status_t deserialize_telemetry(const void *buffer, size_t length,
                                    groundstation_telemetry_packet_t *value);
serial_status_t serialize_command(const groundstation_command_packet_t *value,
                                void *buffer, size_t capacity, size_t *written);
serial_status_t deserialize_command(const void *buffer, size_t length,
                                  groundstation_command_packet_t *value);
serial_status_t serialize_ssdv(const groundstation_ssdv_packet_t *value,
                             void *buffer, size_t capacity, size_t *written);
serial_status_t deserialize_ssdv(const void *buffer, size_t length,
                               groundstation_ssdv_packet_t *value);

#endif
