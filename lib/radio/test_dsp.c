#include "dsp.h"
#include <stdio.h>
#include <inttypes.h>
#include <math.h> 

int main(void) {

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

    dsp_state_t impulse_test = {0};

    int32_t int_process_status = dsp_init(&impulse_test, 1000, 100, 5, 150);
    if (int_process_status != 0) return int_process_status;

    float complex test_samples[] = {1.0, 0, 0, 0, 0, 0};
    
    int32_t process_status = dsp_process(&impulse_test, test_samples, sizeof(test_samples) / sizeof(test_samples[0]));

    for (size_t i = 0; i < impulse_test.tap_count; i++){
        
    }

}

