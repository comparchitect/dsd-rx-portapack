#!/usr/bin/env bash
set -euo pipefail

# Fetch mbelib, verify a tag, and stage sources for MBELIB baseband patching.
# This script does NOT enable mbelib by default; it just prepares sources under tools/mbelib_work.

ROOT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="${ROOT_DIR}/tools/mbelib_work"
MBELIB_REPO="https://github.com/szechyjs/mbelib.git"
MBELIB_TAG="${MBELIB_TAG:-v1.3.0}"

echo "Staging mbelib into ${WORK_DIR} (tag: ${MBELIB_TAG})"
rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}"

git clone --depth 1 --branch "${MBELIB_TAG}" "${MBELIB_REPO}" "${WORK_DIR}/mbelib"

echo "Copying mbelib sources..."
mkdir -p "${WORK_DIR}/src"
cp "${WORK_DIR}/mbelib"/{mbelib.c,mbelib.h,mbelib_const.h} "${WORK_DIR}/src/"
cp -r "${WORK_DIR}/mbelib"/*.c "${WORK_DIR}/src/" || true

cat >"${WORK_DIR}/README.txt" <<'EOF'
This folder contains mbelib sources fetched locally by tools/get-mbelib.sh.
They are NOT redistributed with the firmware. Use them only for local builds/testing.

To enable mbelib for MBELIB:
- Add these sources to the MBELIB baseband build (proc_mbelib_decode) and define WITH_MBELIB=1.
- Rebuild the external MBELIB baseband (mbelib_decode.m4b) and MBELIB.ppma.
- Do not redistribute mbelib without complying with its license.
EOF

echo "Done. Sources staged in ${WORK_DIR}/src. Manually wire them into the MBELIB build with WITH_MBELIB=1."
