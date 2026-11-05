#include "dsp.h"
#include <math.h>

int32_t dsp_init(dsp_state_t *state, double sample_rate_hz, double bandwidth_hz){

    if (state == NULL) return -1;
    if (!isfinite(sample_rate_hz) || !isfinite(bandwidth_hz)) return -2;
    if (sample_rate_hz <= 0 || bandwidth_hz <= 0) return -3;
    if (bandwidth_hz >= sample_rate_hz) return -4;

    dsp_state_t prepared = {0};
    prepared.sample_rate_hz = sample_rate_hz;
    prepared.bandwidth_hz = bandwidth_hz;
    prepared.agc_gain = 1.0F;
    

}
