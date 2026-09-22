#include "App.h"
#include "Config.h"
#include "Buttons.h"
#include "Display.h"
#include "Storage.h"
#include "Reader.h"
#include <vector>
#include <Arduino.h>

namespace {
  enum class State { Menu, Reading, ConfirmExit };

  State state = State::Menu;
  std::vector<BookInfo> books;
  int menuSelection = 0;
  bool sdError = false;

  Reader reader;
  int currentLine = 0;        // top line index of the current reading page (always page-aligned)
  std::vector<int> bookmarks; // cached bookmarks for the currently open book
  unsigned long toastUntilMs = 0;

  bool isBookmarkedHere() {
    for (int b : bookmarks) {
      if (b == currentLine) return true;
    }
    return false;
  }

  // Redraws the menu from the already-cached book list. Cheap: touches only
  // the display, never the SD card. Use this for anything that happens on
  // every button press (navigation, returning from reading).
  void redrawMenu() {
    std::vector<String> names;
    for (auto &b : books) names.push_back(b.name);
    Display::showMenu(names, menuSelection, sdError);
  }

  // Re-scans the SD card's directory and redraws. This is comparatively
  // expensive (a full directory walk over every file on the card), so it
  // must only run when the file list can actually have changed: at boot,
  // when retrying after an SD error, and after a host file transfer.
  void rescanBooks() {
    Storage::listBooks(books);
    if (menuSelection >= (int)books.size()) {
      menuSelection = books.empty() ? 0 : (int)books.size() - 1;
    }
  }

  void refreshMenu() {
    rescanBooks();
    redrawMenu();
  }

  void drawReadingPage() {
    std::vector<String> lines;
    reader.renderPage(currentLine, lines);
    int page = currentLine / LINES_PER_PAGE;
    Display::showReadingPage(lines, page, reader.pageCount(), isBookmarkedHere());
  }

  void openSelectedBook() {
    if (books.empty()) return;
    String path = books[menuSelection].name;
    Display::showMessage("Loading...", path.c_str());
    if (!reader.open(path)) {
      Display::showMessage("Failed to open", path.c_str());
      delay(1200);
      refreshMenu();
      return;
    }
    int saved = Storage::getSavedPosition(path);
    int total = reader.totalLines();
    if (saved < 0 || saved >= total) saved = 0;
    currentLine = (saved / LINES_PER_PAGE) * LINES_PER_PAGE; // snap to a page boundary
    bookmarks = Storage::getBookmarks(path);
    state = State::Reading;
    drawReadingPage();
  }

  void exitToMenu(bool save) {
    if (save) Storage::savePosition(reader.path(), currentLine);
    reader.close();
    state = State::Menu;
    redrawMenu(); // the SD file list can't have changed just from reading
  }
}

void App::begin() {
  Buttons::begin();
  Display::begin();
  sdError = !Storage::begin();
  refreshMenu();
}

void App::onFilesChanged() {
  rescanBooks();
  if (state == State::Menu) redrawMenu();
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
    case State::Menu: {
      if (books.empty()) {
        // retry the SD card / rescan on any press when nothing is available
        sdError = !Storage::begin();
        refreshMenu();
        break;
      }
      if (ev == ButtonEvent::UpPressed) {
        menuSelection = (menuSelection - 1 + (int)books.size()) % (int)books.size();
        redrawMenu();
      } else if (ev == ButtonEvent::DownPressed) {
        menuSelection = (menuSelection + 1) % (int)books.size();
        redrawMenu();
      } else if (ev == ButtonEvent::SelectShort || ev == ButtonEvent::SelectLong) {
        openSelectedBook();
      }
      break;
    }

    case State::Reading: {
      if (ev == ButtonEvent::DownPressed) {
        int total = reader.totalLines();
        int next = currentLine + LINES_PER_PAGE;
        if (next < total) {
          currentLine = next;
          drawReadingPage();
        }
      } else if (ev == ButtonEvent::UpPressed) {
        int prev = currentLine - LINES_PER_PAGE;
        currentLine = prev < 0 ? 0 : prev;
        drawReadingPage();
      } else if (ev == ButtonEvent::SelectLong) {
        Storage::addBookmark(reader.path(), currentLine);
        bookmarks = Storage::getBookmarks(reader.path());
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
        exitToMenu(true);
      } else if (ev == ButtonEvent::UpPressed) {
        state = State::Reading;
        drawReadingPage();
      }
      // SelectShort/SelectLong are ignored here: only the 1st/3rd buttons act on this dialog.
      break;
    }
  }
}
