#include "Display.h"
#include "Config.h"
#include <Adafruit_SH110X.h>
#include <Wire.h>

namespace {
  Adafruit_SH1106G oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
  const int MENU_ROW_HEIGHT = 10; // 8px glyph + 1px top/bottom padding
  const int MENU_VISIBLE_ROWS = OLED_HEIGHT / MENU_ROW_HEIGHT;

  // Shared scrollable-list-with-highlight renderer used by both the
  // library and the bookmarks screens. progressPct is nullable: pass
  // nullptr for a plain list with no right-aligned progress column.
  void drawList(const std::vector<String> &labels, const std::vector<int> *progressPct, int selectedIndex) {
    oled.clearDisplay();
    oled.setTextSize(1);

    int total = (int)labels.size();
    int windowStart = 0;
    if (selectedIndex >= MENU_VISIBLE_ROWS) windowStart = selectedIndex - MENU_VISIBLE_ROWS + 1;
    int maxStart = total - MENU_VISIBLE_ROWS;
    if (maxStart < 0) maxStart = 0;
    if (windowStart > maxStart) windowStart = maxStart;

    for (int row = 0; row < MENU_VISIBLE_ROWS; row++) {
      int idx = windowStart + row;
      if (idx >= total) break;
      int y = row * MENU_ROW_HEIGHT;
      if (idx == selectedIndex) {
        oled.fillRect(0, y, OLED_WIDTH, MENU_ROW_HEIGHT, SH110X_WHITE);
        oled.setTextColor(SH110X_BLACK);
      } else {
        oled.setTextColor(SH110X_WHITE);
      }

      String progStr = "";
      int maxChars = CHARS_PER_LINE;
      if (progressPct && idx < (int)progressPct->size() && (*progressPct)[idx] >= 0) {
        progStr = String((*progressPct)[idx]) + "%";
        maxChars -= (int)progStr.length() + 1;
      }

      String label = labels[idx];
      if ((int)label.length() > maxChars) label = label.substring(0, maxChars);
      oled.setCursor(2, y + 1);
      oled.print(label);

      if (progStr.length() > 0) {
        int px = OLED_WIDTH - 2 - (int)progStr.length() * 6;
        oled.setCursor(px, y + 1);
        oled.print(progStr);
      }
    }
    oled.display();
  }
}

bool Display::begin() {
  Wire.begin(); // SDA=21, SCL=22 (ESP32 defaults)
  if (!oled.begin(OLED_I2C_ADDR, true)) return false;
  oled.setRotation(0);
  oled.clearDisplay();
  oled.display();
  return true;
}

void Display::showLibrary(const std::vector<String> &titles, const std::vector<int> &progressPct, int selectedIndex, bool sdError) {
  if (sdError) {
    showMessage("SD/catalog error", "Press any button");
    return;
  }
  if (titles.empty()) {
    showMessage("No books found", "Push a library from");
    return;
  }
  drawList(titles, &progressPct, selectedIndex);
}

void Display::showBookmarksList(const std::vector<String> &labels, int selectedIndex) {
  drawList(labels, nullptr, selectedIndex);
}

void Display::showReadingPage(const std::vector<String> &lines, int pageIndex, int pageCount, bool isBookmarked) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SH110X_WHITE);

  for (size_t row = 0; row < lines.size() && row < (size_t)LINES_PER_PAGE; row++) {
    oled.setCursor(0, row * 8);
    oled.print(lines[row]);
  }

  int statusY = LINES_PER_PAGE * 8;
  oled.drawFastHLine(0, statusY, OLED_WIDTH, SH110X_WHITE);
  oled.setCursor(0, statusY + 1);
  oled.print(pageIndex + 1);
  oled.print("/");
  oled.print(pageCount < 1 ? 1 : pageCount);
  if (isBookmarked) {
    oled.setCursor(OLED_WIDTH - 6, statusY + 1);
    oled.print("*");
  }
  oled.display();
}

void Display::showConfirmExit() {
  int w = 120, h = 28;
  int x = (OLED_WIDTH - w) / 2;
  int y = (OLED_HEIGHT - h) / 2;
  oled.fillRect(x, y, w, h, SH110X_BLACK);
  oled.drawRect(x, y, w, h, SH110X_WHITE);
  oled.setTextColor(SH110X_WHITE);
  oled.setCursor(x + 4, y + 6);
  oled.print("Back to menu?");

  // "No" (1st/up button, cancel) on the left, "Yes" (3rd/down button,
  // confirm) on the right, matching the physical left-to-right button order.
  const int charW = 6; // default font cell width at textSize 1
  int rowY = y + 18;
  oled.setCursor(x + 8, rowY);
  oled.print("No");
  oled.setCursor(x + w - 8 - 3 * charW, rowY);
  oled.print("Yes");
  oled.display();
}

void Display::showToast(const char *message) {
  int w = 100, h = 16;
  int x = (OLED_WIDTH - w) / 2;
  int y = (OLED_HEIGHT - h) / 2;
  oled.fillRect(x, y, w, h, SH110X_BLACK);
  oled.drawRect(x, y, w, h, SH110X_WHITE);
  oled.setTextColor(SH110X_WHITE);
  oled.setCursor(x + 4, y + 4);
  oled.print(message);
  oled.display();
}

void Display::showMessage(const char *line1, const char *line2) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SH110X_WHITE);
  oled.setCursor(0, 0);
  oled.println(line1);
  if (line2) oled.println(line2);
  oled.display();
}
