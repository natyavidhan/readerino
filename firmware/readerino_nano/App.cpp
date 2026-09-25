#include "App.h"
#include "Config.h"
#include "Buttons.h"
#include "Display.h"
#include "Storage.h"
#include "Book.h"
#include "Media.h"
#include "Font.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

// Screens and buttons:
//   Home       up/down pick Library / Bookmarks / Settings, select opens it
//   Library    up/down move, select opens the book, hold select -> Home
//   Bookmarks  up/down move, select opens the book at that page, hold -> Home
//   Settings   placeholder, select (short or held) -> Home
//   Reading    down/up turn pages, hold select toggles a bookmark on this
//              page, short select asks "Back to library?"
//   Playing    video: up/down skip back/forward VIDEO_SKIP_S seconds, hold
//              select toggles a bookmark on this second, short select
//              pauses and asks "Back to library?"
//   Viewing    image: hold select toggles a bookmark, short select asks
//   Confirm    down/square = yes (save position, back to the list the book
//              was opened from), up/triangle = no (keep reading)

namespace {
  enum class State : uint8_t { Home, Library, Bookmarks, Settings, Reading, Playing, Viewing, ConfirmExit };

  State state = State::Home;
  State readerReturn = State::Library; // list to go back to when a book/video/image closes
  uint8_t homeSelection = 0;
  bool sdError = false;

  Book book;
  int openBookIndex = -1;   // catalog index of the currently open book, -1 if none
  uint32_t currentLine = 0; // top line index of the current reading page (page-aligned)
  CatalogEntry openEntry;   // the open item's catalog record, kept for its bookmark list
  unsigned long toastUntilMs = 0;

  char lineBuf[RD_COLS + 1];

  // ---- list screens ----
  // Tracks what's actually on screen so navigation can redraw only the
  // rows that changed instead of the whole list. lastWindowStart = -1
  // means the screen shows something else, so the next draw is a full one.
  struct ListState {
    int selection;
    int lastWindowStart;
    int lastSelectedRow;
  };
  ListState libraryList = {0, -1, -1};
  ListState bookmarkList = {0, -1, -1};

  // Every bookmark across the library, in catalog order and by page within
  // a book. Rebuilt whenever the Bookmarks screen is entered: one pass over
  // catalog.bin, so rows can then be drawn without rescanning it.
  struct BookmarkRef {
    uint16_t book;
    uint32_t line;
  };
  // The bookmarks index and the media player's read-ahead buffer are never
  // in use at the same time (the index is rebuilt whenever the Bookmarks
  // screen opens), so they share this memory.
  union {
    BookmarkRef refs[MAX_BOOKMARK_LIST];
    uint8_t media[MAX_BOOKMARK_LIST * sizeof(BookmarkRef)];
  } scratch;
  BookmarkRef *const bookmarkRefs = scratch.refs;
  uint8_t bookmarkRefCount = 0;

  // Scrolling title on the selected list row (only when it doesn't fit).
  struct {
    bool active;
    uint8_t slot;
    uint16_t offset; // pixels into the looping title
    uint16_t period; // pixels per loop: title + gap
    unsigned long nextMs;
    char title[DISK_TITLE_LEN + 1];
  } marquee;

  void invalidateLists() {
    libraryList.lastWindowStart = -1;
    bookmarkList.lastWindowStart = -1;
    marquee.active = false;
  }

  // (Re)starts the marquee for the selected row: shows the resting title,
  // then starts scrolling after a pause if the full title doesn't fit.
  void startMarquee(uint8_t slot, int bookIndex) {
    marquee.active = false;
    if (!Storage::getTitle(bookIndex, marquee.title)) return;
    uint8_t len = strlen(marquee.title);
    if (len <= LIB_TITLE_CHARS) return;
    marquee.slot = slot;
    marquee.offset = 0;
    marquee.period = (len + MARQUEE_GAP) * GLYPH_W;
    marquee.nextMs = millis() + MARQUEE_PAUSE_MS;
    marquee.active = true;
    Display::listRowTitle(slot, marquee.title, 0);
  }

  void tickMarquee() {
    if (!marquee.active || (long)(millis() - marquee.nextMs) < 0) return;
    if (++marquee.offset >= marquee.period) {
      marquee.offset = 0;
      marquee.nextMs = millis() + MARQUEE_PAUSE_MS;
    } else {
      marquee.nextMs = millis() + MARQUEE_STEP_MS;
    }
    Display::listRowTitle(marquee.slot, marquee.title, marquee.offset);
  }

  // Whether the open item has a bookmark at pos (a line, or a video frame).
  bool isMarked(uint32_t pos) {
    for (uint8_t i = 0; i < openEntry.bookmarkCount; i++) {
      if (openEntry.bookmarks[i] == pos) return true;
    }
    return false;
  }

  bool isBookmarkedHere() { return isMarked(currentLine); }

  uint8_t progressPercent(const CatalogEntry &e) {
    if (e.totalLines == 0) return 0;
    // position is the start of the last page (or second of video) reached,
    // so the very end never reaches totalLines by itself.
    uint32_t lastChunk = e.kind == KIND_VIDEO ? e.fps : RD_LINES;
    if (e.position > 0 && e.position + lastChunk >= e.totalLines) return 100;
    long pct = ((long)e.position * 100L) / (long)e.totalLines;
    return pct > 99 ? 99 : (uint8_t)pct;
  }

  uint16_t progressColor(uint8_t percent) {
    if (percent == 0) return COL_PROG_NEW;
    if (percent >= 100) return COL_PROG_DONE;
    return COL_PROG_MID;
  }

  const __FlashStringHelper *hintMenu() { return F("Hold " GLYPH_BUTTON " for menu"); }

  void drawLibraryRow(uint8_t slot, int idx, bool sel) {
    CatalogEntry e;
    if (idx >= Storage::bookCount() || !Storage::getEntry(idx, e)) {
      Display::listEmptyRow(slot);
      return;
    }
    if (e.kind == KIND_IMAGE) {
      Display::listRow(slot, e.title, "img", COL_MUTED, idx, e.kind, sel);
      return;
    }
    uint8_t pct = progressPercent(e);
    char right[5];
    itoa(pct, right, 10);
    strcat(right, "%");
    Display::listRow(slot, e.title, right, progressColor(pct), idx, e.kind, sel);
  }

  void drawBookmarkRow(uint8_t slot, int idx, bool sel) {
    CatalogEntry e;
    if (idx >= bookmarkRefCount || !Storage::getEntry(bookmarkRefs[idx].book, e)) {
      Display::listEmptyRow(slot);
      return;
    }
    const uint32_t pos = bookmarkRefs[idx].line;
    char right[9];
    if (e.kind == KIND_VIDEO) {
      // time in the video, m:ss
      uint32_t sec = e.fps ? pos / e.fps : 0;
      itoa(sec / 60, right, 10);
      strcat(right, sec % 60 < 10 ? ":0" : ":");
      itoa(sec % 60, right + strlen(right), 10);
    } else if (e.kind == KIND_IMAGE) {
      strcpy(right, "img");
    } else {
      strcpy(right, "p.");
      itoa(pos / RD_LINES + 1, right + 2, 10);
    }
    Display::listRow(slot, e.title, right, COL_ACCENT, bookmarkRefs[idx].book, e.kind, sel);
  }

  // Draws a list screen, redrawing only what changed since the last call.
  // Returns the on-screen slot of the selected row.
  uint8_t drawList(ListState &ls, int n, const __FlashStringHelper *title,
                void (*drawRow)(uint8_t, int, bool)) {
    int windowStart = 0;
    if (ls.selection >= LIB_ROWS) windowStart = ls.selection - LIB_ROWS + 1;
    int maxStart = n - LIB_ROWS;
    if (maxStart < 0) maxStart = 0;
    if (windowStart > maxStart) windowStart = maxStart;
    int selectedRow = ls.selection - windowStart;

    if (windowStart == ls.lastWindowStart) {
      // Same window: only the old and new highlighted rows changed, plus
      // the "3/52" counter.
      if (ls.lastSelectedRow != selectedRow) {
        drawRow(ls.lastSelectedRow, windowStart + ls.lastSelectedRow, false);
        drawRow(selectedRow, ls.selection, true);
        Display::listHeader(title, ls.selection, n, false);
      }
    } else {
      // Scrolled, or coming from another screen: every row and the
      // scrollbar, plus (only when coming from elsewhere) the static
      // header and footer.
      bool full = ls.lastWindowStart < 0;
      Display::listHeader(title, ls.selection, n, full);
      for (uint8_t row = 0; row < LIB_ROWS; row++) {
        int idx = windowStart + row;
        drawRow(row, idx, idx == ls.selection);
      }
      Display::listScrollbar(windowStart, n);
      if (full) Display::listFooter(hintMenu());
    }
    ls.lastWindowStart = windowStart;
    ls.lastSelectedRow = selectedRow;
    return selectedRow;
  }

  // A list screen showing a message instead of rows.
  void drawListMessage(const __FlashStringHelper *title, const __FlashStringHelper *line1,
                       const __FlashStringHelper *line2) {
    Display::listHeader(title, 0, 0, true);
    Display::listEmpty(line1, line2);
    Display::listFooter(hintMenu());
    invalidateLists();
  }

  void redrawLibrary() {
    if (sdError) {
      drawListMessage(F("Library"), F("SD card error"), F("Press any button"));
    } else if (Storage::bookCount() == 0) {
      drawListMessage(F("Library"), F("No books yet"), F("Use push_to_sd.py"));
    } else {
      uint8_t slot = drawList(libraryList, Storage::bookCount(), F("Library"), drawLibraryRow);
      startMarquee(slot, libraryList.selection);
    }
  }

  void buildBookmarkIndex() {
    bookmarkRefCount = 0;
    int n = Storage::bookCount();
    CatalogEntry e;
    for (int b = 0; b < n && bookmarkRefCount < MAX_BOOKMARK_LIST; b++) {
      if (!Storage::getEntry(b, e) || e.bookmarkCount == 0) continue;
      // insertion-sort this book's bookmarks by line, appended after the
      // previous books' ones
      uint8_t first = bookmarkRefCount;
      for (uint8_t i = 0; i < e.bookmarkCount && bookmarkRefCount < MAX_BOOKMARK_LIST; i++) {
        uint8_t j = bookmarkRefCount++;
        while (j > first && bookmarkRefs[j - 1].line > e.bookmarks[i]) {
          bookmarkRefs[j] = bookmarkRefs[j - 1];
          j--;
        }
        bookmarkRefs[j].book = b;
        bookmarkRefs[j].line = e.bookmarks[i];
      }
    }
    if (bookmarkList.selection >= bookmarkRefCount) {
      bookmarkList.selection = bookmarkRefCount > 0 ? bookmarkRefCount - 1 : 0;
    }
    bookmarkList.lastWindowStart = -1;
  }

  void redrawBookmarks() {
    if (sdError) {
      drawListMessage(F("Bookmarks"), F("SD card error"), F("Press any button"));
    } else if (bookmarkRefCount == 0) {
      drawListMessage(F("Bookmarks"), F("No bookmarks yet"), F("Hold " GLYPH_BUTTON " on a page"));
    } else {
      uint8_t slot = drawList(bookmarkList, bookmarkRefCount, F("Bookmarks"), drawBookmarkRow);
      startMarquee(slot, bookmarkRefs[bookmarkList.selection].book);
    }
  }

  void showHome() {
    state = State::Home;
    invalidateLists();
    Display::home(homeSelection);
  }

  void showLibrary() {
    state = State::Library;
    if (sdError) sdError = !Storage::begin();
    if (libraryList.selection >= Storage::bookCount()) libraryList.selection = 0;
    libraryList.lastWindowStart = -1;
    redrawLibrary();
  }

  void showBookmarks() {
    state = State::Bookmarks;
    if (sdError) sdError = !Storage::begin();
    if (!sdError) buildBookmarkIndex();
    bookmarkList.lastWindowStart = -1;
    redrawBookmarks();
  }

  void showList(State s) {
    if (s == State::Bookmarks) showBookmarks();
    else showLibrary();
  }

  void drawReaderStatus() {
    Display::readerStatus(openEntry.title, currentLine / RD_LINES + 1, book.pageCount(), isBookmarkedHere());
  }

  void drawReadingPage() {
    Display::readerTop();
    for (uint8_t row = 0; row < RD_LINES; row++) {
      book.getLine(currentLine + row, lineBuf, RD_COLS);
      Display::readerLine(row, lineBuf);
    }
    drawReaderStatus();
  }

  // ---- media ----
  bool playing = false;          // video running (false while paused / at the end)
  unsigned long nextDueUs = 0;   // when the next video frame should go up
  unsigned long framePeriodUs = 33333;
  unsigned long stripMsgUntil = 0; // a "Bookmarked" message is showing in the strip

  // First frame of the second of video currently on screen: where the
  // position and bookmarks snap to (keyframes are one second apart).
  uint32_t currentKeyFrame() {
    uint32_t f = Media::nextFrame();
    if (f) f--;
    return f - f % Media::keyInterval();
  }

  void drawStrip() {
    Display::mediaStrip(Media::nextFrame(), Media::frameCount(), isMarked(currentKeyFrame()));
  }

  void videoFrom(uint16_t key) {
    stripMsgUntil = 0;
    Media::seekKey(key);
    drawStrip();
    playing = true;
    nextDueUs = micros() + framePeriodUs;
  }

  void drawImage() {
    if (!Media::drawImage(openEntry.filename, scratch.media, sizeof(scratch.media))) {
      Display::message(F("Could not open"), openEntry.filename, COL_ERROR);
    }
  }

  // Opens a catalog item -- book, video or image -- at pos (a line or frame).
  bool openItem(int catalogIndex, uint32_t pos) {
    CatalogEntry e;
    if (!Storage::getEntry(catalogIndex, e)) return false;
    Display::message(F("Opening" GLYPH_ELLIPSIS), e.title);
    invalidateLists();
    readerReturn = state;
    openBookIndex = catalogIndex;
    openEntry = e;
    toastUntilMs = 0;

    bool ok;
    if (e.kind == KIND_VIDEO) {
      bookmarkRefCount = 0; // the index's memory now holds the read-ahead buffer
      ok = Media::openVideo(e.filename, scratch.media, sizeof(scratch.media));
      if (ok) {
        framePeriodUs = 1000000UL / Media::fps();
        state = State::Playing;
        videoFrom(pos / Media::keyInterval());
      }
    } else if (e.kind == KIND_IMAGE) {
      bookmarkRefCount = 0;
      state = State::Viewing;
      ok = Media::drawImage(e.filename, scratch.media, sizeof(scratch.media));
    } else {
      ok = book.open(e.filename);
      if (ok) {
        if (pos >= book.totalLines()) pos = 0;
        currentLine = (pos / RD_LINES) * RD_LINES;
        state = State::Reading;
        drawReadingPage();
      }
    }
    if (!ok) {
      Display::message(F("Could not open"), e.filename, COL_ERROR);
      delay(1200);
      openBookIndex = -1;
      Media::close();
      showList(readerReturn);
    }
    return ok;
  }

  // Closes whatever is open (after the "Back to library?" dialog) and goes
  // back to the list it was opened from.
  void closeItem() {
    if (openBookIndex >= 0) {
      if (openEntry.kind == KIND_VIDEO) Storage::setPosition(openBookIndex, currentKeyFrame());
      else if (openEntry.kind == KIND_BOOK) Storage::setPosition(openBookIndex, currentLine);
    }
    book.close();
    Media::close();
    playing = false;
    openBookIndex = -1;
    toastUntilMs = 0;
    showList(readerReturn);
  }

  // Adds a bookmark at pos, or removes it if there already is one.
  // Returns true if it was removed.
  bool toggleMark(uint32_t pos) {
    bool removing = isMarked(pos);
    if (removing) Storage::removeBookmark(openBookIndex, pos);
    else Storage::addBookmark(openBookIndex, pos);
    Storage::getEntry(openBookIndex, openEntry); // refresh the cached list
    return removing;
  }

  void showToggleToast(bool removed) {
    if (removed) Display::toast(F("Removed"), COL_TOAST_REMOVED_BG);
    else Display::toast(F(GLYPH_BOOKMARK " Bookmarked"), COL_TOAST_BG);
    toastUntilMs = millis() + 900;
  }

  // Up/down on a list; returns true if the selection moved.
  bool moveSelection(ListState &ls, int n, ButtonEvent ev) {
    if (n <= 0) return false;
    if (ev == ButtonEvent::UpPressed) {
      ls.selection = (ls.selection - 1 + n) % n;
      return true;
    }
    if (ev == ButtonEvent::DownPressed) {
      ls.selection = (ls.selection + 1) % n;
      return true;
    }
    return false;
  }

  // Runs the video clock: draws the next frame when it's due, and spends
  // the time until then reading ahead from the card.
  void tickVideo() {
    if (state != State::Playing || !playing) return;
    if ((long)(micros() - nextDueUs) < 0) {
      Media::prefetch();
      return;
    }
    if (!Media::drawNextFrame()) {
      playing = false;
      Display::mediaStripText(F("The end"), COL_MUTED);
      return;
    }
    // Each frame goes up a full frame-time after the previous one. If this
    // one ran late (a heavy frame, or SD re-reads), the clock restarts from
    // now instead of rushing the next frames to catch up: with no audio to
    // stay in sync with, catching up would just show as the video speeding up.
    nextDueUs += framePeriodUs;
    if ((long)(micros() - nextDueUs) > 0) nextDueUs = micros() + framePeriodUs;
    if (stripMsgUntil && millis() > stripMsgUntil) stripMsgUntil = 0;
    if (!stripMsgUntil && (Media::nextFrame() - 1) % Media::keyInterval() == 0) drawStrip();
  }
}

void App::begin() {
  Buttons::begin();
  Display::begin();
  sdError = !Storage::begin();
  showHome();
}

void App::onFilesChanged() {
  sdError = !Storage::rescan();
  invalidateLists();
  if (state == State::Library) showLibrary();
  else if (state == State::Bookmarks) showBookmarks();
}

void App::loop() {
  ButtonEvent ev = Buttons::poll();

  if (toastUntilMs && millis() > toastUntilMs) {
    toastUntilMs = 0;
    if (state == State::Reading) drawReadingPage();
    else if (state == State::Viewing) drawImage();
  }

  tickMarquee();
  tickVideo();

  if (ev == ButtonEvent::None) return;

  switch (state) {
    case State::Home: {
      if (ev == ButtonEvent::UpPressed || ev == ButtonEvent::DownPressed) {
        uint8_t prev = homeSelection;
        homeSelection = ev == ButtonEvent::UpPressed ? (homeSelection + HOME_ITEMS - 1) % HOME_ITEMS
                                                     : (homeSelection + 1) % HOME_ITEMS;
        Display::homeItem(prev, false);
        Display::homeItem(homeSelection, true);
      } else if (homeSelection == 0) {
        showLibrary();
      } else if (homeSelection == 1) {
        showBookmarks();
      } else {
        state = State::Settings;
        invalidateLists();
        Display::settings();
      }
      break;
    }

    case State::Settings: {
      if (ev == ButtonEvent::SelectShort || ev == ButtonEvent::SelectLong) showHome();
      break;
    }

    case State::Library: {
      if (ev == ButtonEvent::SelectLong) {
        showHome();
      } else if (sdError || Storage::bookCount() == 0) {
        showLibrary(); // retries the SD card
      } else if (ev == ButtonEvent::SelectShort) {
        CatalogEntry e;
        if (Storage::getEntry(libraryList.selection, e)) openItem(libraryList.selection, e.position);
      } else if (moveSelection(libraryList, Storage::bookCount(), ev)) {
        redrawLibrary();
      }
      break;
    }

    case State::Bookmarks: {
      if (ev == ButtonEvent::SelectLong) {
        showHome();
      } else if (sdError) {
        showBookmarks(); // retries the SD card
      } else if (ev == ButtonEvent::SelectShort) {
        if (bookmarkRefCount > 0) {
          BookmarkRef r = bookmarkRefs[bookmarkList.selection]; // copy: opening reuses this memory
          openItem(r.book, r.line);
        }
      } else if (moveSelection(bookmarkList, bookmarkRefCount, ev)) {
        redrawBookmarks();
      }
      break;
    }

    case State::Reading: {
      if (ev == ButtonEvent::DownPressed) {
        uint32_t next = currentLine + RD_LINES;
        if (next < book.totalLines()) {
          currentLine = next;
          toastUntilMs = 0;
          drawReadingPage();
        }
      } else if (ev == ButtonEvent::UpPressed) {
        if (currentLine > 0) {
          currentLine = currentLine < (uint32_t)RD_LINES ? 0 : currentLine - RD_LINES;
          toastUntilMs = 0;
          drawReadingPage();
        }
      } else if (ev == ButtonEvent::SelectLong) {
        bool removed = toggleMark(currentLine);
        drawReaderStatus();
        showToggleToast(removed);
      } else if (ev == ButtonEvent::SelectShort) {
        toastUntilMs = 0;
        state = State::ConfirmExit;
        Display::confirmExit();
      }
      break;
    }

    case State::Playing: {
      uint16_t key = currentKeyFrame() / Media::keyInterval();
      if (ev == ButtonEvent::DownPressed) {
        videoFrom(key + VIDEO_SKIP_S < Media::keyCount() ? key + VIDEO_SKIP_S : Media::keyCount() - 1);
      } else if (ev == ButtonEvent::UpPressed) {
        videoFrom(key > VIDEO_SKIP_S ? key - VIDEO_SKIP_S : 0);
      } else if (ev == ButtonEvent::SelectLong) {
        bool removed = toggleMark(currentKeyFrame());
        if (removed) Display::mediaStripText(F("Removed"), COL_MUTED);
        else Display::mediaStripText(F(GLYPH_BOOKMARK " Bookmarked"), COL_PROG_DONE);
        stripMsgUntil = millis() + 900;
        if (!playing) stripMsgUntil = 0; // paused at the end: no frames will clear it
      } else if (ev == ButtonEvent::SelectShort) {
        state = State::ConfirmExit;
        Display::confirmExit();
      }
      break;
    }

    case State::Viewing: {
      if (ev == ButtonEvent::SelectLong) {
        showToggleToast(toggleMark(0));
      } else if (ev == ButtonEvent::SelectShort) {
        toastUntilMs = 0;
        state = State::ConfirmExit;
        Display::confirmExit();
      }
      break;
    }

    case State::ConfirmExit: {
      if (ev == ButtonEvent::DownPressed) {
        closeItem();
      } else if (ev == ButtonEvent::UpPressed) {
        // back to whatever the dialog is covering
        if (openEntry.kind == KIND_VIDEO) {
          state = State::Playing;
          videoFrom(currentKeyFrame() / Media::keyInterval());
        } else if (openEntry.kind == KIND_IMAGE) {
          state = State::Viewing;
          drawImage();
        } else {
          state = State::Reading;
          drawReadingPage();
        }
      }
      // Select is ignored here: only the up/down buttons answer the dialog.
      break;
    }
  }
}
