#pragma once
#include <stdint.h>

// Every color and every layout number the UI uses lives here, and only
// here. tools/simulator/simulate.py parses this file (and Font.h) to render
// the same screens on a PC, so keep entries as plain one-line #defines:
// integers, RGB565(r, g, b) with 8-bit channels, or the name of another
// define.
//
// Screen is landscape, 160x128. Text is the 6x8 cell font from Font.h.

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// ---------------------------------------------------------------------------
// Colors -- library and system screens (dark slate)
// ---------------------------------------------------------------------------
#define COL_BG RGB565(0x0F, 0x17, 0x2A)
#define COL_HEADING RGB565(0xF8, 0xFA, 0xFC)
#define COL_TEXT RGB565(0xCB, 0xD5, 0xE1)
#define COL_MUTED RGB565(0x64, 0x74, 0x8B)
#define COL_ACCENT RGB565(0x38, 0xBD, 0xF8)
#define COL_DIVIDER RGB565(0x1E, 0x29, 0x3B)
#define COL_SEL_BG RGB565(0x1E, 0x3A, 0x5F)
#define COL_SEL_TEXT RGB565(0xFF, 0xFF, 0xFF)
#define COL_TRACK RGB565(0x1E, 0x29, 0x3B)
#define COL_THUMB RGB565(0x38, 0xBD, 0xF8)
#define COL_ERROR RGB565(0xF8, 0x71, 0x71)

// Reading progress shown per book in the list
#define COL_PROG_NEW RGB565(0x47, 0x55, 0x69)
#define COL_PROG_MID RGB565(0xFB, 0xBF, 0x24)
#define COL_PROG_DONE RGB565(0x4A, 0xDE, 0x80)

// Book "spine" chips beside each title, cycled by catalog index
#define SPINE_COUNT 7
#define COL_SPINE_0 RGB565(0xF8, 0x71, 0x71)
#define COL_SPINE_1 RGB565(0xFB, 0x92, 0x3C)
#define COL_SPINE_2 RGB565(0xFA, 0xCC, 0x15)
#define COL_SPINE_3 RGB565(0x4A, 0xDE, 0x80)
#define COL_SPINE_4 RGB565(0x22, 0xD3, 0xEE)
#define COL_SPINE_5 RGB565(0x81, 0x8C, 0xF8)
#define COL_SPINE_6 RGB565(0xF4, 0x72, 0xB6)

// ---------------------------------------------------------------------------
// Colors -- reader (warm paper)
// ---------------------------------------------------------------------------
#define COL_PAPER RGB565(0xFB, 0xF3, 0xE4)
#define COL_INK RGB565(0x3A, 0x2E, 0x2A)
#define COL_PAPER_MUTED RGB565(0x9C, 0x8B, 0x74)
#define COL_BAR_TRACK RGB565(0xEA, 0xDF, 0xC8)
#define COL_BAR_FILL RGB565(0xD9, 0x62, 0x2B)
#define COL_RIBBON RGB565(0xDC, 0x26, 0x26)

// Dialog and toast drawn over the page
#define COL_SHADOW RGB565(0xDD, 0xD0, 0xB8)
#define COL_CARD RGB565(0xFF, 0xFF, 0xFF)
#define COL_CARD_BORDER RGB565(0xE2, 0xD6, 0xBE)
#define COL_BTN_NO_BG RGB565(0xF1, 0xEA, 0xDA)
#define COL_BTN_NO_TEXT COL_INK
#define COL_BTN_YES_BG COL_BAR_FILL
#define COL_BTN_YES_TEXT RGB565(0xFF, 0xFF, 0xFF)
#define COL_TOAST_BG RGB565(0x16, 0xA3, 0x4A)
#define COL_TOAST_TEXT RGB565(0xFF, 0xFF, 0xFF)

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
#define SCREEN_W 160
#define SCREEN_H 128
#define GLYPH_W 6 // 5px glyph + 1px spacing column
#define GLYPH_H 8

// Library header band: "Library" on the left, "3/52" on the right
#define LIB_HEADER_H 20
#define LIB_PAD_X 8        // left/right text inset from the bezel
#define LIB_HEADER_TEXT_Y 6
#define LIB_UNDERLINE_Y 16 // accent bar under the "Library" heading
#define LIB_UNDERLINE_H 2

// Library list rows
#define LIB_LIST_Y 22
#define LIB_ROW_H 14
#define LIB_ROWS 7 // 22 + 7*14 = 120 -> 8px bottom padding
#define LIB_ROW_X 4 // highlight pill spans LIB_ROW_X .. LIB_ROW_X+LIB_ROW_W-1
#define LIB_ROW_W 148
#define LIB_ROW_TEXT_Y 3 // text inset from the top of its row
#define LIB_SPINE_X 9
#define LIB_SPINE_W 3
#define LIB_TITLE_X 17
#define LIB_PCT_W 30        // right-hand column holding "100%"
#define LIB_PCT_PAD_R 5     // gap between the percentage and the pill's right edge
#define LIB_TITLE_CHARS 16  // longer titles end in an ellipsis
#define LIB_SCROLL_X 154
#define LIB_SCROLL_W 2
#define LIB_THUMB_MIN_H 6

// Reader page
#define RD_PAD_X 8
#define RD_TOP 6
#define RD_LINE_H 10     // 8px glyph + 2px leading
#define RD_LINE_TEXT_Y 1 // glyph offset inside its line band
#define RD_LINES 10      // 6 + 10*10 = 106
#define RD_COLS 24       // (160 - 2*8) / 6
#define RD_BAR_Y 110
#define RD_BAR_H 3
#define RD_STATUS_Y 116
#define RD_STATUS_TITLE_CHARS 14

// "Back to library?" dialog
#define DLG_W 132
#define DLG_H 64
#define DLG_SHADOW 3
#define DLG_TITLE_Y 9
#define DLG_SUB_Y 21
#define DLG_BTN_Y 38
#define DLG_BTN_W 48
#define DLG_BTN_H 16
#define DLG_BTN_PAD 8

// Toast pill
#define TOAST_W 84
#define TOAST_H 18
#define TOAST_Y 86

// Full-screen messages
#define MSG_LINE1_Y 50
#define MSG_BAR_Y 62
#define MSG_BAR_W 24
#define MSG_LINE2_Y 70
