# DSD RX (external)

- Lives as a `.ppma` external app (menu: RX). Baseband (PDSD) is bundled and loaded into RAM via the package; no SPI flash tag required.
- Credits: algorithms derived from `szechyjs/dsd` (GitHub).
- Install: copy `DSDRX.ppma` and `dsd_rx.m4b` from `sdcard/APPS/` to your SD card `APPS/` folder. The loader handles placing the baseband in RAM.
- Dependencies: none beyond the standard firmware. DSD RX emits AMBE bursts in a .ambe file stored in the SD card's CAPTURES folder for decoding with the MBELIB app.
