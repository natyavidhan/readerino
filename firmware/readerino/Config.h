#pragma once

// ---- SD card (SPI, ESP32 VSPI default pins) ----
#define PIN_SD_CS 5
// MOSI=23, MISO=19, SCK=18 are the ESP32 VSPI hardware defaults, used
// automatically by SPI.begin() / SD.begin(PIN_SD_CS).

// ---- OLED (I2C, default Wire pins: SDA=21, SCL=22) ----
#define OLED_I2C_ADDR 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// ---- Buttons ----
#define PIN_BTN_UP 32     // 1st button: menu up / reading backward / cancel exit
#define PIN_BTN_SELECT 33 // 2nd button: select / bookmark (hold) / ask-to-exit (short press)
#define PIN_BTN_DOWN 27   // 3rd button: menu down / reading forward / confirm exit

#define DEBOUNCE_MS 25
#define LONG_PRESS_MS 600

// ---- Reader layout ----
#define CHARS_PER_LINE 21
#define LINES_PER_PAGE 7

// ---- Storage ----
#define BOOKMARKS_FILE "/bookmarks.json"

// ---- Serial file transfer (host -> SD card via USB) ----
#define TRANSFER_BAUD 460800
#define TRANSFER_CHUNK_SIZE 512
#define TRANSFER_TIMEOUT_MS 5000
