#include "App.h"
#include "Config.h"
#include "Buttons.h"
#include "Display.h"
#include "Storage.h"
#include "Book.h"
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

  char lineBuf[CHARS_PER_LINE + 1];

  // Tracks what's actually on screen so navigation can redraw only the
  // rows that changed instead of the whole list. Software SPI to the TFT
  // is slow enough (no longer sharing the hardware SPI bus with the SD
  // card, see Config.h) that repainting all 20 rows on every keypress was
  // the dominant cost, on top of a redundant full-screen fillScreen() that
  // happened before those per-row draws anyway.
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

  void drawLibraryRow(uint8_t row, int idx, int n, bool sel) {
    if (idx >= n) {
      Display::textRow(row, "", false);
      return;
    }
    CatalogEntry e;
    if (!Storage::getEntry(idx, e)) {
      Display::textRow(row, "?", sel);
      return;
    }
    Display::textRow(row, e.title, sel);
    char progStr[5];
    itoa(progressPercent(e), progStr, 10);
    strcat(progStr, "%");
    Display::textRowRight(row, progStr, sel);
  }

  void redrawLibrary() {
    if (sdError) {
      Display::showMessage("SD/catalog error", "Press any button");
      lastWindowStart = -1; // screen no longer shows the library; force a full redraw next time
      return;
    }
    int n = Storage::bookCount();
    if (n == 0) {
      Display::showMessage("No books found", "Push a library from");
      lastWindowStart = -1;
      return;
    }

    int windowStart = 0;
    if (librarySelection >= VISIBLE_ROWS) windowStart = librarySelection - VISIBLE_ROWS + 1;
    int maxStart = n - VISIBLE_ROWS;
    if (maxStart < 0) maxStart = 0;
    if (windowStart > maxStart) windowStart = maxStart;
    int selectedRow = librarySelection - windowStart;

    if (windowStart == lastWindowStart && lastSelectedRow >= 0) {
      // Same window as last draw: only the old and new highlighted rows
      // actually changed on screen.
      if (lastSelectedRow != selectedRow) {
        Display::beginBatch();
        drawLibraryRow(lastSelectedRow, windowStart + lastSelectedRow, n, false);
        drawLibraryRow(selectedRow, windowStart + selectedRow, n, true);
        Display::endBatch();
      }
    } else {
      // Window scrolled, or the screen was showing something else before
      // (reading page, a message) -- full redraw. Still no upfront
      // fillScreen(): every row's own fillRect already covers it. Batched
      // into one SPI transaction instead of ~40 separate ones (fillRect +
      // print, per row).
      Display::beginBatch();
      for (uint8_t row = 0; row < VISIBLE_ROWS; row++) {
        int idx = windowStart + row;
        drawLibraryRow(row, idx, n, idx == librarySelection);
      }
      Display::endBatch();
    }

    lastWindowStart = windowStart;
    lastSelectedRow = selectedRow;
  }

  void drawReadingPage() {
    Display::beginBatch();
    for (uint8_t row = 0; row < LINES_PER_PAGE; row++) {
      book.getLine(currentLine + row, lineBuf, CHARS_PER_LINE);
      Display::textRow(row, lineBuf, false);
    }
    int page = currentLine / LINES_PER_PAGE;
    char status[16];
    itoa(page + 1, status, 10);
    strcat(status, "/");
    char pageCountStr[6];
    itoa(book.pageCount(), pageCountStr, 10);
    strcat(status, pageCountStr);
    if (isBookmarkedHere()) strcat(status, "  *");
    Display::textRow(LINES_PER_PAGE, status, false);
    Display::endBatch();
  }

  bool openBookAt(int catalogIndex, uint32_t startLine) {
    CatalogEntry e;
    if (!Storage::getEntry(catalogIndex, e)) return false;
    Display::showMessage("Opening...", e.title);
    if (!book.open(e.filename)) {
      Display::showMessage("Failed to open", e.filename);
      delay(1200);
      return false;
    }
    openBookIndex = catalogIndex;
    openEntry = e;
    uint32_t total = book.totalLines();
    if (startLine >= total) startLine = 0;
    currentLine = (startLine / LINES_PER_PAGE) * LINES_PER_PAGE;
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
        uint32_t next = currentLine + LINES_PER_PAGE;
        if (next < total) {
          currentLine = next;
          drawReadingPage();
        }
      } else if (ev == ButtonEvent::UpPressed) {
        currentLine = (currentLine < (uint32_t)LINES_PER_PAGE) ? 0 : currentLine - LINES_PER_PAGE;
        drawReadingPage();
      } else if (ev == ButtonEvent::SelectLong) {
        Storage::addBookmark(openBookIndex, currentLine);
        Storage::getEntry(openBookIndex, openEntry); // refresh cached bookmarks for the "*" indicator
        Display::showToast("Bookmarked!");
        toastUntilMs = millis() + 900;
      } else if (ev == ButtonEvent::SelectShort) {
        state = State::ConfirmExit;
        Display::showConfirmExit();
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
