#pragma once
#include <Arduino.h>
#include "Theme.h"

// Every screen the app draws. Each function paints its whole region
// (backgrounds included) exactly once, so nothing is cleared beforehand
// and there's no flicker. tools/simulator/simulate.py mirrors these
// function for function -- change both together.
namespace Display {
  void begin();

  // Library. A full screen is header(full=true) + LIB_ROWS rows +
  // scrollbar + footer; moving the selection within the visible window
  // only needs the two rows that changed plus header(full=false), which
  // repaints just the "3/52" counter.
  void libraryHeader(int selected, int count, bool full);
  void libraryRow(uint8_t slot, const char *title, uint8_t percent, int index, bool selected);
  void libraryEmptyRow(uint8_t slot);
  void libraryScrollbar(int windowStart, int count);
  void libraryFooter();

  // Reader page: top margin + RD_LINES lines + status (progress bar, title,
  // page counter, bookmark ribbon).
  void readerTop();
  void readerLine(uint8_t slot, const char *text);
  void readerStatus(const char *title, int page, int pages, bool bookmarked);

  // Overlays drawn on top of the reader page.
  void confirmExit();
  void toastBookmarked();

  // Full-screen message on the dark background, e.g. "Opening..." + title.
  void message(const __FlashStringHelper *line1, const char *line2, uint16_t accent = COL_ACCENT);
  void message(const __FlashStringHelper *line1, const __FlashStringHelper *line2, uint16_t accent = COL_ACCENT);
}
