#!/usr/bin/env bash
set -euo pipefail

# Simple helper to package external DSD RX and MBELIB apps plus their basebands.
# Assumes you've already run the standard build (cmake/ninja or make) so that
# application.elf, *.ppma, and baseband bins exist under build/firmware/.

ROOT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/firmware"
APP_BIN_DIR="${BUILD_DIR}/application"
SDCARD_DIR="${ROOT_DIR}/sdcard/APPS"

mkdir -p "${SDCARD_DIR}"

copy_artifact() {
    local src="$1"
    local dest="$2"
    if [[ -f "${src}" ]]; then
        cp "${src}" "${dest}"
        echo "Copied $(basename "${src}") -> ${dest}"
    else
        echo "WARNING: Missing artifact ${src}" >&2
    fi
}

copy_artifact "${APP_BIN_DIR}/dsdrx.ppma" "${SDCARD_DIR}/DSDRX.ppma"
copy_artifact "${APP_BIN_DIR}/mbelib.ppma" "${SDCARD_DIR}/MBELIB.ppma"
copy_artifact "${APP_BIN_DIR}/dsd_rx.m4b" "${SDCARD_DIR}/"
copy_artifact "${APP_BIN_DIR}/mbelib_decode.m4b" "${SDCARD_DIR}/"

echo "Done. SD card payload in ${SDCARD_DIR}"
