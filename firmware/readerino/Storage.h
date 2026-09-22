#pragma once
#include <Arduino.h>
#include <vector>

struct BookInfo {
  String name; // filename with leading '/'
};

namespace Storage {
  bool begin();
  bool listBooks(std::vector<BookInfo> &out);

  // Bookmarks / reading position, keyed by filename, persisted to BOOKMARKS_FILE as JSON.
  int getSavedPosition(const String &filename); // returns 0 if none saved yet
  void savePosition(const String &filename, int lineIndex);

  std::vector<int> getBookmarks(const String &filename);
  void addBookmark(const String &filename, int lineIndex);
}
