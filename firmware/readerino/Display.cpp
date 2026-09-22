#include "Display.h"
#include "Config.h"
#include <Adafruit_SH110X.h>
#include <Wire.h>

namespace {
  Adafruit_SH1106G oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

  // Draws an already-windowed list (<= MENU_VISIBLE_ROWS entries). The
  // highlighted row's label scrolls by marqueeOffsetPx when it overflows;
  // every other overflowing row is statically truncated with an ellipsis.
  void drawList(const std::vector<String> &labels, const std::vector<int> *progressPct, int highlightRow, int marqueeOffsetPx) {
    oled.clearDisplay();
    oled.setTextSize(1);

    for (int row = 0; row < (int)labels.size(); row++) {
      int y = row * MENU_ROW_HEIGHT;
      bool selected = (row == highlightRow);
      uint16_t bg = selected ? SH110X_WHITE : SH110X_BLACK;
      uint16_t fg = selected ? SH110X_BLACK : SH110X_WHITE;

      if (selected) oled.fillRect(0, y, OLED_WIDTH, MENU_ROW_HEIGHT, SH110X_WHITE);
      oled.setTextColor(fg);

      String progStr = "";
      int maxChars = CHARS_PER_LINE;
      if (progressPct && row < (int)progressPct->size() && (*progressPct)[row] >= 0) {
        progStr = String((*progressPct)[row]) + "%";
        maxChars -= (int)progStr.length() + 1;
      }

      const String &label = labels[row];
      bool overflowing = (int)label.length() > maxChars;

      if (selected && overflowing && marqueeOffsetPx > 0) {
        // actively scrolling: draw the full label shifted left by the
        // marquee offset; off-screen pixels are clipped by the GFX lib.
        oled.setCursor(2 - marqueeOffsetPx, y + 1);
        oled.print(label);
      } else {
        String shown = label;
        if (overflowing) {
          int cut = maxChars > 3 ? maxChars - 3 : maxChars;
          shown = label.substring(0, cut) + (maxChars > 3 ? "..." : "");
        }
        oled.setCursor(2, y + 1);
        oled.print(shown);
      }

      // mask over anything that scrolled under the progress column, then
      // draw the progress text on top
      if (progStr.length() > 0) {
        int progPx = (int)progStr.length() * CHAR_PX;
        int maskX = OLED_WIDTH - 2 - progPx - 2;
        oled.fillRect(maskX, y, OLED_WIDTH - maskX, MENU_ROW_HEIGHT, bg);
        oled.setTextColor(fg);
        oled.setCursor(OLED_WIDTH - 2 - progPx, y + 1);
        oled.print(progStr);
      }
    }
    oled.display();
  }
}

bool Display::begin() {
  Wire.begin(); // SDA=21, SCL=22 (ESP32 defaults)
  Wire.setClock(400000); // fast-mode I2C; SH1106 modules support it, cuts full-frame push time ~4x
  if (!oled.begin(OLED_I2C_ADDR, true)) return false;
  oled.setRotation(0);
  oled.clearDisplay();
  oled.display();
  return true;
}

void Display::showLibrary(const std::vector<String> &titles, const std::vector<int> &progressPct, int highlightRow, int marqueeOffsetPx, bool sdError) {
  if (sdError) {
    showMessage("SD/catalog error", "Press any button");
    return;
  }
  if (titles.empty()) {
    showMessage("No books found", "Push a library from");
    return;
  }
  drawList(titles, &progressPct, highlightRow, marqueeOffsetPx);
}

void Display::showBookmarksList(const std::vector<String> &labels, int highlightRow, int marqueeOffsetPx) {
  drawList(labels, nullptr, highlightRow, marqueeOffsetPx);
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

  int rowY = y + 18;
  oled.setCursor(x + 8, rowY);
  oled.print("No");
  oled.setCursor(x + w - 8 - 3 * CHAR_PX, rowY);
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
