# MBELIB (external)

- Offline AMBE decoder packaged as a `.ppma` external app (menu: Utilities). Baseband (PA2D) is bundled and loaded into RAM via the package; no SPI flash tag required.
- Licensing: mbelib is **not** shipped. The included baseband is stubbed; users must fetch/patch mbelib themselves if they want to see how decoding could work.
- Install: copy `MBELIB.ppma` and `mbelib_decode.m4b` from `sdcard/APPS/` to your SD card `APPS/` folder. The loader copies the baseband into RAM when launching.
- Attribution: AMBE burst handling follows algorithms derived from `szechyjs/dsd` (GitHub).
- Failure modes: if the external baseband is missing or stubbed, decoding will not produce audio. The UI shows the placeholder status text to remind users that an external baseband is required.

## Optional and for educational purposes: fetch and patch mbelib

You are responsible for obtaining and complying with mbelib’s license. The repo does not distribute mbelib. If you want to experiment locally:

1) From the repo root, run the helper script (example):
```
tools/get-mbelib.sh
```
This should clone/fetch `szechyjs/mbelib`, verify the revision, and copy only the needed sources/headers into a local scratch area (e.g. `external/mbelib_work/`).

2) Apply the patch step to replace the MBELIB stubs with the mbelib decoder. A helper script exists:
   - Run `tools/get-mbelib.sh` (optionally `MBELIB_TAG=v1.3.0` override) to fetch mbelib into `tools/mbelib_work/src/`.
   - Wire those sources into the MBELIB baseband build:
     * In your local CMake (baseband side), define `WITH_MBELIB=1` and add the mbelib sources from `tools/mbelib_work/src/` to the `proc_mbelib_decode` target’s source list.
     * Add the mbelib include path (the same `tools/mbelib_work/src/`) to that target.
   - Rebuild the external MBELIB baseband (`mbelib_decode.m4b`) and the `.ppma`.

3) Re-run packaging (e.g. `tools/package_dsd_mbelib.sh`) to refresh `mbelib_decode.m4b` and `MBELIB.ppma`, then copy to SD.

If the helper scripts are absent in your checkout, mimic the above manually: fetch mbelib, wire its sources into `proc_mbelib_decode`, rebuild the baseband, and repackage. Always keep the stubbed version as default for redistribution.
