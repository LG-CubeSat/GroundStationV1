#ifndef DSP_H
#define DSP_H

/*
Code for signal processing for the radio.
Pure math, no hardware (doesn't connect to real radio yet)

Units: Hz, radians, and complex samples—not bytes.
Zero means success; negative values mean errors.
dsp_init must succeed before processing.
Processing modifies the supplied array.
State must persist between blocks.
One state belongs to one stream; simultaneous calls on the same state are not supported.
Positive shift offsets move frequencies downward.
Bandwidth_hz currently only stores information, it does not automatically choose or verify the filter’s passband.

*/

#include <complex.h>
#include <stdint.h>
#include <stddef.h>

#define DSP_MAX_TAPS 129

typedef struct {                                
    double sample_rate_hz;                                      
    double bandwidth_hz;
    float fir_coefficients[DSP_MAX_TAPS];
    size_t tap_count;
    float complex history[DSP_MAX_TAPS];
    float agc_gain;
    double shift_phase;
} dsp_state_t;

int32_t dsp_init(dsp_state_t *state, double sample_rate_hz, double bandwidth_hz, size_t tap_count, double cutoff_frequency_hz);
int32_t dsp_process(dsp_state_t *state, float complex *samples, size_t count);
int32_t dsp_shift(dsp_state_t *state, float complex *samples, size_t count, float frequency_offset_hz);

#endif