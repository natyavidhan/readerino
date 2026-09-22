#pragma once
#include <Arduino.h>
#include <vector>

namespace Display {
  bool begin();

  void showMenu(const std::vector<String> &names, int selectedIndex, bool sdError);
  void showReadingPage(const std::vector<String> &lines, int pageIndex, int pageCount, bool isBookmarked);
  void showConfirmExit();
  void showToast(const char *message); // brief overlay, e.g. "Bookmarked!"
  void showMessage(const char *line1, const char *line2 = nullptr);
}
