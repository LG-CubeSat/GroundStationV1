# Ground Station Code
The ground station code will reside on a normal laptop and must control the antennae, but also uplink information. Receiving info is just as crucial.

### Ground Station Main
- [x] Radio Connection (Handled by a simple library)
- [x] SSDV Decoding
- [ ] CCSDS Decoding
- [ ] NNG Send / Recv 
- [ ] CBOR Encoding / Decoding
- [ ] Bringing it all together

### Ground Station UI
- [ ] NNG Send / Recv
- [ ] CBOR Encoding / Decoding 
- [ ] The Eyecandy

### Ground Station Backup
- [ ] NNG Send / Recv
- [ ] CBOR Encoding / Decoding
- [ ] Pretty printing to a file
- [ ] Tar Zipping
- [ ] Uploading old ones to internet
- [ ] Crash Recovery

## SSDV
Currently, we have a extra sloppity-slop library called simpl_ssdv.h, which is for simplifying ssdv use. The main ssdv file is a decoder that parses a file, but in the futre the file part will get chopped off and it will get the data from the radio. SSDVEncoder is just a sloppity-slop test thing for SSDV. WILL be deleted in the future.
