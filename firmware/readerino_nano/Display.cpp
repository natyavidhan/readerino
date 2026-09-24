#include "Display.h"
#include "Config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <string.h>

namespace {
  // Hardware SPI (fixed D11/D13) -- the SD card now has this bus to
  // itself off on software SPI instead (see Config.h / local_libraries/SD),
  // so the TFT gets full hardware SPI throughput for what's by far the
  // larger, more frequent data mover of the two.
  Adafruit_ST7735 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
}

bool Display::begin() {
  pinMode(PIN_TFT_LED, OUTPUT);
  digitalWrite(PIN_TFT_LED, HIGH); // backlight on

  // INITR_BLACKTAB is the common variant for 1.8"/128x160 ST7735 boards.
  // If the image is mirrored, shifted, or the wrong colors, try
  // INITR_GREENTAB here instead.
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.setTextWrap(false);
  tft.setTextSize(1);
  clear();
  return true;
}

void Display::clear() {
  tft.fillScreen(ST77XX_WHITE);
}

void Display::beginBatch() { tft.startWrite(); }
void Display::endBatch() { tft.endWrite(); }

void Display::textRow(uint8_t row, const char *text, bool inverted) {
  int16_t y = (int16_t)row * ROW_H;
  uint16_t bg = inverted ? ST77XX_BLACK : ST77XX_WHITE;
  uint16_t fg = inverted ? ST77XX_WHITE : ST77XX_BLACK;
  tft.fillRect(0, y, SCREEN_W, ROW_H, bg);
  // Transparent background (fg only): the fillRect above already painted
  // the row, so an opaque text color here would repaint every character's
  // background pixels a second time for no visual difference -- real
  // wasted bytes on a bus this slow.
  tft.setTextColor(fg);
  tft.setCursor(1, y);
  tft.print(text);
}

void Display::textRowRight(uint8_t row, const char *text, bool inverted) {
  int16_t y = (int16_t)row * ROW_H;
  uint16_t bg = inverted ? ST77XX_BLACK : ST77XX_WHITE;
  uint16_t fg = inverted ? ST77XX_WHITE : ST77XX_BLACK;
  uint8_t len = strlen(text);
  int16_t textW = (int16_t)len * 6; // default font cell width
  int16_t x = SCREEN_W - 2 - textW;
  if (x < 0) x = 0;
  tft.fillRect(x - 2, y, SCREEN_W - (x - 2), ROW_H, bg);
  tft.setTextColor(fg);
  tft.setCursor(x, y);
  tft.print(text);
}

void Display::showConfirmExit() {
  int16_t w = 104, h = 40;
  int16_t x = (SCREEN_W - w) / 2;
  int16_t y = (SCREEN_H - h) / 2;
  tft.fillRect(x, y, w, h, ST77XX_BLACK);
  tft.drawRect(x, y, w, h, ST77XX_WHITE);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);

  tft.setCursor(x + 6, y + 8);
  tft.print(F("Back to menu?"));

  tft.setCursor(x + 10, y + 24);
  tft.print(F("No"));
  tft.setCursor(x + w - 10 - 3 * 6, y + 24);
  tft.print(F("Yes"));
}

void Display::showToast(const char *message) {
  int16_t w = 96, h = 24;
  int16_t x = (SCREEN_W - w) / 2;
  int16_t y = (SCREEN_H - h) / 2;
  tft.fillRect(x, y, w, h, ST77XX_BLACK);
  tft.drawRect(x, y, w, h, ST77XX_WHITE);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(x + 6, y + 8);
  tft.print(message);
}

void Display::showMessage(const char *line1, const char *line2) {
  clear();
  tft.setTextColor(ST77XX_BLACK, ST77XX_WHITE);
  tft.setCursor(0, 0);
  tft.println(line1);
  if (line2) tft.println(line2);
}
