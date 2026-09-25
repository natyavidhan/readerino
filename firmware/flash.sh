#!/usr/bin/env bash
# Builds readerino_nano and uploads it to the Nano -- but only if the build
# succeeded and the program ends below the bootloader. (An oversized build
# once got uploaded and overwrote an unprotected bootloader; the bootloader
# is write-protected now, but don't rely on that.)
#
#   ./flash.sh            # build + upload
#   ./flash.sh --check    # build + size check only
set -euo pipefail
cd "$(dirname "$0")"

FQBN=arduino:avr:nano:cpu=atmega328old # clone Nanos with the old bootloader
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

arduino-cli compile --fqbn "$FQBN" --library local_libraries/SD --build-path "$BUILD" readerino_nano

# The bootloader occupies 0x7800-0x7FFF; the program must end before it.
python3 - "$BUILD/readerino_nano.ino.hex" <<'EOF'
import sys
top = 0
for line in open(sys.argv[1]):
    n, addr, typ = int(line[1:3], 16), int(line[3:7], 16), int(line[7:9], 16)
    if typ == 0 and n:
        top = max(top, addr + n)
print(f"program ends at 0x{top:04X} ({0x7800 - top} bytes below the bootloader)")
sys.exit(0 if top <= 0x7800 else 1)
EOF

[ "${1:-}" = "--check" ] && exit 0

# The Nano's FT232R shows up as a different /dev/ttyUSB* after re-plugging.
PORT=""
for d in /dev/ttyUSB*; do
  if udevadm info -q property -n "$d" 2>/dev/null | grep -q "ID_MODEL=FT232R"; then PORT=$d; break; fi
done
[ -n "$PORT" ] || { echo "Nano (FT232R) not found on any /dev/ttyUSB*" >&2; exit 1; }

echo "uploading to $PORT"
arduino-cli upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD" readerino_nano
