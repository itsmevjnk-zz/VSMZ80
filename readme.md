# VSMZ80 - Cycle accurate Z80 for Proteus

This is a model for emulating the Z80 CPU inside Labcenter Proteus.
It is a work in progress to emulate the Z80 CPU inside Proteus and allow for the creating of virtual Z80 computers.
This was originally created by MockbaTheBorg in 2016, and work on it is now continued by me (itsmevjnk).

## Functionality

Right now, the model supports:
- Unprefixed opcode set (partial)
- CB prefixed opcode set
- ED prefixed opcode set (partial)
- CPU reset
- Memory and I/O read/write
- Flags

These features are not working and will be added in the future:
- Interrupt (INT and NMI)
- Wait states
- DMA (BUSRQ/BUSACK)

Any contribution to implement these features/improve existing ones is highly appreciated.

## Building and installing

To build, just open the project on VS2015 and hit "Build". It should build with no errors.
For 32-bit Proteus installations, you MUST compile with the Win32 configuration, otherwise Proteus will error out.  
To install the model, copy the files in the LIBRARY directory in this repo to your Proteus installation's LIBRARY directory, and copy the built VSMZ80.DLL file in the Debug/Release directory (depending on your configuration) to your Proteus installation's MODELS directory.

## Credits

This project was made possible by:
- [MockbaTheBorg](https://github.com/MockbaTheBorg) (for providing the original code)
- Cristian Dinu (for providing [the instruction decoding algorithm used in the model](http://z80.info/decoding.htm))
- Sean Young (for providing [information on Z80 flags](http://www.z80.info/z80sflag.htm))
- ClrHome (for providing [information on Z80 clock cycles](http://clrhome.org/table/))
