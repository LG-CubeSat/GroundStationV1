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

typedef enum {

    DSP_OK = 0,
    DSP_ERROR_NULL = -1,
    DSP_ERROR_RANGE = -2,
    DSP_ERROR_NONFINITE = -3,
    DSP_ERROR_FILTER_DESIGN = -4,

} error_status_t;

typedef struct {                                
    double sample_rate_hz;                                      
    double bandwidth_hz;
    float fir_coefficients[DSP_MAX_TAPS];  // filter weights
    size_t tap_count;  // number of weights in filter
    float complex history[DSP_MAX_TAPS];  // sample history
    float agc_gain;
    double shift_phase;  
} dsp_state_t;

int32_t dsp_init(dsp_state_t *state, double sample_rate_hz, double bandwidth_hz, size_t tap_count, double cutoff_frequency_hz); // Initialize dsp state and generate weights
int32_t dsp_process(dsp_state_t *state, float complex *samples, size_t count); // Process samples with generated weights
int32_t dsp_shift(dsp_state_t *state, float complex *samples, size_t count, float frequency_offset_hz); // Correct frequency offsets

#endif