# readerino

A pocket e-reader built from an ESP32, a 1.3" OLED, an SD card, and three
buttons. No touchscreen, no WiFi dependency, no bloat — just a small library
you can browse and read with almost no lag.

The build has two halves:

- **Firmware** (`firmware/readerino/`) — runs on the ESP32. Shows a home
  screen with your library and reading progress, a dedicated bookmarks view,
  and a paginated reader.
- **Packer** (`packer/`) — a Python CLI that turns `.txt`/`.pdf`/`.epub`
  files into the binary format the device reads, and pushes them onto the
  SD card over the same USB cable used to flash the firmware.

## Why a custom binary format

The obvious first version just drops `.txt` files on the SD card and
word-wraps them on the device when you open a book. That works, but it's
slow in two ways that actually matter on a microcontroller:

1. Opening a book means scanning the whole file byte-by-byte to figure out
   where every wrapped line starts. Fine for a short essay, noticeably slow
   for anything book-length.
2. If you track reading progress per file (say, in a JSON blob), saving your
   position means rewriting that whole file every time you turn a page.

So the packer does the expensive part — word-wrapping and precomputing a
line-offset table — once, on a real computer, and ships the result as a
`.rbk` file the device can page through with a handful of `seek()` calls. A
`catalog.bin` index sits alongside it with fixed-size records (title,
author, progress, bookmarks) so updating your position or adding a bookmark
is a single in-place write, not a rewrite of the whole index.

Concretely: the library screen used to re-scan the entire catalog (all of
it, every book) on *every single button press* just to redraw a 6-row list.
On a library of 200+ books that was a two-second stall per keypress. Fixing
it to only fetch the rows actually on screen — and keeping the catalog file
open instead of reopening it constantly — took that down to well under
100ms. Numbers like that are why this format exists instead of "just read
the text file."

## Hardware

| Component | Interface | Notes |
|---|---|---|
| ESP32 DevKit | — | any 30/38-pin board |
| 1.3" OLED, 128x64 | I2C | SH1106-family driver |
| SD card module | SPI | 3.3V native (no level shifter needed) |
| 3x momentary buttons | GPIO | wired to GND, internal pull-ups |

### Pinout

| Signal | ESP32 pin |
|---|---|
| SD CS | GPIO5 |
| SD MOSI | GPIO23 |
| SD MISO | GPIO19 |
| SD CLK | GPIO18 |
| OLED SDA | GPIO21 |
| OLED SCL | GPIO22 |
| Button 1 (up / back) | GPIO32 |
| Button 2 (select) | GPIO33 |
| Button 3 (down / forward) | GPIO27 |

SD uses the ESP32's default VSPI pins, OLED uses the default I2C pins — both
work with the stock `SD.begin()` / `Wire.begin()` calls, no custom bus setup
needed.

## Controls

**Library / bookmarks screens**
- Button 1 / Button 3 — move selection up / down
- Button 2 — open the selected book (or enter the highlighted view)

**Reading a book**
- Button 3 — next page, Button 1 — previous page
- Hold Button 2 — bookmark the current page
- Short-press Button 2 — asks "back to menu?" (Button 3 = yes, saves your
  position; Button 1 = no, keep reading)

A title too long to fit a row scrolls like a track name on Spotify — only
the selected row animates, everything else just truncates.

## Getting books onto the device

The SD card is wired to the ESP32, not to your computer, so there's no way
to just mount it and drag files over. Instead, the firmware exposes a tiny
serial protocol (see `firmware/readerino/Transfer.h`) that lets a host
script push files through the same USB cable used to program the board.

```bash
cd packer
pip install -r requirements.txt

# 1. Pack your books into the device's binary format
python3 pack.py ~/books/*.pdf ~/books/*.epub ~/notes/*.txt -o ../library

# 2. Push the packed library onto the SD card over serial
python3 push_to_sd.py ../library/*.rbk ../library/catalog.bin -p /dev/ttyUSB0
```

Re-running the packer against files you've already packed updates them in
place without losing your reading position or bookmarks — it tracks
source-file → book-id mappings in a local `.pack_manifest.json` so it knows
what's already there.

## Building and flashing

Built with [arduino-cli](https://arduino.github.io/arduino-cli/) against the
`esp32:esp32` core.

```bash
cd firmware
arduino-cli compile --fqbn esp32:esp32:esp32 readerino
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 readerino
```

Dependencies (installed via `arduino-cli lib install`): `Adafruit SH110X`,
`Adafruit GFX Library`, `Adafruit BusIO`, `SD`.

## Project layout

```
firmware/readerino/   ESP32 sketch
  App.*               state machine (library / bookmarks / reading / confirm)
  Display.*           OLED rendering
  Storage.*           catalog.bin reader/writer
  Book.*              .rbk file reader
  Buttons.*           debounced input with long-press detection
  Transfer.*          serial protocol for pushing files onto the SD card
packer/               host-side Python tool
  extract.py          txt/pdf/epub → plain text
  textutil.py         word-wrap + ASCII sanitization
  formats.py          .rbk / catalog.bin binary layout
  pack.py             CLI entry point
```
