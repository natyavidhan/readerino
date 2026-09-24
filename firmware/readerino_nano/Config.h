#pragma once

// ---- SD card: software SPI on A0/A4/A5 (see local_libraries/SD) ----
// The hardware SPI bus (D11/D12/D13) now belongs exclusively to the TFT,
// which needs the throughput far more -- it pushes 40KB+ per screen
// redraw, vs. the SD card's small chunked reads (a 156-byte catalog
// record, a short line of text at a time). Sharing the bus caused SD
// communication to fail outright; giving each device its own bus (TFT on
// hardware SPI, SD bit-banged) fixed that AND gave the TFT full hardware
// SPI throughput instead of both devices being stuck on software SPI.
//
// SD's actual MOSI/MISO/SCK pins (A5/A0/A4) are compiled into the local
// copy of the SD library at firmware/local_libraries/SD -- the classic
// Arduino SD library has no per-sketch pin override for its software-SPI
// mode, so this project keeps its own patched copy instead of editing the
// global install. CS is the one SD pin our own code controls directly:
#define PIN_SD_CS 10

// ---- TFT: hardware SPI (fixed on ATmega328P: MOSI=D11, MISO=D12 [unused], SCK=D13) ----
// Direct wiring, no resistor dividers needed -- these 1.8" ST7735 boards
// are 5V-tolerant.
#define PIN_TFT_CS 9
#define PIN_TFT_DC 8  // labeled "A0" on the display module (unrelated to the Nano's A0 pin)
#define PIN_TFT_RST 7
#define PIN_TFT_LED 6 // backlight, PWM-capable

// ---- Buttons (GPIO -> button -> GND, internal pull-ups) ----
#define PIN_BTN_UP 3
#define PIN_BTN_SELECT 4
#define PIN_BTN_DOWN 5

#define DEBOUNCE_MS 25
#define LONG_PRESS_MS 600

// ---- Display ----
#define SCREEN_W 128
#define SCREEN_H 160
// Most 1.8" 128x160 ST7735 boards use the black-tab init; if colors or
// geometry look wrong on first boot, try INITR_GREENTAB instead (Display.cpp).

// ---- Reader layout ----
// 6x8px default font. CHARS_PER_LINE matches the wrap width already baked
// into every .rbk file by the packer — do not change without repacking.
#define CHARS_PER_LINE 21
#define ROW_H 8
// One row reserved at the bottom of the reading screen for the page/bookmark status.
#define LINES_PER_PAGE ((SCREEN_H / ROW_H) - 1)
#define VISIBLE_ROWS (SCREEN_H / ROW_H)

// ---- Catalog / bookmarks ----
// On-disk field widths (must match packer/formats.py — do not change).
#define DISK_FILENAME_LEN 32
#define DISK_TITLE_LEN 48
#define DISK_AUTHOR_LEN 32

// RAM-side copies are kept much smaller than the disk fields: nothing on
// this display can show more than CHARS_PER_LINE of a title anyway, and
// the packer's generated filenames ("/b0001.rbk") never approach 32 bytes.
// This is the difference between CatalogEntry fitting in budget or not.
#define FILENAME_LEN 16
#define TITLE_LEN (CHARS_PER_LINE + 1)
#define MAX_BOOKMARKS 4

// ---- Serial file transfer (host -> SD card via USB) ----
// RAM is the binding constraint here (2KB total), so the chunk buffer is
// kept small on purpose — this trades transfer speed for headroom.
#define TRANSFER_BAUD 115200
#define TRANSFER_CHUNK_SIZE 48
#define TRANSFER_TIMEOUT_MS 5000
