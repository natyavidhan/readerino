"""Renders readerino's Nano screens on a PC, pixel for pixel.

Colors, layout and glyphs are parsed straight out of the firmware's
Theme.h and Font.h, and the two drawing primitives below (fill_rect,
text_box) behave exactly like Tft::fillRect / Tft::textBox on the device.
The screen functions mirror Display.cpp call for call, so a screenshot here
is what the 160x128 panel shows (colors are quantized to RGB565 the same
way the panel receives them).

    python tools/simulator/simulate.py            # writes tools/simulator/out/*.png
    python tools/simulator/simulate.py --scale 6  # bigger previews
    python tools/simulator/simulate.py --demo     # public-domain sample data
"""

import argparse
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware" / "readerino_nano"
sys.path.insert(0, str(ROOT / "packer"))


# ---------------------------------------------------------------------------
# Parsing the firmware headers
# ---------------------------------------------------------------------------

def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def parse_theme(path: Path) -> dict:
    raw = {}
    for line in path.read_text().splitlines():
        m = re.match(r"\s*#define\s+(\w+)\s+(.+?)\s*(//.*)?$", line)
        if m and m.group(1) != "RGB565":
            raw[m.group(1)] = m.group(2)

    values = {}

    def resolve(name):
        if name in values:
            return values[name]
        expr = raw[name]
        expr = re.sub(r"\b([A-Z_][A-Z0-9_]+)\b",
                      lambda m: str(resolve(m.group(1))) if m.group(1) in raw else m.group(1),
                      expr)
        values[name] = eval(expr, {"RGB565": rgb565})
        return values[name]

    for name in raw:
        resolve(name)
    return values


def parse_font(path: Path):
    text = path.read_text()
    first = int(re.search(r"#define FONT_FIRST (0x[0-9A-F]+)", text).group(1), 16)
    last = int(re.search(r"#define FONT_LAST (0x[0-9A-F]+)", text).group(1), 16)
    body = text[text.index("FONT[] PROGMEM"):]
    body = re.sub(r"//.*", "", body)
    data = [int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", body)]
    assert len(data) == (last - first + 1) * 5, "Font.h glyph table size mismatch"
    return first, last, data


T = parse_theme(FW / "Theme.h")
FONT_FIRST, FONT_LAST, FONT = parse_font(FW / "Font.h")
ELLIPSIS = "\x7f"
BOOKMARK = "\x80"
BOOK = "\x81"
GEAR = "\x82"
BUTTON = "\x83"


def to_rgb(c565):
    r = (c565 >> 11) & 0x1F
    g = (c565 >> 5) & 0x3F
    b = c565 & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


# ---------------------------------------------------------------------------
# Tft: the device's two primitives
# ---------------------------------------------------------------------------

class Tft:
    def __init__(self):
        self.w, self.h = T["SCREEN_W"], T["SCREEN_H"]
        self.img = Image.new("RGB", (self.w, self.h), (255, 0, 255))  # magenta = never drawn
        self.px = self.img.load()

    def fill_rect(self, x, y, w, h, color):
        rgb = to_rgb(color)
        for yy in range(max(y, 0), min(y + h, self.h)):
            for xx in range(max(x, 0), min(x + w, self.w)):
                self.px[xx, yy] = rgb

    def text_box(self, x, y, w, h, tx, ty, s, fg, bg, scale=1):
        """Fills a w*h box with bg and draws s at (tx, ty) inside it, clipped
        to the box -- the single streamed window Tft::textBox sends. scale 2
        doubles every glyph pixel (Tft::textBox2x)."""
        fg_rgb, bg_rgb = to_rgb(fg), to_rgb(bg)
        for r in range(h):
            gy = (r - ty) // scale if r >= ty else -1
            for c in range(w):
                gx = (c - tx) // scale if c >= tx else -1
                on = False
                if 0 <= gy < T["GLYPH_H"] and gx >= 0:
                    i, col = divmod(gx, T["GLYPH_W"])
                    if i < len(s) and col < 5:
                        code = ord(s[i])
                        if code < FONT_FIRST or code > FONT_LAST:
                            code = ord("?")
                        on = (FONT[(code - FONT_FIRST) * 5 + col] >> gy) & 1
                xx, yy = x + c, y + r
                if 0 <= xx < self.w and 0 <= yy < self.h:
                    self.px[xx, yy] = fg_rgb if on else bg_rgb


# ---------------------------------------------------------------------------
# Display: mirrors Display.cpp
# ---------------------------------------------------------------------------

def text_width(n):
    return n * T["GLYPH_W"] - 1 if n else 0


def truncate(s, max_chars):
    return s if len(s) <= max_chars else s[:max_chars - 1].rstrip() + ELLIPSIS


def cut_corner(tft, x, y, dx, dy, color):
    """Paints the 3-pixel L at one corner of a box; (x, y) is the corner
    pixel and dx/dy (+1/-1) point into the box."""
    tft.fill_rect(x, y, 1, 1, color)
    tft.fill_rect(x + dx, y, 1, 1, color)
    tft.fill_rect(x, y + dy, 1, 1, color)


def cut_corners(tft, x, y, w, h, outside):
    cut_corner(tft, x, y, 1, 1, outside)
    cut_corner(tft, x + w - 1, y, -1, 1, outside)
    cut_corner(tft, x, y + h - 1, 1, -1, outside)
    cut_corner(tft, x + w - 1, y + h - 1, -1, -1, outside)


def spine_color(index):
    return T["COL_SPINE_%d" % (index % T["SPINE_COUNT"])]


def progress_color(pct):
    if pct <= 0:
        return T["COL_PROG_NEW"]
    if pct >= 100:
        return T["COL_PROG_DONE"]
    return T["COL_PROG_MID"]


HEADER_RIGHT_W = 64


def list_header(tft, title, selected, count, full=True):
    if full:
        tft.text_box(0, 0, T["SCREEN_W"] - HEADER_RIGHT_W, T["LIB_HEADER_H"],
                     T["LIB_PAD_X"], T["LIB_HEADER_TEXT_Y"], title, T["COL_HEADING"], T["COL_BG"])
        tft.fill_rect(T["LIB_PAD_X"], T["LIB_UNDERLINE_Y"], text_width(len(title)),
                      T["LIB_UNDERLINE_H"], T["COL_ACCENT"])
        tft.fill_rect(0, T["LIB_HEADER_H"], T["SCREEN_W"], T["LIB_LIST_Y"] - T["LIB_HEADER_H"], T["COL_BG"])
    s = "%d/%d" % (selected + 1, count) if count else ""
    tft.text_box(T["SCREEN_W"] - HEADER_RIGHT_W, 0, HEADER_RIGHT_W, T["LIB_HEADER_H"],
                 HEADER_RIGHT_W - T["LIB_PAD_X"] - text_width(len(s)), T["LIB_HEADER_TEXT_Y"],
                 s, T["COL_MUTED"], T["COL_BG"])


def list_row(tft, slot, title, right, right_color, spine_index, selected):
    y = T["LIB_LIST_Y"] + slot * T["LIB_ROW_H"]
    row_h = T["LIB_ROW_H"]
    bg = T["COL_SEL_BG"] if selected else T["COL_BG"]
    fg = T["COL_SEL_TEXT"] if selected else T["COL_TEXT"]
    title_w = T["LIB_ROW_W"] - T["LIB_PCT_W"]

    tft.fill_rect(0, y, T["LIB_ROW_X"], row_h, T["COL_BG"])
    tft.text_box(T["LIB_ROW_X"], y, title_w, row_h, T["LIB_TITLE_X"] - T["LIB_ROW_X"], T["LIB_ROW_TEXT_Y"],
                 truncate(title, T["LIB_TITLE_CHARS"]), fg, bg)
    tft.text_box(T["LIB_ROW_X"] + title_w, y, T["LIB_PCT_W"], row_h,
                 T["LIB_PCT_W"] - T["LIB_PCT_PAD_R"] - text_width(len(right)), T["LIB_ROW_TEXT_Y"],
                 right, right_color, bg)
    tft.fill_rect(T["LIB_SPINE_X"], y + T["LIB_ROW_TEXT_Y"], T["LIB_SPINE_W"], T["GLYPH_H"] - 1,
                  spine_color(spine_index))
    if selected:
        cut_corners(tft, T["LIB_ROW_X"], y, T["LIB_ROW_W"], row_h, T["COL_BG"])


def list_empty_row(tft, slot):
    tft.fill_rect(0, T["LIB_LIST_Y"] + slot * T["LIB_ROW_H"], T["LIB_ROW_X"] + T["LIB_ROW_W"],
                  T["LIB_ROW_H"], T["COL_BG"])


def list_scrollbar(tft, window_start, count):
    top = T["LIB_LIST_Y"]
    h = T["LIB_ROWS"] * T["LIB_ROW_H"]
    left = T["LIB_ROW_X"] + T["LIB_ROW_W"]
    sx, sw = T["LIB_SCROLL_X"], T["LIB_SCROLL_W"]
    tft.fill_rect(left, top, sx - left, h, T["COL_BG"])
    tft.fill_rect(sx + sw, top, T["SCREEN_W"] - sx - sw, h, T["COL_BG"])
    if count <= T["LIB_ROWS"]:
        tft.fill_rect(sx, top, sw, h, T["COL_BG"])
        return
    thumb_h = max(T["LIB_THUMB_MIN_H"], h * T["LIB_ROWS"] // count)
    thumb_y = top + (h - thumb_h) * window_start // (count - T["LIB_ROWS"])
    tft.fill_rect(sx, top, sw, thumb_y - top, T["COL_TRACK"])
    tft.fill_rect(sx, thumb_y, sw, thumb_h, T["COL_THUMB"])
    tft.fill_rect(sx, thumb_y + thumb_h, sw, top + h - thumb_y - thumb_h, T["COL_TRACK"])


def list_footer(tft, hint):
    """Everything below the list rows: padding plus a centered hint."""
    W = T["SCREEN_W"]
    y = T["LIB_LIST_Y"] + T["LIB_ROWS"] * T["LIB_ROW_H"]
    tft.fill_rect(0, y, W, T["LIB_HINT_Y"] - y, T["COL_BG"])
    tft.text_box(0, T["LIB_HINT_Y"], W, T["SCREEN_H"] - T["LIB_HINT_Y"],
                 (W - text_width(len(hint))) // 2, 0, hint, T["COL_MUTED"], T["COL_BG"])


def list_empty(tft, line1, line2):
    """Replaces the rows and scrollbar of a list with a centered message."""
    W = T["SCREEN_W"]
    top = T["LIB_LIST_Y"]
    bottom = T["LIB_LIST_Y"] + T["LIB_ROWS"] * T["LIB_ROW_H"]
    y1 = T["LIB_EMPTY_Y"]
    y2 = y1 + T["GLYPH_H"] + 6
    tft.fill_rect(0, top, W, y1 - top, T["COL_BG"])
    tft.text_box(0, y1, W, y2 - y1, (W - text_width(len(line1))) // 2, 0, line1, T["COL_TEXT"], T["COL_BG"])
    tft.text_box(0, y2, W, bottom - y2, (W - text_width(len(line2))) // 2, 0, line2, T["COL_MUTED"], T["COL_BG"])


def list_screen(tft, title, rows, selected, hint, empty=None):
    """rows: list of (title, right, right_color, spine_index)."""
    n = len(rows)
    list_header(tft, title, selected, n)
    if not rows:
        list_empty(tft, *empty)
    else:
        per = T["LIB_ROWS"]
        start = max(0, min(selected - per + 1, n - per)) if selected >= per else 0
        for slot in range(per):
            idx = start + slot
            if idx < n:
                list_row(tft, slot, *rows[idx], selected=idx == selected)
            else:
                list_empty_row(tft, slot)
        list_scrollbar(tft, start, n)
    list_footer(tft, hint)


HINT_MENU = "Hold " + BUTTON + " for menu"
HINT_BACK = "Press " + BUTTON + " to go back"


def library_rows(entries):
    return [(e["title"], "%d%%" % e["pct"], progress_color(e["pct"]), i) for i, e in enumerate(entries)]


def bookmark_rows(bookmarks):
    """bookmarks: list of (book_index, title, page)."""
    return [(title, "p.%d" % page, T["COL_ACCENT"], book) for book, title, page in bookmarks]


# ---------------------------------------------------------------------------
# Home menu and settings
# ---------------------------------------------------------------------------

HOME_ITEMS = ((BOOK, "Library", "COL_ICON_LIBRARY"),
              (BOOKMARK, "Bookmarks", "COL_ICON_BOOKMARKS"),
              (GEAR, "Settings", "COL_ICON_SETTINGS"))


def home_item(tft, i, selected):
    icon, label, icon_col = HOME_ITEMS[i]
    x, w, h = T["HOME_BTN_X"], T["HOME_BTN_W"], T["HOME_BTN_H"]
    y = T["HOME_BTN_Y"] + i * T["HOME_BTN_STEP"]
    bg = T["COL_MENU_SEL_BG"] if selected else T["COL_MENU_BG"]
    fg = T["COL_MENU_SEL_TEXT"] if selected else T["COL_MENU_TEXT"]
    ty = (h - 7) // 2
    ix = T["HOME_BTN_ICON_X"]
    tft.text_box(x, y, T["HOME_BTN_TEXT_X"], h, ix, ty, icon, fg if selected else T[icon_col], bg)
    tft.text_box(x + T["HOME_BTN_TEXT_X"], y, w - T["HOME_BTN_TEXT_X"], h, 0, ty, label, fg, bg)
    cut_corners(tft, x, y, w, h, T["COL_BG"])


def home(tft, selected):
    W, H = T["SCREEN_W"], T["SCREEN_H"]
    word = "READERINO"
    cell = T["GLYPH_W"] * 2
    ww = len(word) * cell - 2
    x0 = (W - ww) // 2
    ty, th = T["HOME_TITLE_Y"], T["GLYPH_H"] * 2
    tft.fill_rect(0, 0, W, ty, T["COL_BG"])
    tft.fill_rect(0, ty, x0, th, T["COL_BG"])
    for i, ch in enumerate(word):
        tft.text_box(x0 + i * cell, ty, cell, th, 0, 0, ch, spine_color(i), T["COL_BG"], scale=2)
    tft.fill_rect(x0 + len(word) * cell, ty, W - x0 - len(word) * cell, th, T["COL_BG"])
    tag = "pocket e-reader"
    tft.text_box(0, ty + th, W, T["HOME_BTN_Y"] - ty - th, (W - text_width(len(tag))) // 2,
                 T["HOME_TAG_Y"] - ty - th, tag, T["COL_MUTED"], T["COL_BG"])
    x, w = T["HOME_BTN_X"], T["HOME_BTN_W"]
    for i in range(T["HOME_ITEMS"]):
        y = T["HOME_BTN_Y"] + i * T["HOME_BTN_STEP"]
        tft.fill_rect(0, y, x, T["HOME_BTN_H"], T["COL_BG"])
        tft.fill_rect(x + w, y, W - x - w, T["HOME_BTN_H"], T["COL_BG"])
        home_item(tft, i, i == selected)
        gap_y = y + T["HOME_BTN_H"]
        gap_h = (T["HOME_BTN_STEP"] - T["HOME_BTN_H"]) if i < T["HOME_ITEMS"] - 1 else H - gap_y
        tft.fill_rect(0, gap_y, W, gap_h, T["COL_BG"])


def settings(tft):
    list_header(tft, "Settings", 0, 0)
    list_empty(tft, "Nothing here yet", "Themes are coming")
    list_footer(tft, HINT_BACK)


def reader_top(tft):
    tft.fill_rect(0, 0, T["SCREEN_W"], T["RD_TOP"], T["COL_PAPER"])


def reader_line(tft, slot, text):
    tft.text_box(0, T["RD_TOP"] + slot * T["RD_LINE_H"], T["SCREEN_W"], T["RD_LINE_H"],
                 T["RD_PAD_X"], T["RD_LINE_TEXT_Y"], text[:T["RD_COLS"]], T["COL_INK"], T["COL_PAPER"])


def reader_status(tft, title, page, pages, bookmarked):
    W, pad = T["SCREEN_W"], T["RD_PAD_X"]
    y0 = T["RD_TOP"] + T["RD_LINES"] * T["RD_LINE_H"]
    tft.fill_rect(0, y0, W, T["RD_BAR_Y"] - y0, T["COL_PAPER"])

    bar_w = W - 2 * pad
    fill_w = bar_w * page // pages if pages else 0
    by, bh = T["RD_BAR_Y"], T["RD_BAR_H"]
    tft.fill_rect(0, by, pad, bh, T["COL_PAPER"])
    tft.fill_rect(pad, by, fill_w, bh, T["COL_BAR_FILL"])
    tft.fill_rect(pad + fill_w, by, bar_w - fill_w, bh, T["COL_BAR_TRACK"])
    tft.fill_rect(W - pad, by, pad, bh, T["COL_PAPER"])
    tft.fill_rect(0, by + bh, W, T["RD_STATUS_Y"] - by - bh, T["COL_PAPER"])

    sy, sh = T["RD_STATUS_Y"], T["SCREEN_H"] - T["RD_STATUS_Y"]
    left_w = 100
    tft.text_box(0, sy, left_w, sh, pad, 0, truncate(title, T["RD_STATUS_TITLE_CHARS"]),
                 T["COL_PAPER_MUTED"], T["COL_PAPER"])
    s = "%d/%d" % (page, pages)
    right_w = W - left_w
    tx = right_w - pad - text_width(len(s))
    tft.text_box(left_w, sy, right_w, sh, tx, 0, s, T["COL_INK"], T["COL_PAPER"])
    if bookmarked:
        tft.text_box(left_w + tx - 10, sy, 6, T["GLYPH_H"], 0, 0, BOOKMARK, T["COL_RIBBON"], T["COL_PAPER"])


def reader_page(tft, lines, first_line, title, bookmarked=False):
    per = T["RD_LINES"]
    reader_top(tft)
    for i in range(per):
        idx = first_line + i
        reader_line(tft, i, lines[idx] if idx < len(lines) else "")
    pages = max(1, (len(lines) + per - 1) // per)
    reader_status(tft, title, first_line // per + 1, pages, bookmarked)


def confirm_exit(tft):
    w, h, sh = T["DLG_W"], T["DLG_H"], T["DLG_SHADOW"]
    x = (T["SCREEN_W"] - w) // 2
    y = (T["SCREEN_H"] - h) // 2
    paper, card, border = T["COL_PAPER"], T["COL_CARD"], T["COL_CARD_BORDER"]

    # drop shadow: right and bottom strips, their outer corners rounded
    tft.fill_rect(x + w, y + sh, sh, h, T["COL_SHADOW"])
    tft.fill_rect(x + sh, y + h, w, sh, T["COL_SHADOW"])
    cut_corner(tft, x + w + sh - 1, y + sh, -1, 1, paper)
    cut_corner(tft, x + sh, y + h + sh - 1, 1, -1, paper)
    cut_corner(tft, x + w + sh - 1, y + h + sh - 1, -1, -1, paper)

    # card: 1px border, white body, rounded corners (bottom-right sits on the shadow)
    tft.fill_rect(x, y, w, h, border)
    tft.fill_rect(x + 1, y + 1, w - 2, h - 2, card)
    cut_corner(tft, x, y, 1, 1, paper)
    cut_corner(tft, x + w - 1, y, -1, 1, paper)
    cut_corner(tft, x, y + h - 1, 1, -1, paper)
    cut_corner(tft, x + w - 1, y + h - 1, -1, -1, T["COL_SHADOW"])
    for cx, cy in ((x + 1, y + 1), (x + w - 2, y + 1), (x + 1, y + h - 2), (x + w - 2, y + h - 2)):
        tft.fill_rect(cx, cy, 1, 1, border)

    title, sub = "Back to library?", "Your place is saved"
    tft.text_box(x + 1, y + T["DLG_TITLE_Y"], w - 2, T["GLYPH_H"],
                 (w - 2 - text_width(len(title))) // 2, 0, title, T["COL_INK"], card)
    tft.text_box(x + 1, y + T["DLG_SUB_Y"], w - 2, T["GLYPH_H"],
                 (w - 2 - text_width(len(sub))) // 2, 0, sub, T["COL_PAPER_MUTED"], card)

    bw, bh, bpad = T["DLG_BTN_W"], T["DLG_BTN_H"], T["DLG_BTN_PAD"]
    by = y + T["DLG_BTN_Y"]
    for bx, label, fg, bg in ((x + bpad, "No", T["COL_BTN_NO_TEXT"], T["COL_BTN_NO_BG"]),
                              (x + w - bpad - bw, "Yes", T["COL_BTN_YES_TEXT"], T["COL_BTN_YES_BG"])):
        tft.text_box(bx, by, bw, bh, (bw - text_width(len(label))) // 2, (bh - 7) // 2, label, fg, bg)
        cut_corners(tft, bx, by, bw, bh, card)


def toast(tft, msg, bg):
    w, h = T["TOAST_W"], T["TOAST_H"]
    x = (T["SCREEN_W"] - w) // 2
    y = T["TOAST_Y"]
    tft.text_box(x, y, w, h, (w - text_width(len(msg))) // 2, (h - 7) // 2, msg, T["COL_TOAST_TEXT"], bg)
    cut_corners(tft, x, y, w, h, T["COL_PAPER"])


def message(tft, line1, line2="", accent=None):
    W = T["SCREEN_W"]
    tft.fill_rect(0, 0, W, T["SCREEN_H"], T["COL_BG"])
    tft.text_box(0, T["MSG_LINE1_Y"], W, T["GLYPH_H"], (W - text_width(len(line1))) // 2, 0,
                 line1, T["COL_HEADING"], T["COL_BG"])
    tft.fill_rect((W - T["MSG_BAR_W"]) // 2, T["MSG_BAR_Y"], T["MSG_BAR_W"], 2,
                  accent if accent is not None else T["COL_ACCENT"])
    if line2:
        line2 = truncate(line2, T["RD_COLS"])
        tft.text_box(0, T["MSG_LINE2_Y"], W, T["GLYPH_H"], (W - text_width(len(line2))) // 2, 0,
                     line2, T["COL_MUTED"], T["COL_BG"])


# ---------------------------------------------------------------------------
# Sample data and scenes
# ---------------------------------------------------------------------------

DEMO_TITLES = ("Pride and Prejudice", "Moby-Dick; or, The Whale", "Frankenstein", "Dracula",
               "The Time Machine", "Alice's Adventures in Wonderland", "Walden", "The Odyssey",
               "Great Expectations", "The Picture of Dorian Gray", "Middlemarch", "Treasure Island")
DEMO_TEXT = (
    "It is a truth universally acknowledged, that a single man in possession of a good fortune, "
    "must be in want of a wife.\n\n"
    "However little known the feelings or views of such a man may be on his first entering a "
    "neighbourhood, this truth is so well fixed in the minds of the surrounding families, that he is "
    "considered the rightful property of some one or other of their daughters.\n\n"
    "\"My dear Mr. Bennet,\" said his lady to him one day, \"have you heard that Netherfield Park is "
    "let at last?\"\n\nMr. Bennet replied that he had not.\n\n"
    "\"But it is,\" returned she; \"for Mrs. Long has just been here, and she told me all about it.\"\n\n"
    "Mr. Bennet made no answer.\n\n"
    "\"Do you not want to know who has taken it?\" cried his wife impatiently.\n\n"
    "\"You want to tell me, and I have no objection to hearing it.\"\n\n"
    "This was invitation enough.\n\n") * 6


def sample_entries(demo):
    from formats import read_catalog
    cat = [] if demo else read_catalog(ROOT / "library" / "catalog.bin")
    titles = [r["title"] for r in cat] or list(DEMO_TITLES)
    demo_pct = {1: 34, 3: 100, 4: 72, 7: 8}
    return [{"title": t, "pct": demo_pct.get(i, 0)} for i, t in enumerate(titles)]


def sample_book(demo):
    """The first essay in essays/ if there is one (your own library), else a
    public-domain excerpt -- use --demo for anything you'll publish."""
    from textutil import wrap_text
    cands = [] if demo else sorted((ROOT / "essays").glob("*.txt"))
    if not cands:
        return DEMO_TITLES[0], wrap_text(DEMO_TEXT, T["RD_COLS"])
    title, _, body = cands[0].read_text(errors="ignore").partition("\n")
    return title.strip(), wrap_text(body, T["RD_COLS"])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--out", default=str(Path(__file__).parent / "out"))
    ap.add_argument("--demo", action="store_true",
                    help="public-domain sample titles and text instead of your library/ and essays/")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    entries = sample_entries(args.demo)
    title, lines = sample_book(args.demo)
    first = 0 if args.demo else T["RD_LINES"]

    scenes = {}

    def scene(name, fn):
        tft = Tft()
        fn(tft)
        scenes[name] = tft.img

    bms = [(1, entries[1]["title"], 4), (1, entries[1]["title"], 17), (4, entries[4]["title"], 2),
           (7, entries[7]["title"], 31), (9 % len(entries), entries[9 % len(entries)]["title"], 12)]

    scene("01_home", lambda t: home(t, 0))
    scene("02_home_bookmarks", lambda t: home(t, 1))
    scene("03_library", lambda t: list_screen(t, "Library", library_rows(entries), 1, HINT_MENU))
    scene("04_library_scrolled", lambda t: list_screen(t, "Library", library_rows(entries), 10, HINT_MENU))
    scene("05_bookmarks", lambda t: list_screen(t, "Bookmarks", bookmark_rows(bms), 2, HINT_MENU))
    scene("06_bookmarks_empty", lambda t: list_screen(t, "Bookmarks", [], 0, HINT_MENU,
                                                        ("No bookmarks yet", "Hold " + BUTTON + " on a page")))
    scene("07_settings", settings)
    scene("08_reader", lambda t: reader_page(t, lines, first, title))
    scene("09_reader_bookmarked", lambda t: reader_page(t, lines, first, title, bookmarked=True))
    scene("10_toast_added", lambda t: (reader_page(t, lines, first, title, bookmarked=True),
                                       toast(t, BOOKMARK + " Bookmarked", T["COL_TOAST_BG"])))
    scene("11_toast_removed", lambda t: (reader_page(t, lines, first, title),
                                         toast(t, "Removed", T["COL_TOAST_REMOVED_BG"])))
    scene("12_confirm", lambda t: (reader_page(t, lines, first, title), confirm_exit(t)))
    scene("13_opening", lambda t: message(t, "Opening" + ELLIPSIS, title))
    scene("14_sd_error", lambda t: message(t, "SD card error", "Press any button", T["COL_ERROR"]))

    s = args.scale
    for name, img in scenes.items():
        img.resize((img.width * s, img.height * s), Image.NEAREST).save(out / f"{name}.png")

    # contact sheet: every scene side by side, labelled
    cols = 2
    cell_w, cell_h = T["SCREEN_W"] * s, T["SCREEN_H"] * s
    gap, label_h = 24, 28
    rows = (len(scenes) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell_w + (cols + 1) * gap, rows * (cell_h + label_h + gap) + gap), (40, 40, 44))
    d = ImageDraw.Draw(sheet)
    for i, (name, img) in enumerate(scenes.items()):
        cx = gap + (i % cols) * (cell_w + gap)
        cy = gap + (i // cols) * (cell_h + label_h + gap)
        d.text((cx, cy), name, fill=(220, 220, 220))
        sheet.paste(img.resize((cell_w, cell_h), Image.NEAREST), (cx, cy + label_h))
    sheet.save(out / "all.png")

    undrawn = [n for n, img in scenes.items() if (255, 0, 255) in [c for _, c in img.getcolors(1 << 16)]]
    print(f"wrote {len(scenes)} scenes to {out}")
    if undrawn:
        print("WARNING: pixels never drawn (magenta) in:", ", ".join(undrawn))


if __name__ == "__main__":
    main()
