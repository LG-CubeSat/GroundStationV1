# Ground Station Code
The ground station code will reside on a normal laptop and must control the antennae, but also uplink information. Receiving info is just as crucial.

- [ ] Receive data from satellite (via antenna)
- - [ / ] Image Decoding - ARIN
- - [ ] Actual Antenna Recv stuff - TBD
- [ ] Uplink specific commands (via antenna)
- - [ ] Command Encoding / Decoding - OLIVER
- - [ ] Actual Antenna Send stuff - TBD
- [ ] Show result on a website - ARIN

## SSDV
Currently, we have a extra sloppity-slop library called simpl_ssdv.h, which is for simplifying ssdv use. The main ssdv file is a decoder that parses a file, but in the futre the file part will get chopped off and it will get the data from the radio. SSDVEncoder is just a sloppity-slop test thing for SSDV. WILL be deleted in the future.
