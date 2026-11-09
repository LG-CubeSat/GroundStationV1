#ifndef DSP_H
#define DSP_H

/*
Code for signal processing for the radio.
Pure math, no hardware
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