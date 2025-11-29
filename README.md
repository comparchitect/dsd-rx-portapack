Capturing and listening to DMR voice data on the Portapack

This repository contains 2 apps for the Portapack mayhem-firmware:
- DSD RX: captures DMR voice data to a .ambe file in the SD card's CAPTURES/ directory
- (for educational purposes only) MBELIB: if built with https://github.com/szechyjs/mbelib it decodes a .ambe file on the SD card to a .wav file

DSD RX

This is effectively a slimmed down port of https://github.com/szechyjs/dsd that only currently supports DMR. It should be relatively easy to create more basebands that could perform the same voice data extractions for other protocols like NXDN. DSD RX is a simple app: it tunes to a frequency specificed by the user and extracts any DMR voice packets if the "Log to SD" checkbox is checked.

MBELIB

This app can open the .ambe files stored by the DSD RX app and decode them into audible .wav files. The app can also play the .wav files. Main message here is that this is just a proof of concept. The decoding is VERY slow on the H4M hardware. It can take 1 minute to decode 10 seconds of audio. With more powerful hardware (more cores, higher clock speed) it may be possible to decode digital voice modes live. But with the current hardware it is not. Also, please note that the codec used by DMR and other digital voice modes is proprietary so any use beyond educational purposes may be prohibited.

A better way to do this with the current hardware and while respecting licensing:
1. create a GPIO extension board for the H4M that has a DVSI AMBE 3000 or AMBE 4000 chip on board
2. modify the DSD RX app so it sends the DMR voice packets via GPIO to the DVSI chip
3. DVSI chip decodes the audio and sends back audible PCM / WAV data in real time
4. I researched this option lightly and found that the H4M GPIO only supports I2C so limited bandwidth may make it impossible to listen to audio real-time even while using a DVSI chip via GPIO
