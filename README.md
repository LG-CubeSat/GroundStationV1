# Ground Station Code
The ground station code will reside on a normal laptop and must control the antennae, but also uplink information. Receiving info is just as crucial.

### Ground Station Main
- [x] Radio Connection (Handled by a simple library)
- [x] SSDV Decoding
- [x] CCSDS Encoding / Decoding
- [x] NNG Send / Recv 
- [ ] Packet Police
- [x] Function Stubs - Recv
- [x] Fill in Stub Implementations - Recv
- [ ] Function Stubs - Send
- [ ] Fill in Stub Implementations - Send

### Ground Station UI
- [ ] Install civetweb
- [ ] NNG Send / Recv
- [ ] Civetweb stuff
- [ ] Function Stubs
- [ ] Fill in Stub Implementations
- [ ] The Eyecandy

### Ground Station Backup
- [ ] NNG Send / Recv
- [ ] CBOR Encoding / Decoding
- [ ] Pretty printing to a file
- [ ] Tar Zipping
- [ ] Uploading old ones to internet
- [ ] Crash Recovery
- [ ] Function Stubs
- [ ] Fill in Stub Implementations

## SSDV
Currently, we have a extra sloppity-slop library called simpl_ssdv.h, which is for simplifying ssdv use. The main ssdv file is a decoder that parses a file, but in the futre the file part will get chopped off and it will get the data from the radio. SSDVEncoder is just a sloppity-slop test thing for SSDV. WILL be deleted in the future.
