#pragma once
#include <Arduino.h>

// Minimal ST7735R (1.8" 128x160, "black tab") driver on the ATmega328P's
// hardware SPI, run in landscape as 160x128.
//
// Replaces Adafruit_GFX + Adafruit_ST7735: those pulled in Adafruit_BusIO's
// I2C stack and malloc (~2.5KB of flash doing nothing on this build), and
// GFX draws text one pixel at a time -- a separate address-window command
// for every lit pixel. Here every primitive is a single address window
// streamed start to finish, and each pixel on screen is sent exactly once:
// textBox() paints its own background, so callers never clear first.
//
// tools/simulator/simulate.py implements these same two primitives; keep
// them in step.
namespace Tft {
  void begin();

  void fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color);

  // Fills a w*h box with bg and draws text at (tx, ty) inside it, clipped
  // to the box. Text is 6x8 cells from Font.h; codes outside the font draw
  // as '?'.
  void textBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tx, uint8_t ty,
               const char *text, uint16_t fg, uint16_t bg);
  // Same, with the text in PROGMEM (F("...") / PSTR("...")).
  void textBoxP(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tx, uint8_t ty,
                const char *text, uint16_t fg, uint16_t bg);
  // One marquee frame: text followed by `gap` spaces, repeated forever,
  // drawn starting `offset` pixels into that loop and clipped to w.
  void textBoxLoop(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t ty, const char *text,
                   uint8_t gap, uint16_t offset, uint16_t fg, uint16_t bg);
  // Same as textBox, drawn at 2x (12x16 cells).
  void textBox2x(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tx, uint8_t ty,
                 const char *text, uint16_t fg, uint16_t bg);
}
