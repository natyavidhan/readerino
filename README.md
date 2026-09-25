# readerino

A pocket e-reader built from an Arduino Nano, a 1.8" color TFT, an SD card,
and three buttons. No touchscreen, no WiFi dependency, no bloat — just a
small library you can browse and read with almost no lag, on a classic
ATmega328P (2KB RAM, 32KB flash).

The build has two halves:

- **Firmware** (`firmware/readerino_nano/`) — a landscape 160x128 color UI:
  home menu, library, bookmarks, a paginated reader, and a small video
  player / image viewer.
- **Packer** (`packer/`) — a Python CLI that turns books (`.txt`/`.pdf`/
  `.epub`), videos and images into the binary formats the device reads, and
  pushes them onto the SD card over the same USB cable used to flash the
  firmware.

## Screens

![screens](docs/nano-screens.png)

A home menu, a dark library screen (colored book spines, per-book progress
that turns amber while you're reading and green when you finish, a
scrollbar, a `3/52` counter), a bookmarks list across every book, and a
warm-paper reader (24 columns x 10 lines, a progress
bar, the title, the page number, and a red ribbon on bookmarked pages).

## Controls

Three buttons: ▲ triangle (up), ● circle (center), ■ square (down). The
screens use the same shapes, e.g. the exit dialog reads "▲ No" / "■ Yes".

- **Menu** — up/down to pick Library, Bookmarks or Settings, center to open
- **Library / Bookmarks** — up/down to move, center to open the book (a
  bookmark opens at its page), hold center to go back to the menu. A
  selected title too long for its row scrolls like a track name on Spotify
- **Reading** — down/up to turn pages; hold center to bookmark the page, or
  to remove the bookmark if it already has one; tap center for "Back to
  library?" (down = yes and save your place, up = keep reading)
- **Video** — plays from where you left off; down/up skip 5 seconds
  forward/back; hold center to bookmark the current second (or remove that
  bookmark); tap center to pause and get "Back to library?"
- **Image** — hold center to bookmark it, tap center for "Back to library?"
- **Settings** — empty for now, center goes back

Videos show a ▶ and images a picture icon in place of the color stripe in
the library; bookmarks on a video list the time (`1:23`) instead of a page.

## Hardware and pins

| Part | Pin | Nano |
|---|---|---|
| 1.8" ST7735 TFT | LED | D6 |
| | SCK | D13 |
| | SDA | D11 |
| | A0 (data/command) | D8 |
| | RESET | D7 |
| | CS | D9 |
| SD card module (3.3V-only) | CS | D10 via divider |
| | MOSI | A5 via divider |
| | CLK | A4 via divider |
| | MISO | A0, direct |
| | 3V3 | 3V3 |
| Buttons (to GND) | up / select / down | D3 / D4 / D5 |

The SD module has no level shifter, so CS, MOSI and CLK each go through a
5V → ~3.3V resistor divider (two equal resistors in parallel on the Nano
side, one more of the same value to GND). The TFT gets the hardware SPI
bus to itself; the SD card is bit-banged on A0/A4/A5 by a patched copy of
the SD library in `firmware/local_libraries/SD` (sharing one bus between
the two broke the card).

The display driver (`Tft.cpp`) is a small hardware-SPI ST7735 driver
written for this build instead of Adafruit_GFX: every text box is streamed
as one address window with its background, so each pixel is sent once, and
it leaves ~8KB of flash free for future features.

## Designing screens on a PC

`tools/simulator/simulate.py` renders every screen pixel for pixel from the
firmware's own `Theme.h` (all colors and layout numbers) and `Font.h`, so
you can tweak the design without flashing:

```bash
python3 tools/simulator/simulate.py         # -> tools/simulator/out/*.png + screens.png (all in a grid)
python3 tools/simulator/simulate.py --demo  # same with public-domain sample books, safe to share
```

## Building and flashing

Built with [arduino-cli](https://arduino.github.io/arduino-cli/) against the
`arduino:avr` core. No display library is needed; the SD library comes from
the patched copy in `firmware/local_libraries/SD`. Clone Nanos with the old
bootloader need `cpu=atmega328old` to upload.

```bash
firmware/flash.sh           # build, check it fits, find the Nano's port, upload
firmware/flash.sh --check   # build + size check only
```

`flash.sh` refuses to upload a failed build or one that would run into the
bootloader (0x7800). It calls `arduino-cli compile --fqbn
arduino:avr:nano:cpu=atmega328old --library local_libraries/SD` and
`arduino-cli upload` under the hood.

## Getting books onto the device

The SD card is wired to the Nano, not to your computer, so the firmware
exposes a tiny serial protocol (`firmware/readerino_nano/Transfer.cpp`) that
lets a host script push files through the USB cable.

```bash
cd packer
pip install -r requirements.txt

# 1. Pack your books into the device's binary format (wrapped at 24 columns)
python3 pack.py ~/books/*.pdf ~/books/*.epub ~/notes/*.txt -o ../library

# 2. Push the packed library onto the SD card over serial
python3 push_to_sd.py ../library/*.rbk ../library/catalog.bin -p /dev/ttyUSB0
```

A full 3.5MB library takes about 9 minutes to push at the Nano's 115200 baud.

Re-running the packer against files you've already packed updates them in
place without losing your reading position or bookmarks — it tracks
source-file → book-id mappings in a local `.pack_manifest.json` so it knows
what's already there.

## Videos and images

`pack.py` takes videos (`.mp4`/`.mkv`/`.webm`/`.mov`/`.gif`...) and images
(`.png`/`.jpg`/...) alongside books; they land in the same library.

```bash
python3 pack.py clip.mp4 photo.png -o ../library           # 30 fps, black/white threshold
python3 pack.py footage.mp4 -o ../library --fps 20 --dither # ordinary footage: dithered 1-bit
```

**Video** is 160x120, 1-bit, no audio, in a `.rvd` file built for a chip
with 2KB of RAM and no frame buffer (the TFT's own memory is the frame
buffer):

- Each frame stores only the pixels that changed, as row spans of
  run-length-coded black/white runs — the exact pixels to rewrite, so the
  Nano never compares or rebuilds frames. Clean black-and-white animation
  averages ~700 bytes and ~1,300 rewritten pixels a frame, which plays at
  a full 30 fps.
- A full snapshot of every second is stored *outside* the playback stream,
  so normal playback never pays for a full redraw; resuming, skipping and
  bookmarks jump to a snapshot and carry on from there.
- The file is streamed, never loaded: a ~190-byte read-ahead buffer is
  topped up from the card in the idle time between frames, so a heavy
  frame's data is usually already in RAM when it's due. The buffer shares
  memory with the bookmarks list, which isn't needed during playback.
- `media.decode_video()` is a reference decoder; the encoder is checked
  against it frame for frame.

**Images** are stored as raw RGB565 (`.rim`), fit into 160x128, and
streamed from the card straight to the screen.

Before re-packing a library you've been reading on the device, pull its
catalog so the progress and bookmarks made on the device are kept:

```bash
python3 push_to_sd.py --pull catalog.bin --into ../library
```

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

## Project layout

```
firmware/readerino_nano/  Arduino Nano sketch
  App.*               state machine (home / library / bookmarks / settings / reading / confirm)
  Display.*           screens (menu, lists, reader, player strip, dialog, toast, messages)
  Media.*             video player and image viewer
  Tft.*               lean ST7735 hardware-SPI driver
  Theme.h             every color and layout number (read by the simulator too)
  Font.h              5x7 glyphs + icon glyphs
  Storage.*           catalog.bin reader/writer
  Book.*              .rbk file reader
  Buttons.*           debounced input with long-press detection
  Transfer.*          serial protocol for pushing files onto the SD card
firmware/local_libraries/SD/  SD library patched for software SPI on A0/A4/A5
tools/simulator/      renders the Nano screens to PNG on a PC
packer/               host-side Python tool
  extract.py          txt/pdf/epub → plain text
  textutil.py         word-wrap + ASCII sanitization
  formats.py          .rbk / catalog.bin binary layout
  media.py            video (.rvd) and image (.rim) encoders + reference decoder
  pack.py             CLI entry point
  push_to_sd.py       pushes packed files onto the SD card over USB serial
```
