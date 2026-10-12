#include <stdio.h>
#include <stdlib.h>
#include "simpl_SSDV.h"
#include "radio.h"

simpl_ssdv_buffer parse_ssdv_raw(const uint8_t* buffer, int length);
int save_ssdv_image(const char* path, simpl_ssdv_buffer image, int length);
void serialize_ccsds(); // Todo args and return type
void deserialize_ccsds(); // Todo args and return type

int main(void) {
    printf("Hello World!\n");
    return 0;
}
