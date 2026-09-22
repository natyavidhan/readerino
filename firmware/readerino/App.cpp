#include "App.h"
#include "Config.h"
#include "Buttons.h"
#include "Display.h"
#include "Storage.h"
#include "Book.h"
#include <vector>
#include <Arduino.h>

namespace {
  enum class State { Library, Bookmarks, Reading, ConfirmExit };
  enum class MarqueePhase { PauseStart, Scrolling, PauseEnd };

  State state = State::Library;
  int librarySelection = 0; // 0 = "* Bookmarks" pseudo-row, 1..N = books (catalog index = librarySelection-1)
  bool sdError = false;

  Book book;
  int openBookIndex = -1;      // catalog index of the currently open book, -1 if none
  uint32_t currentLine = 0;    // top line index of the current reading page (always page-aligned)
  CatalogEntry openEntry;      // cached catalog entry for the open book (title/bookmarks)
  unsigned long toastUntilMs = 0;

  struct BookmarkRef {
    int bookIndex;
    uint32_t line;
    String label;
  };
  std::vector<BookmarkRef> bookmarkRefs; // flat list built fresh each time Bookmarks is entered
  int bookmarkSelection = 0;

  // The currently-visible window for whichever list screen is active
  // (Library or Bookmarks) — at most MENU_VISIBLE_ROWS entries, refetched
  // from SD only on real navigation, not on every marquee animation tick.
  std::vector<String> windowLabels;
  std::vector<int> windowProgress; // Library only; empty for Bookmarks
  int windowHighlightRow = 0;

  MarqueePhase marqueePhase = MarqueePhase::PauseStart;
  int marqueeOffsetPx = 0;
  unsigned long marqueePhaseStartMs = 0;
  unsigned long marqueeLastStepMs = 0;

  void resetMarquee() {
    marqueePhase = MarqueePhase::PauseStart;
    marqueeOffsetPx = 0;
    marqueePhaseStartMs = millis();
  }

  bool isBookmarkedHere() {
    for (uint8_t i = 0; i < openEntry.bookmarkCount; i++) {
      if (openEntry.bookmarks[i] == currentLine) return true;
    }
    return false;
  }

  int progressPercent(const CatalogEntry &e) {
    if (e.totalLines == 0) return 0;
    long pct = ((long)e.position * 100) / (long)e.totalLines;
    if (pct > 100) pct = 100;
    return (int)pct;
  }

  // Fetches only the rows that will actually be drawn instead of scanning
  // the whole catalog: at most MENU_VISIBLE_ROWS calls to Storage::getEntry
  // regardless of library size.
  void redrawLibrary() {
    int n = Storage::bookCount();
    int totalRows = n + 1; // + the "* Bookmarks" pseudo-row
    int windowStart = 0;
    if (librarySelection >= MENU_VISIBLE_ROWS) windowStart = librarySelection - MENU_VISIBLE_ROWS + 1;
    int maxStart = totalRows - MENU_VISIBLE_ROWS;
    if (maxStart < 0) maxStart = 0;
    if (windowStart > maxStart) windowStart = maxStart;

    windowLabels.clear();
    windowProgress.clear();
    for (int row = 0; row < MENU_VISIBLE_ROWS; row++) {
      int idx = windowStart + row;
      if (idx >= totalRows) break;
      if (idx == 0) {
        windowLabels.push_back(String("* Bookmarks"));
        windowProgress.push_back(-1);
      } else {
        CatalogEntry e;
        if (Storage::getEntry(idx - 1, e)) {
          windowLabels.push_back(e.title);
          windowProgress.push_back(progressPercent(e));
        } else {
          windowLabels.push_back(String("?"));
          windowProgress.push_back(-1);
        }
      }
    }
    windowHighlightRow = librarySelection - windowStart;
    resetMarquee();
    Display::showLibrary(windowLabels, windowProgress, windowHighlightRow, marqueeOffsetPx, sdError);
  }

  void redrawBookmarks() {
    int totalRows = (int)bookmarkRefs.size() + 1; // + "< Back"
    int windowStart = 0;
    if (bookmarkSelection >= MENU_VISIBLE_ROWS) windowStart = bookmarkSelection - MENU_VISIBLE_ROWS + 1;
    int maxStart = totalRows - MENU_VISIBLE_ROWS;
    if (maxStart < 0) maxStart = 0;
    if (windowStart > maxStart) windowStart = maxStart;

    windowLabels.clear();
    windowProgress.clear();
    for (int row = 0; row < MENU_VISIBLE_ROWS; row++) {
      int idx = windowStart + row;
      if (idx >= totalRows) break;
      windowLabels.push_back(idx == 0 ? String("< Back") : bookmarkRefs[idx - 1].label);
    }
    windowHighlightRow = bookmarkSelection - windowStart;
    resetMarquee();
    Display::showBookmarksList(windowLabels, windowHighlightRow, marqueeOffsetPx);
  }

  // Pure-RAM redraw used by the marquee ticker: re-renders the cached
  // window with the current scroll offset, no SD access at all.
  void redrawWindowFromCache() {
    if (state == State::Library) {
      Display::showLibrary(windowLabels, windowProgress, windowHighlightRow, marqueeOffsetPx, sdError);
    } else if (state == State::Bookmarks) {
      Display::showBookmarksList(windowLabels, windowHighlightRow, marqueeOffsetPx);
    }
  }

  void stepMarquee() {
    if (state != State::Library && state != State::Bookmarks) return;
    if (windowHighlightRow < 0 || windowHighlightRow >= (int)windowLabels.size()) return;

    int maxChars = CHARS_PER_LINE;
    if (state == State::Library && windowHighlightRow < (int)windowProgress.size() && windowProgress[windowHighlightRow] >= 0) {
      String prog = String(windowProgress[windowHighlightRow]) + "%";
      maxChars -= (int)prog.length() + 1;
    }
    const String &title = windowLabels[windowHighlightRow];
    if ((int)title.length() <= maxChars) return; // fits; nothing to animate

    unsigned long now = millis();
    int maxOffsetPx = ((int)title.length() - maxChars) * CHAR_PX;

    switch (marqueePhase) {
      case MarqueePhase::PauseStart:
        if (now - marqueePhaseStartMs >= MARQUEE_PAUSE_MS) {
          marqueePhase = MarqueePhase::Scrolling;
          marqueePhaseStartMs = now;
        }
        return;
      case MarqueePhase::Scrolling:
        if (now - marqueeLastStepMs < MARQUEE_STEP_MS) return;
        marqueeLastStepMs = now;
        marqueeOffsetPx++;
        if (marqueeOffsetPx >= maxOffsetPx) {
          marqueeOffsetPx = maxOffsetPx;
          marqueePhase = MarqueePhase::PauseEnd;
          marqueePhaseStartMs = now;
        }
        break;
      case MarqueePhase::PauseEnd:
        if (now - marqueePhaseStartMs >= MARQUEE_PAUSE_MS) {
          marqueeOffsetPx = 0;
          marqueePhase = MarqueePhase::PauseStart;
          marqueePhaseStartMs = now;
        }
        return;
    }
    redrawWindowFromCache();
  }

  void drawReadingPage() {
    std::vector<String> lines;
    book.renderPage(currentLine, lines);
    int page = currentLine / LINES_PER_PAGE;
    Display::showReadingPage(lines, page, book.pageCount(), isBookmarkedHere());
  }

  bool openBookAt(int catalogIndex, uint32_t startLine) {
    CatalogEntry e;
    if (!Storage::getEntry(catalogIndex, e)) return false;
    Display::showMessage("Opening...", e.title.c_str());
    if (!book.open(e.filename)) {
      Display::showMessage("Failed to open", e.filename.c_str());
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
    redrawLibrary();
  }

  void buildBookmarkRefs() {
    bookmarkRefs.clear();
    int n = Storage::bookCount();
    for (int i = 0; i < n; i++) {
      CatalogEntry e;
      if (!Storage::getEntry(i, e)) continue;
      for (uint8_t b = 0; b < e.bookmarkCount; b++) {
        BookmarkRef ref;
        ref.bookIndex = i;
        ref.line = e.bookmarks[b];
        ref.label = e.title;
        bookmarkRefs.push_back(ref);
      }
    }
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
  if (librarySelection > Storage::bookCount()) librarySelection = Storage::bookCount();
  if (state == State::Library) redrawLibrary();
}

void App::loop() {
  ButtonEvent ev = Buttons::poll();

  // auto-dismiss the "Bookmarked!" toast, redrawing the page underneath
  if (toastUntilMs && millis() > toastUntilMs) {
    toastUntilMs = 0;
    if (state == State::Reading) drawReadingPage();
  }

  stepMarquee(); // pure-RAM; no-op unless the highlighted title overflows and it's time to step

  if (ev == ButtonEvent::None) return;

  switch (state) {
    case State::Library: {
      int n = Storage::bookCount();
      if (sdError) {
        // retry the SD card / catalog on any press when nothing is available
        sdError = !Storage::begin();
        redrawLibrary();
        break;
      }
      int totalRows = n + 1; // + the "* Bookmarks" pseudo-row
      if (ev == ButtonEvent::UpPressed) {
        librarySelection = (librarySelection - 1 + totalRows) % totalRows;
        redrawLibrary();
      } else if (ev == ButtonEvent::DownPressed) {
        librarySelection = (librarySelection + 1) % totalRows;
        redrawLibrary();
      } else if (ev == ButtonEvent::SelectShort || ev == ButtonEvent::SelectLong) {
        if (librarySelection == 0) {
          buildBookmarkRefs();
          bookmarkSelection = 0;
          state = State::Bookmarks;
          redrawBookmarks();
        } else {
          int catalogIndex = librarySelection - 1;
          CatalogEntry e;
          if (Storage::getEntry(catalogIndex, e)) {
            openBookAt(catalogIndex, e.position);
          }
        }
      }
      break;
    }

    case State::Bookmarks: {
      if (bookmarkRefs.empty()) {
        // any press leaves the "no bookmarks yet" message back to the library
        state = State::Library;
        redrawLibrary();
        break;
      }
      int totalRows = (int)bookmarkRefs.size() + 1; // + "< Back"
      if (ev == ButtonEvent::UpPressed) {
        bookmarkSelection = (bookmarkSelection - 1 + totalRows) % totalRows;
        redrawBookmarks();
      } else if (ev == ButtonEvent::DownPressed) {
        bookmarkSelection = (bookmarkSelection + 1) % totalRows;
        redrawBookmarks();
      } else if (ev == ButtonEvent::SelectShort || ev == ButtonEvent::SelectLong) {
        if (bookmarkSelection == 0) {
          state = State::Library;
          redrawLibrary();
        } else {
          BookmarkRef ref = bookmarkRefs[bookmarkSelection - 1];
          openBookAt(ref.bookIndex, ref.line);
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
