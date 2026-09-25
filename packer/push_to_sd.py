#!/usr/bin/env python3
"""Push local files onto the device's SD card over serial, using the
readerino firmware's Transfer protocol (firmware/readerino_nano/Transfer.cpp).

Typical use is pushing a packed library:
    python3 push_to_sd.py library/*.rbk library/catalog.bin
"""

import argparse
import sys
import time
from pathlib import Path

import serial

DEFAULT_PORT = "/dev/ttyUSB0"

# Must match the firmware's TRANSFER_BAUD / TRANSFER_CHUNK_SIZE (Config.h).
# The chunk is kept under the Nano's 64-byte serial RX buffer, since it only
# ACKs once a chunk is written to the card.
BAUD = 115200
CHUNK_SIZE = 48


def readline(ser, timeout=10):
    ser.timeout = timeout
    line = ser.readline()
    return line.decode("utf-8", errors="replace").strip()


def handshake(ser):
    time.sleep(2.5)  # let the board finish its DTR-triggered reset
    ser.reset_input_buffer()
    ser.write(b"HELLO\n")
    resp = readline(ser, timeout=10)
    if not resp.startswith("READERINO"):
        raise RuntimeError(f"unexpected handshake response: {resp!r}")
    print(f"Connected: {resp}")


def push_file(ser, local_path: Path, remote_name: str) -> bool:
    size = local_path.stat().st_size
    ser.write(f"PUT {remote_name} {size}\n".encode("utf-8"))
    resp = readline(ser, timeout=10)
    if resp != "OK":
        print(f"  FAIL {remote_name}: device said {resp!r}")
        return False

    data = local_path.read_bytes()
    sent = 0
    while sent < size:
        chunk = data[sent:sent + CHUNK_SIZE]
        ser.write(chunk)
        sent += len(chunk)
        resp = readline(ser, timeout=10)
        if not resp.startswith("ACK "):
            print(f"  FAIL {remote_name}: expected ACK, got {resp!r} at {sent}/{size}")
            return False
        acked = int(resp.split()[1])
        if acked != sent:
            print(f"  FAIL {remote_name}: ACK mismatch {acked} != {sent}")
            return False

    resp = readline(ser, timeout=10)
    if not resp.startswith("DONE "):
        print(f"  FAIL {remote_name}: expected DONE, got {resp!r}")
        return False
    written = int(resp.split()[1])
    if written != size:
        print(f"  FAIL {remote_name}: wrote {written} != {size}")
        return False
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", help="files to push (paths on the SD card mirror the basenames)")
    ap.add_argument("-p", "--port", default=DEFAULT_PORT, help=f"serial port (default {DEFAULT_PORT})")
    args = ap.parse_args()

    files = [Path(p) for p in args.files]

    ser = serial.Serial(args.port, BAUD)
    try:
        handshake(ser)
        ok, failed = 0, []
        for i, f in enumerate(files, 1):
            remote = "/" + f.name
            print(f"[{i}/{len(files)}] Sending {f.name} ({f.stat().st_size} bytes)...")
            if push_file(ser, f, remote):
                ok += 1
            else:
                failed.append(f.name)

        ser.write(b"BYE\n")
        readline(ser, timeout=5)

        print(f"\n{ok} sent, {len(failed)} failed")
        if failed:
            print("Failed:", failed)
            sys.exit(1)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
