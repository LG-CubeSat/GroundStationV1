# OLIVERS PERSONAL CHECKLIST
### goals (wesley and his claude)
- [ ] Integrate Avi’s LPF, but modularly so it works with many GS Radios
    - Oliver notes: ask radio software people abt this, also "modularly" is a vague term
- [ ] Improve data storage (Isn’t it just a string log) add a parsing thread or something
    - Oliver notes: look into s3 and other storage
- [ ] Research what full satellite ground station software would look like.
    - Oliver notes: thats very broad
- [ ] In real GS, are there more parts? if there’s a big dish how do we change SW, etc
    - Oliver notes: idfk what im supposed to do here. dont we just need one radio? and havent the radio hardware guys worked really hard on it already?
- [ ] Make changes to improve system accordingly
    - Oliver notes: yes obviously
- [ ] Update the command controls to follow
    - Oliver notes: wtf does that mean

### goals (revised)
- What does a ground station do? "TT&C"
    - [ ] telemetry (downlink)
        - [ ] recieving (radio)
            - [ ] integrate avi filter?
                - [ ] ask avi if its in the radio alr
        - [ ] parsing/decoding
            - [ ] figure out wtf packet police is
        - [ ] control center (website)
            - [x] learn how tf to use html
            - [x] build html from external file instead of giant string
            - [ ] human made ui
            - [ ] display jpeg images
            - [ ] crash recovery
                - [ ] decompression as well once compression in backup system
            - [x] research what an acds telemetry packet looks like
                - found it
                    uint64_t received_unix_us;
                    uint64_t board_monotonic_us;
                    uint32_t sequence;
                    uint32_t sensor_valid_mask;
                    uint32_t active_faults;
                    uint8_t mode;
                    uint8_t attitude_valid;
                    uint8_t control_flags;
                    uint8_t health_flags;
                    float quaternion[4];
                    float target_quaternion[4];
                    float estimated_rate_rad_s[3];
                    float gyro_bias_rad_s[3];
                    float magnetic_field_t[3];
                    float sun_vector_body[3];
                    float sun_irradiance_w_m2;
                    float confidence;
                    float requested_torque_nm[3];
                    float requested_dipole_a_m2[3];
                    float achievable_torque_nm[3];
                    float pointing_error_rad;
                    uint32_t rejected_commands;
                    uint32_t dropped_messages;
                    uint32_t estimator_resets;
                    uint32_t controller_errors;
                    uint32_t mode_transitions;
                    float minimum_stack_margin_words;
            - [x] put ALL this information on website
                - wesley and/or claude alr did apparently
        - [ ] storage (backup)
            - [x] rework storage to use flight_protocol over serial
                - [x] able to recv acds telemetry and cmd result
                - [x] store with separators - uint8 to hex surrounded with curly braces
            - [ ] look into s3
            - [ ] timings (see tracking)
                - save to hard drive when satellite in view (actively recieving)
                - upload to internet when satellite out of range
            - [ ] compression of files before storage
                - upload in chunks (see timings)
    - [ ] tracking - like obc? how tf we doin this? computation or camera or what
        - [ ] position - how tf
        - [ ] distance - radio time to send maybe?
        - [ ] speed - doppler shift says gemini?
        - [ ] direction - onboard star tracker?
            - actually can star tracker solve all these problems?
            - if so is all the info just coming down with the telemetry?
            - i sure hope so i dont want to do all this
        - [ ] figure out when satellite in range and not, communicate to backup
        - [ ] physical movement - need hardware to point antenna toward satellite
    - [x] command (uplink)
        - [x] control center (website)
        - [x] encoding
        - [x] sending (radio)
        - surely theres more than that
    - [ ] monitioring (track satellite health)
        - would assume the information is in telemetry
    - look at build a cubesat for research?
- Old tasks from nov 6 phase
    - [ ] Main: Packet Police
        - so theres a branch called "packet police" in github, gonna check that mayb
        - so apparently its kinda integrated in main.c but maybe check through everything it does and make sure its working
    - [ ] Web: File Auto-embedder
        - wtf does that mean? is this displaying jpeg?
    - [ ] Web: The Eyecandy
        - so basically a better ui
        - i gotta learn to write in html and improve code surrounding the index_html in web.c
    - [ ] Web: Crash Recovery
        - the web on startup needs to pull from backup storage (s3?)
        - originally in backup checklist instead of web? maayyybe look into that
    - [ ] Backup: Compression
        - shouldnt be too hard
    - [x] merge wesley's flight integration
    - [x] merge backup