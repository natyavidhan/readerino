#pragma once
#include <Arduino.h>
#include "Theme.h"

// Every screen the app draws. Each function paints its whole region
// (backgrounds included) exactly once, so nothing is cleared beforehand
// and there's no flicker. tools/simulator/simulate.py mirrors these
// function for function -- change both together.
namespace Display {
  void begin();

  // Home menu: "READERINO" wordmark + HOME_ITEMS buttons. Moving the
  // selection only needs the two homeItem()s that changed.
  void home(uint8_t selected);
  void homeItem(uint8_t item, bool selected);

  // List screens (library, bookmarks). A full screen is listHeader(full)
  // + LIB_ROWS rows + listScrollbar + listFooter; moving the selection
  // within the visible window only needs the two rows that changed plus
  // listHeader(full=false), which repaints just the "3/52" counter.
  // Titles and hints are PROGMEM strings (PSTR / F).
  void listHeader(const __FlashStringHelper *title, int selected, int count, bool full);
  void listRow(uint8_t slot, const char *title, const char *right, uint16_t rightColor,
               int spineIndex, bool selected);
  void listEmptyRow(uint8_t slot);
  void listScrollbar(int windowStart, int count);
  void listFooter(const __FlashStringHelper *hint);
  // Replaces the rows and scrollbar with a centered two-line message.
  void listEmpty(const __FlashStringHelper *line1, const __FlashStringHelper *line2);

  void settings();

  // Reader page: top margin + RD_LINES lines + status (progress bar, title,
  // page counter, bookmark ribbon).
  void readerTop();
  void readerLine(uint8_t slot, const char *text);
  void readerStatus(const char *title, int page, int pages, bool bookmarked);

  // Overlays drawn on top of the reader page.
  void confirmExit();
  void toast(const __FlashStringHelper *text, uint16_t bg);

  // Full-screen message on the dark background, e.g. "Opening..." + title.
  void message(const __FlashStringHelper *line1, const char *line2, uint16_t accent = COL_ACCENT);
  void message(const __FlashStringHelper *line1, const __FlashStringHelper *line2, uint16_t accent = COL_ACCENT);
}
