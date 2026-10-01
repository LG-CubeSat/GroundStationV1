#ifndef CCSDS_STATUS_H
#define CCSDS_STATUS_H

/*
 * Every CCSDS helper returns one of these values.  Keeping one small error
 * vocabulary makes it possible for flight code to handle failures without
 * parsing log messages or knowing which layer produced the error.
 */
typedef enum {
    CCSDS_OK = 0,
    CCSDS_ERROR_NULL = -1,
    CCSDS_ERROR_RANGE = -2,
    CCSDS_ERROR_BUFFER_TOO_SMALL = -3,
    CCSDS_ERROR_INVALID_FORMAT = -4,
    CCSDS_ERROR_CRC = -5,
    CCSDS_ERROR_UNSUPPORTED = -6,
    CCSDS_ERROR_CODEC = -7,
    CCSDS_ERROR_CALLBACK = -8
} ccsds_status_t;

const char *ccsds_status_string(ccsds_status_t status);

#endif
