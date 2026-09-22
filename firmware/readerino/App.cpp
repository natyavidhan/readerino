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

  void redrawLibrary() {
    std::vector<String> titles;
    std::vector<int> progress;
    titles.push_back(String("* Bookmarks"));
    progress.push_back(-1);
    int n = Storage::bookCount();
    for (int i = 0; i < n; i++) {
      CatalogEntry e;
      if (!Storage::getEntry(i, e)) continue;
      titles.push_back(e.title);
      progress.push_back(progressPercent(e));
    }
    Display::showLibrary(titles, progress, librarySelection, sdError);
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

  void redrawBookmarks() {
    if (bookmarkRefs.empty()) {
      Display::showMessage("No bookmarks yet", "Hold select to add");
      return;
    }
    std::vector<String> labels;
    labels.push_back(String("< Back"));
    for (auto &r : bookmarkRefs) labels.push_back(r.label);
    Display::showBookmarksList(labels, bookmarkSelection);
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
