#pragma once
#include <Arduino.h>

namespace Display {
  bool begin();
  void clear();

  // Wrap a batch of textRow()/textRowRight() calls in beginBatch()/endBatch()
  // when drawing multiple rows in one pass. Every Adafruit_GFX primitive
  // (fillRect, print, ...) normally opens and closes its own SPI
  // transaction; on this chip's software SPI, that per-call overhead is
  // significant when it happens ~40 times for a 20-row screen. Calls nest
  // safely if already inside a batch.
  void beginBatch();
  void endBatch();

  // Draws text left-aligned on a row (0-based, ROW_H px tall), clearing
  // the whole row first. inverted = selection highlight (filled bar).
  void textRow(uint8_t row, const char *text, bool inverted);

  // Draws text right-aligned on an already-drawn row without disturbing
  // the left-aligned label (masks just the region it needs).
  void textRowRight(uint8_t row, const char *text, bool inverted);

  void showConfirmExit();
  void showToast(const char *message);
  void showMessage(const char *line1, const char *line2 = nullptr);
}
