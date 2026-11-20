# Ground Station Code
The ground station code will reside on a normal laptop and must control the antennae, but also uplink information. Receiving info is just as crucial.


## November 6 Phase (done)
### Ground Station Main
- [x] Radio Connection (Handled by a simple library)
- [x] SSDV Decoding
- [x] CCSDS Encoding / Decoding
- [x] NNG Send / Recv 
- [ ] Packet Police
- [x] Function Stubs - Recv
- [x] Fill in Stub Implementations - Recv
- [x] Function Stubs - Send
- [x] Fill in Stub Implementations - Send

### Ground Station UI
- [x] Install civetweb
- [x] NNG Send / Recv
- [x] Civetweb stuff
- [x] Function Stubs
- [x] Fill in Stub Implementations
- [ ] HTML File Auto-embedder
- [ ] The Eyecandy

### Ground Station Backup
- [x] NNG Send / Recv
- [x] Pretty printing to a file
- [ ] Tar Zipping
- [x] Uploading old ones to internet
- [ ] Crash Recovery
- [x] Function Stubs
- [x] Fill in Stub Implementations

## December 20 Phase
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
- Actual satellite and space stuff: "TT&C"
    - [ ] telemetry (downlink)
        - [ ] recieving (radio) - integrate avi filter?
        - [ ] parsing/decoding
            - [ ] figure out wtf packet police is
        - [ ] control center (website)
            - [ ] learn how tf to use html
            - [ ] display jpeg images
            - [ ] human made ui
            - [ ] crash recovery
                - [ ] decompression as well once compression in backup system
        - [ ] storage (backup)
            - [ ] compression of files before storage
            - [ ] look into s3
            - [ ] timings
                - save to hard drive when satellite in view (actively recieving)
                - upload to internet when satellite out of range
    - [ ] tracking - like obc? how tf we doin this? computation or camera or what
        - [ ] position - how tf
        - [ ] distance - radio time to send maybe?
        - [ ] speed - doppler shift says gemini?
        - [ ] direction - onboard star tracker?
            - actually can star tracker solve all these problems?
            - if so is all the info just coming down with the telemetry?
            - i sure hope so i dont want to do all this
        - [ ] figure out when satellite in range and not (see downlink:storage)
    - [ ] command (uplink)
        - [x] control center (website)
        - [x] encoding
        - [x] sending (radio)
        - surely theres more than that
    - [ ] monitioring (track satellite health)
        - would assume the information is in telemetry
        - [ ] research what an acds telemetry packet looks like
    - look at build a cubesat for research?
- Mission stuff
    - "PDT"? look into this
    - commands? is flight software ppl working on this or should i?
        - [ ] acds commands (unless we're letting snn ai do it)
        - [ ] thermals commands
        - [ ] power commands
        - [ ] mission commands (obc)
        - [ ] updating onboard software? mission isnt that long i think so probably not
- Old tasks from nov 6 phase
    - [ ] Main: Packet Police
        - so theres a branch called "packet police" in github, gonna check that mayb
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
- [ ] merge wesley's flight integration
- [ ] merge backup (?)