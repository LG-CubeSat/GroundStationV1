#include "dsp.h"
#include <math.h>

static int32_t dsp_design_lowpass(float filter_weights[], size_t weight_capacity, size_t weight_count, double sample_rate_hz, double cutoff_frequency_hz){
    if (filter_weights == NULL) return -1;
    if (weight_count % 2 == 0 || weight_count < 3) return -2;
    if (weight_count > weight_capacity) return -3;
    if (!isfinite(sample_rate_hz) || !isfinite(cutoff_frequency_hz)) return -4;
    if (sample_rate_hz <= 0 || !(cutoff_frequency_hz > 0 && cutoff_frequency_hz < (sample_rate_hz / 2))) return -5;

    double normalized_cutoff = cutoff_frequency_hz / sample_rate_hz;
    size_t middle_index = (weight_count - 1) / 2;
    double running_sum = 0;

    const double pi = 3.141592653589793;

    for (size_t i = 0; i < weight_count; i++){
        double distance_from_middle = (double)i - (double)middle_index;
        double ideal_weight;

        if (i == middle_index) ideal_weight = 2.0 * normalized_cutoff;
        else ideal_weight = sin(2.0 * pi * normalized_cutoff * distance_from_middle) / (pi * distance_from_middle);
        double window = 0.54 - 0.46 * cos(2 * pi * (double)i / ((double)weight_count - 1));

        filter_weights[i] = (float)(ideal_weight * window);
        running_sum += filter_weights[i];
    }

    if (!isfinite(running_sum)) return -6;
    if (running_sum == 0) return -7;

    for (size_t i = 0; i < weight_count; i++){
        filter_weights[i] /= running_sum;
    }
    
    return 0;
}

int32_t dsp_init(dsp_state_t *state, double sample_rate_hz, double bandwidth_hz, size_t tap_count, double cutoff_frequency_hz){

    if (state == NULL) return -1;
    if (!isfinite(sample_rate_hz) || !isfinite(bandwidth_hz)) return -2;
    if (sample_rate_hz <= 0 || bandwidth_hz <= 0) return -3;
    if (bandwidth_hz >= sample_rate_hz) return -4;

    dsp_state_t prepared = {0};
    prepared.sample_rate_hz = sample_rate_hz;
    prepared.bandwidth_hz = bandwidth_hz;
    prepared.agc_gain = 1.0F;
    
    int32_t status = dsp_design_lowpass(prepared.fir_coefficients, DSP_MAX_TAPS, tap_count, sample_rate_hz, cutoff_frequency_hz);
    if (status != 0) return status;
    prepared.tap_count = tap_count;
    *state = prepared;

    return 0; 

}

int32_t dsp_process(dsp_state_t *state, float complex *samples, size_t count){

    if (state == NULL) return -1;
    if (state->tap_count < 3 || state->tap_count > DSP_MAX_TAPS || state->tap_count % 2 == 0) return -2;
    if (count == 0) return 0;
    if (samples == NULL) return -3;

    for (size_t i = 0; i < count; i++){
        for (size_t j = (state->tap_count - 1); j > 0; j--){
            state->history[j] = state->history[j - 1];
        }
        state->history[0] = samples[i];

        float complex accumulator = 0;
        for (size_t j = 0; j < state->tap_count; j++){
            accumulator += state->fir_coefficients[j] * state->history[j];
        }

        samples[i] = accumulator;

    }

    return 0;

}

int32_t dsp_shift(dsp_state_t *state, float complex *samples, size_t count, float frequency_offset_hz){

    if (state == NULL) return -1;
    if (!isfinite(state->sample_rate_hz)) return -2;
    if (state->sample_rate_hz <= 0) return -3;
    if (!isfinite(frequency_offset_hz)) return -4;
    if (!isfinite(state->shift_phase)) return -5;
    if (count == 0) return 0;
    if (samples == NULL) return -6;

    const double pi = 3.141592653589793;

    double reduced_offset = remainder(frequency_offset_hz, state->sample_rate_hz);
    double phase_increment = (reduced_offset / state->sample_rate_hz) * 2 * pi;

    for (size_t i = 0; i < count; i++){
        float complex rotation = cos(state->shift_phase) - _Complex_I * sin(state->shift_phase);
        samples[i] *= rotation;
        state->shift_phase += phase_increment;
        state->shift_phase = remainder(state->shift_phase, 2 * pi);
    }

    return 0;

}
