#pragma once
#include <Arduino.h>
#include <vector>

namespace Display {
  bool begin();

  // Both list screens take an already-windowed slice (at most
  // MENU_VISIBLE_ROWS entries — the caller is responsible for scrolling
  // math and fetching only the visible rows). highlightRow is the selected
  // row's index within that slice. marqueeOffsetPx scrolls the highlighted
  // row's label left by that many pixels when it overflows the row width;
  // 0 means "at rest" (drawn truncated, same as any other row).

  // titles[0] is conventionally "* Bookmarks". progressPct is parallel to
  // titles; pass -1 for a row with no progress indicator.
  void showLibrary(const std::vector<String> &titles, const std::vector<int> &progressPct, int highlightRow, int marqueeOffsetPx, bool sdError);

  // labels[0] is conventionally "< Back". No progress column.
  void showBookmarksList(const std::vector<String> &labels, int highlightRow, int marqueeOffsetPx);

  void showReadingPage(const std::vector<String> &lines, int pageIndex, int pageCount, bool isBookmarked);
  void showConfirmExit();
  void showToast(const char *message); // brief overlay, e.g. "Bookmarked!"
  void showMessage(const char *line1, const char *line2 = nullptr);
}
