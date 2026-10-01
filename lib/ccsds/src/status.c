#include "ccsds/status.h"

const char *ccsds_status_string(ccsds_status_t status)
{
    switch (status) {
        case CCSDS_OK:
            return "ok";
        case CCSDS_ERROR_NULL:
            return "null pointer";
        case CCSDS_ERROR_RANGE:
            return "value out of range";
        case CCSDS_ERROR_BUFFER_TOO_SMALL:
            return "buffer too small";
        case CCSDS_ERROR_INVALID_FORMAT:
            return "invalid format";
        case CCSDS_ERROR_CRC:
            return "CRC mismatch";
        case CCSDS_ERROR_UNSUPPORTED:
            return "unsupported option";
        case CCSDS_ERROR_CODEC:
            return "compression codec error";
        case CCSDS_ERROR_CALLBACK:
            return "packet callback failed";
        default:
            return "unknown CCSDS status";
    }
}
