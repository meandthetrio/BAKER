#!/usr/bin/env bash
set -euo pipefail

VIDPID="0483:df11"

echo "READY TO FLASH QSPI (USB ${VIDPID})..."
echo "1) Do NOT hold BOOT."
echo "2) Tap RESET once NOW (or double-tap if needed)."
echo "Waiting for DFU device to appear..."

while true; do
  if dfu-util -l 2>/dev/null | grep -qi "${VIDPID}"; then
    echo "DFU device found. Flashing to QSPI..."
    break
  fi
  sleep 0.25
done

make program-dfu
echo "Done."
