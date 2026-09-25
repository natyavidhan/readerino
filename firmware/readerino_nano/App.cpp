#include "App.h"
#include "Config.h"
#include "Buttons.h"
#include "Display.h"
#include "Storage.h"
#include "Book.h"
#include "Font.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

namespace {
  enum class State : uint8_t { Library, Reading, ConfirmExit };

  State state = State::Library;
  int librarySelection = 0;
  bool sdError = false;

  Book book;
  int openBookIndex = -1;   // catalog index of the currently open book, -1 if none
  uint32_t currentLine = 0; // top line index of the current reading page (page-aligned)
  CatalogEntry openEntry;   // the open book's catalog record, kept for its bookmark list
  unsigned long toastUntilMs = 0;

  char lineBuf[RD_COLS + 1];

  // Tracks what's actually on screen so navigation can redraw only the
  // rows that changed instead of the whole list. -1 = the screen shows
  // something else (reader, a message), so the next draw is a full one.
  int lastWindowStart = -1;
  int lastSelectedRow = -1;

  bool isBookmarkedHere() {
    for (uint8_t i = 0; i < openEntry.bookmarkCount; i++) {
      if (openEntry.bookmarks[i] == currentLine) return true;
    }
    return false;
  }

  int8_t progressPercent(const CatalogEntry &e) {
    if (e.totalLines == 0) return 0;
    long pct = ((long)e.position * 100L) / (long)e.totalLines;
    if (pct > 100) pct = 100;
    return (int8_t)pct;
  }

  void drawLibraryRow(uint8_t slot, int idx, int n, bool sel) {
    CatalogEntry e;
    if (idx >= n || !Storage::getEntry(idx, e)) {
      Display::libraryEmptyRow(slot);
      return;
    }
    Display::libraryRow(slot, e.title, progressPercent(e), idx, sel);
  }

  void redrawLibrary() {
    if (sdError) {
      Display::message(F("SD card error"), F("Press any button"), COL_ERROR);
      lastWindowStart = -1;
      return;
    }
    int n = Storage::bookCount();
    if (n == 0) {
      Display::message(F("No books yet"), F("Use push_to_sd.py"));
      lastWindowStart = -1;
      return;
    }

    int windowStart = 0;
    if (librarySelection >= LIB_ROWS) windowStart = librarySelection - LIB_ROWS + 1;
    int maxStart = n - LIB_ROWS;
    if (maxStart < 0) maxStart = 0;
    if (windowStart > maxStart) windowStart = maxStart;
    int selectedRow = librarySelection - windowStart;

    if (windowStart == lastWindowStart) {
      // Same window: only the old and new highlighted rows changed, plus
      // the "3/52" counter.
      if (lastSelectedRow != selectedRow) {
        drawLibraryRow(lastSelectedRow, windowStart + lastSelectedRow, n, false);
        drawLibraryRow(selectedRow, windowStart + selectedRow, n, true);
        Display::libraryHeader(librarySelection, n, false);
      }
    } else {
      // Scrolled, or coming from another screen: every row, the scrollbar,
      // and (only when coming from elsewhere) the static header and footer.
      bool full = lastWindowStart < 0;
      Display::libraryHeader(librarySelection, n, full);
      for (uint8_t row = 0; row < LIB_ROWS; row++) {
        int idx = windowStart + row;
        drawLibraryRow(row, idx, n, idx == librarySelection);
      }
      Display::libraryScrollbar(windowStart, n);
      if (full) Display::libraryFooter();
    }

    lastWindowStart = windowStart;
    lastSelectedRow = selectedRow;
  }

  void drawReadingPage() {
    Display::readerTop();
    for (uint8_t row = 0; row < RD_LINES; row++) {
      book.getLine(currentLine + row, lineBuf, RD_COLS);
      Display::readerLine(row, lineBuf);
    }
    Display::readerStatus(openEntry.title, currentLine / RD_LINES + 1, book.pageCount(), isBookmarkedHere());
  }

  bool openBookAt(int catalogIndex, uint32_t startLine) {
    CatalogEntry e;
    if (!Storage::getEntry(catalogIndex, e)) return false;
    Display::message(F("Opening" GLYPH_ELLIPSIS), e.title);
    if (!book.open(e.filename)) {
      Display::message(F("Could not open"), e.filename, COL_ERROR);
      delay(1200);
      lastWindowStart = -1; // the message replaced the whole library screen
      redrawLibrary();
      return false;
    }
    openBookIndex = catalogIndex;
    openEntry = e;
    uint32_t total = book.totalLines();
    if (startLine >= total) startLine = 0;
    currentLine = (startLine / RD_LINES) * RD_LINES;
    state = State::Reading;
    drawReadingPage();
    return true;
  }

  void exitToLibrary(bool save) {
    if (save && openBookIndex >= 0) {
      Storage::setPosition(openBookIndex, currentLine);
    }
    book.close();
    openBookIndex = -1;
    state = State::Library;
    lastWindowStart = -1; // screen currently shows the reading page, not the library
    redrawLibrary();
  }
}

void App::begin() {
  Buttons::begin();
  Display::begin();
  sdError = !Storage::begin();
  redrawLibrary();
}

void App::onFilesChanged() {
  sdError = !Storage::rescan();
  if (librarySelection >= Storage::bookCount()) {
    librarySelection = Storage::bookCount() > 0 ? Storage::bookCount() - 1 : 0;
  }
  lastWindowStart = -1; // catalog contents may have changed; don't trust the cached draw state
  if (state == State::Library) redrawLibrary();
}

void App::loop() {
  ButtonEvent ev = Buttons::poll();

  if (toastUntilMs && millis() > toastUntilMs) {
    toastUntilMs = 0;
    if (state == State::Reading) drawReadingPage();
  }

  if (ev == ButtonEvent::None) return;

  switch (state) {
    case State::Library: {
      int n = Storage::bookCount();
      if (sdError || n == 0) {
        sdError = !Storage::begin();
        redrawLibrary();
        break;
      }
      if (ev == ButtonEvent::UpPressed) {
        librarySelection = (librarySelection - 1 + n) % n;
        redrawLibrary();
      } else if (ev == ButtonEvent::DownPressed) {
        librarySelection = (librarySelection + 1) % n;
        redrawLibrary();
      } else if (ev == ButtonEvent::SelectShort || ev == ButtonEvent::SelectLong) {
        CatalogEntry e;
        if (Storage::getEntry(librarySelection, e)) {
          openBookAt(librarySelection, e.position);
        }
      }
      break;
    }

    case State::Reading: {
      if (ev == ButtonEvent::DownPressed) {
        uint32_t total = book.totalLines();
        uint32_t next = currentLine + RD_LINES;
        if (next < total) {
          currentLine = next;
          drawReadingPage();
        }
      } else if (ev == ButtonEvent::UpPressed) {
        currentLine = (currentLine < (uint32_t)RD_LINES) ? 0 : currentLine - RD_LINES;
        drawReadingPage();
      } else if (ev == ButtonEvent::SelectLong) {
        Storage::addBookmark(openBookIndex, currentLine);
        Storage::getEntry(openBookIndex, openEntry); // refresh cached bookmarks for the "*" indicator
        Display::toastBookmarked();
        toastUntilMs = millis() + 900;
      } else if (ev == ButtonEvent::SelectShort) {
        state = State::ConfirmExit;
        Display::confirmExit();
      }
      break;
    }

    case State::ConfirmExit: {
      if (ev == ButtonEvent::DownPressed) {
        exitToLibrary(true);
      } else if (ev == ButtonEvent::UpPressed) {
        state = State::Reading;
        drawReadingPage();
      }
      // SelectShort/SelectLong are ignored here: only the 1st/3rd buttons act on this dialog.
      break;
    }
  }
}
