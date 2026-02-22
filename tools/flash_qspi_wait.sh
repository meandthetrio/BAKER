#!/usr/bin/env bash
set -euo pipefail

VIDPID="0483:df11"

echo "Waiting for Daisy in DFU mode (USB ${VIDPID})..."
echo "Tip: Hold BOOT, tap RESET, then release BOOT."

while true; do
  if dfu-util -l 2>/dev/null | grep -qi "${VIDPID}"; then
    echo "DFU device found. Flashing to QSPI..."
    break
  fi
  sleep 0.25
done

make program-dfu
echo "Done."
