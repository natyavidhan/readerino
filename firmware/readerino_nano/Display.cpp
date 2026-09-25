#include "Display.h"
#include "Config.h"
#include "Theme.h"
#include "Font.h"
#include "Tft.h"
#include <string.h>
#include <stdlib.h>

namespace {
  const uint16_t SPINES[SPINE_COUNT] PROGMEM = {
    COL_SPINE_0, COL_SPINE_1, COL_SPINE_2, COL_SPINE_3, COL_SPINE_4, COL_SPINE_5, COL_SPINE_6,
  };

  // Width of the right-hand "3/52" box in list headers.
  const uint8_t HEADER_RIGHT_W = 64;
  // Split of the reader's status row: title on the left, page on the right.
  const uint8_t STATUS_LEFT_W = 100;

  uint8_t textWidth(uint8_t chars) { return chars ? chars * GLYPH_W - 1 : 0; }

  // Copies src into dst (size maxChars+1); if it doesn't fit, cuts it to
  // maxChars-1 characters, drops trailing spaces and ends it with the
  // ellipsis glyph.
  void truncateInto(char *dst, const char *src, uint8_t maxChars) {
    uint8_t n = strlen(src);
    if (n <= maxChars) {
      memcpy(dst, src, n + 1);
      return;
    }
    n = maxChars - 1;
    while (n && src[n - 1] == ' ') n--;
    memcpy(dst, src, n);
    dst[n] = GLYPH_ELLIPSIS[0];
    dst[n + 1] = 0;
  }

  // Paints the 3-pixel L at one corner of a box to round it off. (x, y) is
  // the corner pixel; dx/dy (+1/-1) point into the box.
  void cutCorner(uint8_t x, uint8_t y, int8_t dx, int8_t dy, uint16_t color) {
    Tft::fillRect(x, y, 1, 1, color);
    Tft::fillRect(x + dx, y, 1, 1, color);
    Tft::fillRect(x, y + dy, 1, 1, color);
  }

  void cutCorners(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t outside) {
    cutCorner(x, y, 1, 1, outside);
    cutCorner(x + w - 1, y, -1, 1, outside);
    cutCorner(x, y + h - 1, 1, -1, outside);
    cutCorner(x + w - 1, y + h - 1, -1, -1, outside);
  }

  void messageImpl(const __FlashStringHelper *line1, const char *line2, bool line2InFlash, uint16_t accent) {
    const char *l1 = (const char *)line1;
    Tft::fillRect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
    Tft::textBoxP(0, MSG_LINE1_Y, SCREEN_W, GLYPH_H, (SCREEN_W - textWidth(strlen_P(l1))) / 2, 0,
                  l1, COL_HEADING, COL_BG);
    Tft::fillRect((SCREEN_W - MSG_BAR_W) / 2, MSG_BAR_Y, MSG_BAR_W, 2, accent);
    if (!line2) return;
    if (line2InFlash) {
      Tft::textBoxP(0, MSG_LINE2_Y, SCREEN_W, GLYPH_H, (SCREEN_W - textWidth(strlen_P(line2))) / 2, 0,
                    line2, COL_MUTED, COL_BG);
    } else {
      char buf[RD_COLS + 1];
      truncateInto(buf, line2, RD_COLS);
      Tft::textBox(0, MSG_LINE2_Y, SCREEN_W, GLYPH_H, (SCREEN_W - textWidth(strlen(buf))) / 2, 0,
                   buf, COL_MUTED, COL_BG);
    }
  }
}

void Display::begin() {
  Tft::begin();
  Tft::fillRect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
}

// ---------------------------------------------------------------------------
// Home menu
// ---------------------------------------------------------------------------

namespace {
  const char HOME_LABEL_0[] PROGMEM = GLYPH_BOOK;
  const char HOME_LABEL_1[] PROGMEM = GLYPH_BOOKMARK;
  const char HOME_LABEL_2[] PROGMEM = GLYPH_GEAR;
  const char HOME_TEXT_0[] PROGMEM = "Library";
  const char HOME_TEXT_1[] PROGMEM = "Bookmarks";
  const char HOME_TEXT_2[] PROGMEM = "Settings";
  const char *const HOME_ICONS[HOME_ITEMS] PROGMEM = {HOME_LABEL_0, HOME_LABEL_1, HOME_LABEL_2};
  const char *const HOME_TEXTS[HOME_ITEMS] PROGMEM = {HOME_TEXT_0, HOME_TEXT_1, HOME_TEXT_2};
  const uint16_t HOME_ICON_COLORS[HOME_ITEMS] PROGMEM = {COL_ICON_LIBRARY, COL_ICON_BOOKMARKS, COL_ICON_SETTINGS};
}

void Display::homeItem(uint8_t item, bool selected) {
  const uint8_t y = HOME_BTN_Y + item * HOME_BTN_STEP;
  const uint16_t bg = selected ? COL_MENU_SEL_BG : COL_MENU_BG;
  const uint16_t fg = selected ? COL_MENU_SEL_TEXT : COL_MENU_TEXT;
  const uint8_t ty = (HOME_BTN_H - 7) / 2;
  Tft::textBoxP(HOME_BTN_X, y, HOME_BTN_TEXT_X, HOME_BTN_H, HOME_BTN_ICON_X, ty,
                (const char *)pgm_read_ptr(&HOME_ICONS[item]),
                selected ? fg : pgm_read_word(&HOME_ICON_COLORS[item]), bg);
  Tft::textBoxP(HOME_BTN_X + HOME_BTN_TEXT_X, y, HOME_BTN_W - HOME_BTN_TEXT_X, HOME_BTN_H, 0, ty,
                (const char *)pgm_read_ptr(&HOME_TEXTS[item]), fg, bg);
  cutCorners(HOME_BTN_X, y, HOME_BTN_W, HOME_BTN_H, COL_BG);
}

void Display::home(uint8_t selected) {
  static const char WORD[] PROGMEM = "READERINO";
  const uint8_t n = sizeof(WORD) - 1;
  const uint8_t cell = GLYPH_W * 2, th = GLYPH_H * 2;
  const uint8_t x0 = (SCREEN_W - (n * cell - 2)) / 2;
  const uint8_t ty = HOME_TITLE_Y;

  Tft::fillRect(0, 0, SCREEN_W, ty, COL_BG);
  Tft::fillRect(0, ty, x0, th, COL_BG);
  char ch[2] = {0, 0};
  for (uint8_t i = 0; i < n; i++) {
    ch[0] = pgm_read_byte(&WORD[i]);
    Tft::textBox2x(x0 + i * cell, ty, cell, th, 0, 0, ch, pgm_read_word(&SPINES[i % SPINE_COUNT]), COL_BG);
  }
  Tft::fillRect(x0 + n * cell, ty, SCREEN_W - x0 - n * cell, th, COL_BG);
  Tft::textBoxP(0, ty + th, SCREEN_W, HOME_BTN_Y - ty - th, (SCREEN_W - textWidth(15)) / 2,
                HOME_TAG_Y - ty - th, PSTR("pocket e-reader"), COL_MUTED, COL_BG);

  for (uint8_t i = 0; i < HOME_ITEMS; i++) {
    const uint8_t y = HOME_BTN_Y + i * HOME_BTN_STEP;
    Tft::fillRect(0, y, HOME_BTN_X, HOME_BTN_H, COL_BG);
    Tft::fillRect(HOME_BTN_X + HOME_BTN_W, y, SCREEN_W - HOME_BTN_X - HOME_BTN_W, HOME_BTN_H, COL_BG);
    homeItem(i, i == selected);
    const uint8_t gapY = y + HOME_BTN_H;
    Tft::fillRect(0, gapY, SCREEN_W, i < HOME_ITEMS - 1 ? HOME_BTN_STEP - HOME_BTN_H : SCREEN_H - gapY, COL_BG);
  }
}

// ---------------------------------------------------------------------------
// Lists (library, bookmarks) and settings
// ---------------------------------------------------------------------------

void Display::listHeader(const __FlashStringHelper *title, int selected, int count, bool full) {
  if (full) {
    const char *t = (const char *)title;
    Tft::textBoxP(0, 0, SCREEN_W - HEADER_RIGHT_W, LIB_HEADER_H, LIB_PAD_X, LIB_HEADER_TEXT_Y,
                  t, COL_HEADING, COL_BG);
    Tft::fillRect(LIB_PAD_X, LIB_UNDERLINE_Y, textWidth(strlen_P(t)), LIB_UNDERLINE_H, COL_ACCENT);
    Tft::fillRect(0, LIB_HEADER_H, SCREEN_W, LIB_LIST_Y - LIB_HEADER_H, COL_BG);
  }
  char s[12] = "";
  if (count > 0) {
    itoa(selected + 1, s, 10);
    strcat(s, "/");
    itoa(count, s + strlen(s), 10);
  }
  Tft::textBox(SCREEN_W - HEADER_RIGHT_W, 0, HEADER_RIGHT_W, LIB_HEADER_H,
               HEADER_RIGHT_W - LIB_PAD_X - textWidth(strlen(s)), LIB_HEADER_TEXT_Y, s, COL_MUTED, COL_BG);
}

void Display::listRow(uint8_t slot, const char *title, const char *right, uint16_t rightColor,
                      int spineIndex, bool selected) {
  uint8_t y = LIB_LIST_Y + slot * LIB_ROW_H;
  uint16_t bg = selected ? COL_SEL_BG : COL_BG;
  uint16_t fg = selected ? COL_SEL_TEXT : COL_TEXT;
  const uint8_t titleW = LIB_ROW_W - LIB_PCT_W;

  Tft::fillRect(0, y, LIB_ROW_X, LIB_ROW_H, COL_BG);
  char buf[LIB_TITLE_CHARS + 1];
  truncateInto(buf, title, LIB_TITLE_CHARS);
  Tft::textBox(LIB_ROW_X, y, titleW, LIB_ROW_H, LIB_TITLE_X - LIB_ROW_X, LIB_ROW_TEXT_Y, buf, fg, bg);
  Tft::textBox(LIB_ROW_X + titleW, y, LIB_PCT_W, LIB_ROW_H,
               LIB_PCT_W - LIB_PCT_PAD_R - textWidth(strlen(right)), LIB_ROW_TEXT_Y, right, rightColor, bg);

  Tft::fillRect(LIB_SPINE_X, y + LIB_ROW_TEXT_Y, LIB_SPINE_W, GLYPH_H - 1,
                pgm_read_word(&SPINES[spineIndex % SPINE_COUNT]));
  if (selected) cutCorners(LIB_ROW_X, y, LIB_ROW_W, LIB_ROW_H, COL_BG);
}

void Display::listRowTitle(uint8_t slot, const char *title, uint16_t offset) {
  const uint8_t y = LIB_LIST_Y + slot * LIB_ROW_H;
  const uint8_t w = LIB_ROW_X + LIB_ROW_W - LIB_PCT_W - LIB_TITLE_X;
  if (offset == 0) {
    char buf[LIB_TITLE_CHARS + 1];
    truncateInto(buf, title, LIB_TITLE_CHARS);
    Tft::textBox(LIB_TITLE_X, y, w, LIB_ROW_H, 0, LIB_ROW_TEXT_Y, buf, COL_SEL_TEXT, COL_SEL_BG);
  } else {
    Tft::textBoxLoop(LIB_TITLE_X, y, w, LIB_ROW_H, LIB_ROW_TEXT_Y, title, MARQUEE_GAP, offset,
                     COL_SEL_TEXT, COL_SEL_BG);
  }
}

void Display::listEmptyRow(uint8_t slot) {
  Tft::fillRect(0, LIB_LIST_Y + slot * LIB_ROW_H, LIB_ROW_X + LIB_ROW_W, LIB_ROW_H, COL_BG);
}

void Display::listScrollbar(int windowStart, int count) {
  const uint8_t top = LIB_LIST_Y;
  const uint8_t h = LIB_ROWS * LIB_ROW_H;
  const uint8_t left = LIB_ROW_X + LIB_ROW_W;
  Tft::fillRect(left, top, LIB_SCROLL_X - left, h, COL_BG);
  Tft::fillRect(LIB_SCROLL_X + LIB_SCROLL_W, top, SCREEN_W - LIB_SCROLL_X - LIB_SCROLL_W, h, COL_BG);
  if (count <= LIB_ROWS) {
    Tft::fillRect(LIB_SCROLL_X, top, LIB_SCROLL_W, h, COL_BG);
    return;
  }
  uint8_t thumbH = (uint16_t)h * LIB_ROWS / count;
  if (thumbH < LIB_THUMB_MIN_H) thumbH = LIB_THUMB_MIN_H;
  uint8_t thumbY = top + (long)(h - thumbH) * windowStart / (count - LIB_ROWS);
  Tft::fillRect(LIB_SCROLL_X, top, LIB_SCROLL_W, thumbY - top, COL_TRACK);
  Tft::fillRect(LIB_SCROLL_X, thumbY, LIB_SCROLL_W, thumbH, COL_THUMB);
  Tft::fillRect(LIB_SCROLL_X, thumbY + thumbH, LIB_SCROLL_W, top + h - thumbY - thumbH, COL_TRACK);
}

void Display::listFooter(const __FlashStringHelper *hint) {
  const char *t = (const char *)hint;
  const uint8_t y = LIB_LIST_Y + LIB_ROWS * LIB_ROW_H;
  Tft::fillRect(0, y, SCREEN_W, LIB_HINT_Y - y, COL_BG);
  Tft::textBoxP(0, LIB_HINT_Y, SCREEN_W, SCREEN_H - LIB_HINT_Y, (SCREEN_W - textWidth(strlen_P(t))) / 2, 0,
                t, COL_MUTED, COL_BG);
}

void Display::listEmpty(const __FlashStringHelper *line1, const __FlashStringHelper *line2) {
  const char *l1 = (const char *)line1;
  const char *l2 = (const char *)line2;
  const uint8_t bottom = LIB_LIST_Y + LIB_ROWS * LIB_ROW_H;
  const uint8_t y2 = LIB_EMPTY_Y + GLYPH_H + 6;
  Tft::fillRect(0, LIB_LIST_Y, SCREEN_W, LIB_EMPTY_Y - LIB_LIST_Y, COL_BG);
  Tft::textBoxP(0, LIB_EMPTY_Y, SCREEN_W, y2 - LIB_EMPTY_Y, (SCREEN_W - textWidth(strlen_P(l1))) / 2, 0,
                l1, COL_TEXT, COL_BG);
  Tft::textBoxP(0, y2, SCREEN_W, bottom - y2, (SCREEN_W - textWidth(strlen_P(l2))) / 2, 0,
                l2, COL_MUTED, COL_BG);
}

void Display::settings() {
  listHeader(F("Settings"), 0, 0, true);
  listEmpty(F("Nothing here yet"), F("Themes are coming"));
  listFooter(F("Press " GLYPH_BUTTON " to go back"));
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

void Display::readerTop() {
  Tft::fillRect(0, 0, SCREEN_W, RD_TOP, COL_PAPER);
}

void Display::readerLine(uint8_t slot, const char *text) {
  Tft::textBox(0, RD_TOP + slot * RD_LINE_H, SCREEN_W, RD_LINE_H, RD_PAD_X, RD_LINE_TEXT_Y,
               text, COL_INK, COL_PAPER);
}

void Display::readerStatus(const char *title, int page, int pages, bool bookmarked) {
  const uint8_t y0 = RD_TOP + RD_LINES * RD_LINE_H;
  const uint8_t barW = SCREEN_W - 2 * RD_PAD_X;
  Tft::fillRect(0, y0, SCREEN_W, RD_BAR_Y - y0, COL_PAPER);

  uint8_t fillW = pages > 0 ? (long)barW * page / pages : 0;
  Tft::fillRect(0, RD_BAR_Y, RD_PAD_X, RD_BAR_H, COL_PAPER);
  Tft::fillRect(RD_PAD_X, RD_BAR_Y, fillW, RD_BAR_H, COL_BAR_FILL);
  Tft::fillRect(RD_PAD_X + fillW, RD_BAR_Y, barW - fillW, RD_BAR_H, COL_BAR_TRACK);
  Tft::fillRect(SCREEN_W - RD_PAD_X, RD_BAR_Y, RD_PAD_X, RD_BAR_H, COL_PAPER);
  Tft::fillRect(0, RD_BAR_Y + RD_BAR_H, SCREEN_W, RD_STATUS_Y - RD_BAR_Y - RD_BAR_H, COL_PAPER);

  const uint8_t h = SCREEN_H - RD_STATUS_Y;
  char buf[RD_STATUS_TITLE_CHARS + 1];
  truncateInto(buf, title, RD_STATUS_TITLE_CHARS);
  Tft::textBox(0, RD_STATUS_Y, STATUS_LEFT_W, h, RD_PAD_X, 0, buf, COL_PAPER_MUTED, COL_PAPER);

  char s[12];
  itoa(page, s, 10);
  strcat(s, "/");
  itoa(pages, s + strlen(s), 10);
  const uint8_t rightW = SCREEN_W - STATUS_LEFT_W;
  uint8_t tx = rightW - RD_PAD_X - textWidth(strlen(s));
  Tft::textBox(STATUS_LEFT_W, RD_STATUS_Y, rightW, h, tx, 0, s, COL_INK, COL_PAPER);
  if (bookmarked) {
    Tft::textBoxP(STATUS_LEFT_W + tx - 10, RD_STATUS_Y, GLYPH_W, GLYPH_H, 0, 0, PSTR(GLYPH_BOOKMARK),
                  COL_RIBBON, COL_PAPER);
  }
}

// ---------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------

void Display::confirmExit() {
  const uint8_t w = DLG_W, h = DLG_H, sh = DLG_SHADOW;
  const uint8_t x = (SCREEN_W - w) / 2;
  const uint8_t y = (SCREEN_H - h) / 2;

  // drop shadow: right and bottom strips, their outer corners rounded
  Tft::fillRect(x + w, y + sh, sh, h, COL_SHADOW);
  Tft::fillRect(x + sh, y + h, w, sh, COL_SHADOW);
  cutCorner(x + w + sh - 1, y + sh, -1, 1, COL_PAPER);
  cutCorner(x + sh, y + h + sh - 1, 1, -1, COL_PAPER);
  cutCorner(x + w + sh - 1, y + h + sh - 1, -1, -1, COL_PAPER);

  // card: 1px border, white body, rounded corners (bottom-right sits on the shadow)
  Tft::fillRect(x, y, w, h, COL_CARD_BORDER);
  Tft::fillRect(x + 1, y + 1, w - 2, h - 2, COL_CARD);
  cutCorner(x, y, 1, 1, COL_PAPER);
  cutCorner(x + w - 1, y, -1, 1, COL_PAPER);
  cutCorner(x, y + h - 1, 1, -1, COL_PAPER);
  cutCorner(x + w - 1, y + h - 1, -1, -1, COL_SHADOW);
  Tft::fillRect(x + 1, y + 1, 1, 1, COL_CARD_BORDER);
  Tft::fillRect(x + w - 2, y + 1, 1, 1, COL_CARD_BORDER);
  Tft::fillRect(x + 1, y + h - 2, 1, 1, COL_CARD_BORDER);
  Tft::fillRect(x + w - 2, y + h - 2, 1, 1, COL_CARD_BORDER);

  Tft::textBoxP(x + 1, y + DLG_TITLE_Y, w - 2, GLYPH_H, (w - 2 - textWidth(16)) / 2, 0,
                PSTR("Back to library?"), COL_INK, COL_CARD);
  Tft::textBoxP(x + 1, y + DLG_SUB_Y, w - 2, GLYPH_H, (w - 2 - textWidth(19)) / 2, 0,
                PSTR("Your place is saved"), COL_PAPER_MUTED, COL_CARD);

  // Left = "No" (triangle / up button), right = "Yes" (square / down button)
  const uint8_t by = y + DLG_BTN_Y;
  const uint8_t noX = x + DLG_BTN_PAD;
  const uint8_t yesX = x + w - DLG_BTN_PAD - DLG_BTN_W;
  Tft::textBoxP(noX, by, DLG_BTN_W, DLG_BTN_H, (DLG_BTN_W - textWidth(4)) / 2, (DLG_BTN_H - 7) / 2,
                PSTR(GLYPH_BTN_UP " No"), COL_BTN_NO_TEXT, COL_BTN_NO_BG);
  cutCorners(noX, by, DLG_BTN_W, DLG_BTN_H, COL_CARD);
  Tft::textBoxP(yesX, by, DLG_BTN_W, DLG_BTN_H, (DLG_BTN_W - textWidth(5)) / 2, (DLG_BTN_H - 7) / 2,
                PSTR(GLYPH_BTN_DOWN " Yes"), COL_BTN_YES_TEXT, COL_BTN_YES_BG);
  cutCorners(yesX, by, DLG_BTN_W, DLG_BTN_H, COL_CARD);
}

void Display::toast(const __FlashStringHelper *text, uint16_t bg) {
  const char *t = (const char *)text;
  const uint8_t x = (SCREEN_W - TOAST_W) / 2;
  Tft::textBoxP(x, TOAST_Y, TOAST_W, TOAST_H, (TOAST_W - textWidth(strlen_P(t))) / 2, (TOAST_H - 7) / 2,
                t, COL_TOAST_TEXT, bg);
  cutCorners(x, TOAST_Y, TOAST_W, TOAST_H, COL_PAPER);
}

void Display::message(const __FlashStringHelper *line1, const char *line2, uint16_t accent) {
  messageImpl(line1, line2, false, accent);
}

void Display::message(const __FlashStringHelper *line1, const __FlashStringHelper *line2, uint16_t accent) {
  messageImpl(line1, (const char *)line2, true, accent);
}
