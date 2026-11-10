#include "dsp.h"
#include <stdio.h>
#include <inttypes.h>
#include <math.h> 

int main(void) {

    // Test for dsp_init

    dsp_state_t test_struct = {0};
    
    int32_t init_status = dsp_init(&test_struct, 1000, 100, 5, 150);

    if (init_status != 0) {
        printf("Error Code: %" PRId32 "\n", init_status);
        return -1;
    }
    else {
        double total = 0.0;
        for (size_t i = 0; i < test_struct.tap_count; i++){
            total += test_struct.fir_coefficients[i]; 
        }
        printf("Total: %.9lf\n", total);

        if (!isfinite(total)) {
            printf("Total is not finite!\n");
            return -3;
        }

        double total_distance_from_one = fabs(1 - total);
        if (total_distance_from_one > 0.000001) {
            printf("Difference from one is too much!\n");
            return -2;
        }

        for (size_t i = 0; i < test_struct.tap_count / 2; i++){
            if (fabs(test_struct.fir_coefficients[i] - test_struct.fir_coefficients[test_struct.tap_count - 1 - i]) > 0.000001) {
                printf("Not symmetric!\n");
                return -4;
            }
        }

        dsp_state_t successful = test_struct;

        int32_t failed_init_status = dsp_init(&test_struct, 1000, 100, 4, 150);

        if (failed_init_status >= 0) {
            printf("Expected Negative Status: %" PRId32 "\n", failed_init_status);
            return -5;
        }

        if (test_struct.tap_count != successful.tap_count) {
            printf("Tap Count changed!\n");
            return -6;
        }
        if (test_struct.sample_rate_hz != successful.sample_rate_hz) {
            printf("Sample Rate changed!\n");
            return -7;
        }
        if (test_struct.bandwidth_hz != successful.bandwidth_hz) {
            printf("Bandwidth changed!\n");
            return -8;
        }
        if (test_struct.agc_gain != successful.agc_gain) {
            printf("AGC Gain changed!\n");
            return -9;
        }
        if (test_struct.shift_phase != successful.shift_phase) {
            printf("Shift Phase changed!\n");
            return -10;
        }
        for (size_t i = 0; i < DSP_MAX_TAPS; i++){
            if (test_struct.fir_coefficients[i] != successful.fir_coefficients[i]){
                printf("Coefficient changed at index %zu\n", i);
                return -11;
            }
            if (test_struct.history[i] != successful.history[i]){
                printf("History changed at index %zu\n", i);
                return -12;
            }
        }
        

        printf("Init Success!\n");

    }

    // Impulse test for dsp_process

    dsp_state_t impulse_test = {0};

    int32_t init_process_status = dsp_init(&impulse_test, 1000, 100, 5, 150);
    if (init_process_status != 0) return init_process_status;

    float complex test_samples[] = {1.0, 0, 0, 0, 0, 0};
    
    int32_t process_status = dsp_process(&impulse_test, test_samples, sizeof(test_samples) / sizeof(test_samples[0]));
    if (process_status != 0) {
        printf("Processing failed with error code: %" PRId32 "\n", process_status);
        return -13;
    }

    for (size_t i = 0; i < impulse_test.tap_count; i++){
        double difference_impulse = cabsf(test_samples[i] - impulse_test.fir_coefficients[i]);
        if (!isfinite(difference_impulse)){
            printf("diff is not finite. Index: %zu\n", i);
            return -14;
        } 
        if (difference_impulse > 0.000001) {
            printf("diff is too large. Index: %zu\n", i);
            return -15;
        }
    }
    if (!isfinite(cabsf(test_samples[5]))){
        printf("Index 5 is not finite\n");
        return -16;
    }
    if (cabsf(test_samples[5]) > 0.000001){
        printf("Index 5 is not 0\n");
        return -17;
    }

    printf("Impulse Test Succeed\n");

    // Separate blocks impulse test

    dsp_state_t sep_block_test_one = {0};
    dsp_state_t sep_block_test_two = {0};

    int32_t sep_block_init_status_one = dsp_init(&sep_block_test_one, 1000, 100, 5, 150);

    if (sep_block_init_status_one != 0){
        printf("Block 1 failed to initialize with code: %" PRId32 "\n", sep_block_init_status_one);
        return -18;
    }

    int32_t sep_block_init_status_two = dsp_init(&sep_block_test_two, 1000, 100, 5, 150);

    if (sep_block_init_status_two != 0){
        printf("Block 2 failed to initialize with code: %" PRId32 "\n", sep_block_init_status_two);
        return -19;
    }

    float complex sep_samples_one[] = {1.0, 0, 0, 0, 0, 0};
    float complex sep_samples_two[] = {1.0, 0, 0, 0, 0, 0};

    int32_t sep_block_process_status_one = dsp_process(&sep_block_test_one, sep_samples_one, sizeof(sep_samples_one) / sizeof(sep_samples_one[0]));

    if (sep_block_process_status_one != 0){
        printf("Block 1 failed to process with code: %" PRId32 "\n", sep_block_process_status_one);
        return -20;
    }

    int32_t sep_block_process_status_two_one = dsp_process(&sep_block_test_two, sep_samples_two, (sizeof(sep_samples_two) / sizeof(sep_samples_two[0])) / 2);

    if (sep_block_process_status_two_one != 0){
        printf("Block 2, section 1 failed to process with code: %" PRId32 "\n", sep_block_process_status_two_one);
        return -21;
    }

    int32_t sep_block_process_status_two_two = dsp_process(&sep_block_test_two, &sep_samples_two[(sizeof(sep_samples_two) / sizeof(sep_samples_two[0])) / 2], (sizeof(sep_samples_two) / sizeof(sep_samples_two[0])) / 2);

    if (sep_block_process_status_two_two != 0){
        printf("Block 2, section 2 failed to process with code: %" PRId32 "\n", sep_block_process_status_two_two);
        return -22;
    }

    for (size_t i = 0; i < 6; i++){
        if (sep_samples_one[i] != sep_samples_two[i]){
            printf("Blocks don't match!\n");
            return -23;
        }
    }

    printf("Separate Blocks test succeeded!\n");

    // Complex Impulse Test

    dsp_state_t complex_impulse = {0};
    float complex complex_samples[] = {1 + 2 * _Complex_I, 0, 0, 0, 0, 0};

    int32_t complex_init_status = dsp_init(&complex_impulse, 1000, 100, 5, 150);

    if (complex_init_status != 0){
        printf("Complex init failed with error code: %" PRId32 "\n", complex_init_status);
        return -25;
    }

    int32_t complex_process_status = dsp_process(&complex_impulse, complex_samples, sizeof(complex_samples) / sizeof(complex_samples[0]));

    if (complex_process_status != 0){
        printf("Complex process failed with error code: %" PRId32 "\n", complex_process_status);
        return -27;
    }

    for (size_t i = 0; i < complex_impulse.tap_count; i++){
        float complex expected_value = complex_impulse.fir_coefficients[i] * (1 + 2 * _Complex_I);
        float actual_vs_expected_diff = cabsf(complex_samples[i] - expected_value);
        if (!isfinite(actual_vs_expected_diff)){
            printf("Actual vs expected diff is not finite\n");
            return -28;
        }
        if (actual_vs_expected_diff > 0.000001) {
            printf("Actual vs expected diff is too large\n");
            return -29;
        }
        
    }
    if (!isfinite(cabsf(complex_samples[5]))){
        printf("Index 5 is not finite\n");
        return -30;
    }
    if (cabsf(complex_samples[5]) > 0.000001){
        printf("Index 5 is not 0\n");
        return -31;
    }

    printf("Complex impulse test passed\n");

    // Tone test 50hz

    dsp_state_t tone_test = {0};
    int32_t tone_test_init_status = dsp_init(&tone_test, 1000, 100, 65, 150);
    if (tone_test_init_status != 0){
        printf("Tone test init failed with code: %" PRId32 "\n", tone_test_init_status);
        return -32;
    }
    float complex tone_test_samples[1000];

    const double pi = 3.141592653589793;
    float tone_test_frequency_hz = 50;

    for (size_t i = 0; i < 1000; i++){
        double tone_test_angle = 2 * pi * tone_test_frequency_hz * i / 1000;
        tone_test_samples[i] = cos(tone_test_angle) + _Complex_I * sin(tone_test_angle);
    }

    int32_t tone_test_process_status = dsp_process(&tone_test, tone_test_samples, 1000);

    if (tone_test_process_status != 0){
        printf("Tone test process failed with code: %" PRId32 "\n", tone_test_process_status);
        return -33;
    }

    double tone_test_sum = 0;

    for (size_t i = tone_test.tap_count - 1; i < 1000; i++){
        double tone_test_magnitude = cabsf(tone_test_samples[i]);
        if (!isfinite(tone_test_magnitude)) {
            printf("Tone test magnitude is not finite!\n");
            return -34;
        }
        tone_test_sum += tone_test_magnitude;
    }

    double tone_test_average = tone_test_sum / (1000 - (tone_test.tap_count - 1));
    if (!isfinite(tone_test_average)) {
        printf("Tone test average is not finite\n");
        return -35;
    }

    printf("Average for tone test: %lf\n", tone_test_average);

    if (fabs(tone_test_average - 1) > 0.01) {
        printf("Average is too far from one\n");
        return -36;
    }

    printf("Tone test 50hz success\n");

    // Tone Test 300hz

    dsp_state_t tone_test_300 = {0};
    int32_t tone_test_300_init_status = dsp_init(&tone_test_300, 1000, 100, 65, 150);
    if (tone_test_300_init_status != 0){
        printf("Tone test 300 hz init failed with code: %" PRId32 "\n", tone_test_300_init_status);
        return -37;
    }
    float complex tone_test_300_samples[1000];

    float tone_test_300_frequency_hz = 300;

    for (size_t i = 0; i < 1000; i++){
        double tone_test_300_angle = 2 * pi * tone_test_300_frequency_hz * i / 1000;
        tone_test_300_samples[i] = cos(tone_test_300_angle) + _Complex_I * sin(tone_test_300_angle);
    }

    int32_t tone_test_300_process_status = dsp_process(&tone_test_300, tone_test_300_samples, 1000);

    if (tone_test_300_process_status != 0){
        printf("Tone test 300 hz process failed with code: %" PRId32 "\n", tone_test_300_process_status);
        return -38;
    }

    double tone_test_300_sum = 0;

    for (size_t i = tone_test_300.tap_count - 1; i < 1000; i++){
        double tone_test_300_magnitude = cabsf(tone_test_300_samples[i]);
        if (!isfinite(tone_test_300_magnitude)) {
            printf("Tone test 300 hz magnitude is not finite!\n");
            return -39;
        }
        tone_test_300_sum += tone_test_300_magnitude;
    }

    double tone_test_300_average = tone_test_300_sum / (1000 - (tone_test_300.tap_count - 1));
    if (!isfinite(tone_test_300_average)) {
        printf("Tone test 300 hz average is not finite\n");
        return -40;
    }

    printf("Average for tone test 300 hz: %lf\n", tone_test_300_average);

    if (tone_test_300_average > 0.01) {
        printf("Tone test 300 hz average is bigger than 0.01\n");
        return -41;
    }

    printf("Tone test 300hz success\n");

    return 0;

}

