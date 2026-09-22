#pragma once
#include <Arduino.h>
#include <vector>

namespace Display {
  bool begin();

  // Home screen: titles[0] is conventionally "* Bookmarks", the rest are
  // book titles from the catalog. progressPct is parallel to titles; pass
  // -1 for a row with no progress indicator (e.g. the Bookmarks row).
  void showLibrary(const std::vector<String> &titles, const std::vector<int> &progressPct, int selectedIndex, bool sdError);

  // Flat list of bookmark labels (no progress column). labels[0] is
  // conventionally "< Back".
  void showBookmarksList(const std::vector<String> &labels, int selectedIndex);

  void showReadingPage(const std::vector<String> &lines, int pageIndex, int pageCount, bool isBookmarked);
  void showConfirmExit();
  void showToast(const char *message); // brief overlay, e.g. "Bookmarked!"
  void showMessage(const char *line1, const char *line2 = nullptr);
}
