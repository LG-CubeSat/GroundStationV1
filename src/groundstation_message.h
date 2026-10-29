#ifndef GROUNDSTATION_MESSAGE_H
#define GROUNDSTATION_MESSAGE_H

#include "ccsds/space_packet.h"
#include "serial.h"

#define GROUNDSTATION_NNG_URL "ipc:///tmp/groundstation.ipc"
#define GROUNDSTATION_COMMAND_NNG_URL "ipc:///tmp/groundstation-commands.ipc"
#define GROUNDSTATION_PACKET_DATA_CAPACITY CCSDS_SPACE_PACKET_MAX_DATA_SIZE
#define GROUNDSTATION_SSDV_PACKET_SIZE 256U

#pragma pack(push, 1)
typedef struct {
    uint8_t samples[GROUNDSTATION_PACKET_DATA_CAPACITY];
} groundstation_telemetry_packet_t;

typedef struct {
    uint8_t data[GROUNDSTATION_SSDV_PACKET_SIZE];
} groundstation_ssdv_packet_t;

typedef struct {
    uint8_t commands[GROUNDSTATION_PACKET_DATA_CAPACITY];
} groundstation_command_packet_t;

typedef union {
    groundstation_telemetry_packet_t telemetry;
    groundstation_ssdv_packet_t ssdv;
    groundstation_command_packet_t command;
} groundstation_packet_t;
#pragma pack(pop)

_Static_assert(sizeof(groundstation_telemetry_packet_t) == 65536U, "Telemetry storage size changed");
_Static_assert(sizeof(groundstation_ssdv_packet_t) == 256U, "SSDV packet size changed");
_Static_assert(sizeof(groundstation_command_packet_t) == 65536U, "Command storage size changed");
_Static_assert(sizeof(groundstation_packet_t) == 65536U, "Packet storage size changed");
_Static_assert(_Alignof(groundstation_telemetry_packet_t) == 1, "Telemetry storage must be packed");
_Static_assert(_Alignof(groundstation_ssdv_packet_t) == 1, "SSDV packet must be packed");
_Static_assert(_Alignof(groundstation_command_packet_t) == 1, "Command storage must be packed");
_Static_assert(_Alignof(groundstation_packet_t) == 1, "Packet storage must be packed");
_Static_assert(offsetof(groundstation_telemetry_packet_t, samples) == 0U, "Telemetry data must start at byte zero");
_Static_assert(offsetof(groundstation_ssdv_packet_t, data) == 0U, "SSDV data must start at byte zero");
_Static_assert(offsetof(groundstation_command_packet_t, commands) == 0U, "Command data must start at byte zero");

#endif
