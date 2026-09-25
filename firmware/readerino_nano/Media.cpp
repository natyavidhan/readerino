#include "Media.h"
#include "Config.h"
#include "Theme.h"
#include "Tft.h"
#include <SD.h>

namespace {
  const uint8_t RVD_HEADER_SIZE = 16;
  const uint8_t RIM_HEADER_SIZE = 8;
  const uint8_t END_OF_FRAME = 0xFF;
  const uint8_t RVD_PALETTE = 2;        // header version: 64-colour palette video
  const uint8_t PALETTE_BYTES = 64 * 2; // RGB565, big-endian, after the key table

  File file;
  uint32_t frames = 0, next = 0;
  uint16_t keyInt = 1, keys = 0;
  uint8_t rate = 30, vidW = SCREEN_W, vidH = VIDEO_H, originX = 0, originY = 0;
  uint8_t mode;           // header version: 1 = black/white, RVD_PALETTE = colour
  const uint8_t *palette; // colour mode: the first PALETTE_BYTES of the lent buffer

  // Read-ahead ring buffer over the file.
  uint8_t *ring;
  uint8_t ringCap, ringHead, ringCount;
  bool atEof;

  void ringReset() {
    ringHead = ringCount = 0;
    atEof = false;
  }

  // Reads from the card into the ring's free space: one read if `once`,
  // otherwise until it's full.
  void ringFill(bool once) {
    while (ringCount < ringCap && !atEof) {
      if (ringCount == 0) ringHead = 0; // keep the free space contiguous
      uint16_t tail = ringHead + ringCount; // can exceed 255 before wrapping
      if (tail >= ringCap) tail -= ringCap;
      uint8_t space = tail >= ringHead ? ringCap - tail : ringHead - tail;
      int n = file.read(ring + tail, space);
      if (n <= 0) atEof = true;
      else ringCount += n;
      if (once) break;
    }
    Tft::restoreSpi(); // the SD library reprograms the SPI registers
  }

  // Next byte of the stream; END_OF_FRAME past the end of the file, so a
  // truncated file just ends the frame instead of hanging.
  uint8_t get() {
    if (!ringCount) {
      ringFill(true);
      if (!ringCount) return END_OF_FRAME;
    }
    uint8_t b = ring[ringHead];
    if (++ringHead == ringCap) ringHead = 0;
    ringCount--;
    return b;
  }

  bool seekTo(uint32_t pos) {
    ringReset();
    return file.seek(pos);
  }

  // Little-endian reads. Separate statements: in `get() | get() << 8` the
  // two calls may run in either order.
  uint16_t readU16() {
    uint16_t v = get();
    return v | (uint16_t)get() << 8;
  }

  uint32_t readU32() {
    uint32_t v = readU16();
    return v | (uint32_t)readU16() << 16;
  }

  uint16_t paletteColor(uint8_t i) {
    const uint8_t *p = palette + ((i & 63) << 1);
    return (uint16_t)p[0] << 8 | p[1];
  }

  // Paints black around a w x h picture centred at (x, y) within the top
  // areaH rows of the screen.
  void blackBorder(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t areaH) {
    Tft::fillRect(0, 0, SCREEN_W, y, COL_VIDEO_BLACK);
    Tft::fillRect(0, y + h, SCREEN_W, areaH - y - h, COL_VIDEO_BLACK);
    Tft::fillRect(0, y, x, h, COL_VIDEO_BLACK);
    Tft::fillRect(x + w, y, SCREEN_W - x - w, h, COL_VIDEO_BLACK);
  }

  // Decodes one frame onto the screen (packer/media.py: encode_frame for
  // black/white, encode_color_frame for palette colour).
  bool drawFrame() {
    Tft::select();
    bool ok = true;
    for (;;) {
      uint8_t y = get();
      if (y == END_OF_FRAME) break;
      uint8_t x = get(), n = get();
      if (y >= vidH || !n || x + n > vidW) { // corrupt stream
        ok = false;
        break;
      }
      Tft::setWindow(originX + x, originY + y, n, 1);
      while (n) {
        uint8_t r = get();
        uint8_t len = (r & 0x7F) + 1;
        if (len > n) len = n;
        if (mode != RVD_PALETTE) {
          Tft::pushColor(r & 0x80 ? COL_VIDEO_WHITE : COL_VIDEO_BLACK, len);
        } else if (r & 0x80) { // literal palette indices
          for (uint8_t i = 0; i < len; i++) Tft::pushColor(paletteColor(get()), 1);
        } else {               // run of one index
          Tft::pushColor(paletteColor(get()), len);
        }
        n -= len;
      }
    }
    Tft::deselect();
    return ok && !atEof;
  }
}

bool Media::openVideo(const char *path, uint8_t *buffer, uint8_t bufferSize) {
  close();
  // Colour videos keep their palette at the front of the buffer; the rest
  // is the read-ahead ring.
  palette = buffer;
  ring = buffer + PALETTE_BYTES;
  ringCap = bufferSize - PALETTE_BYTES;
  file = SD.open(path, O_READ);
  if (!file) return false;
  ringReset();
  // header: "RVD1", version, fps, width, height, frameCount u32, keyInterval u16, keyCount u16
  if (get() != 'R' || get() != 'V' || get() != 'D' || get() != '1') {
    close();
    return false;
  }
  mode = get(); // version
  rate = get();
  vidW = get();
  vidH = get();
  frames = readU32();
  keyInt = readU16();
  keys = readU16();
  if (!rate || !keyInt || !keys || vidW > SCREEN_W || vidH > VIDEO_H) {
    close();
    return false;
  }
  originX = (SCREEN_W - vidW) / 2;
  originY = (VIDEO_H - vidH) / 2;
  if (mode == RVD_PALETTE) {
    seekTo(RVD_HEADER_SIZE + (uint32_t)keys * 8);
    for (uint8_t i = 0; i < PALETTE_BYTES; i++) buffer[i] = get();
  }
  next = 0;
  return true;
}

void Media::close() {
  if (file) file.close();
  frames = next = 0;
}

uint32_t Media::frameCount() { return frames; }
uint8_t Media::fps() { return rate; }
uint16_t Media::keyInterval() { return keyInt; }
uint16_t Media::keyCount() { return keys; }
uint32_t Media::nextFrame() { return next; }

bool Media::seekKey(uint16_t k) {
  if (k >= keys) k = keys - 1;
  // key table entry k: snapshotOffset u32, nextOffset u32
  if (!seekTo(RVD_HEADER_SIZE + (uint32_t)k * 8)) return false;
  uint32_t snapshot = readU32();
  uint32_t resume = readU32();
  if (!seekTo(snapshot)) return false;
  blackBorder(originX, originY, vidW, vidH, VIDEO_H); // letterbox bars
  bool ok = drawFrame();
  next = (uint32_t)k * keyInt + 1;
  return seekTo(resume) && ok;
}

bool Media::drawNextFrame() {
  if (next >= frames) return false;
  if (!drawFrame()) {
    next = frames;
    return false;
  }
  next++;
  return true;
}

void Media::prefetch() {
  if (file && ringCount < ringCap) ringFill(false);
}

bool Media::drawImage(const char *path, uint8_t *buffer, uint8_t bufferSize) {
  close();
  file = SD.open(path, O_READ);
  if (!file) return false;
  uint8_t h[RIM_HEADER_SIZE];
  bool ok = file.read(h, RIM_HEADER_SIZE) == RIM_HEADER_SIZE && h[0] == 'R' && h[1] == 'I' &&
            h[2] == 'M' && h[3] == '1' && h[4] && h[5] && h[4] <= SCREEN_W && h[5] <= SCREEN_H;
  if (ok) {
    const uint8_t w = h[4], ht = h[5];
    const uint8_t x = (SCREEN_W - w) / 2, y = (SCREEN_H - ht) / 2;
    blackBorder(x, y, w, ht, SCREEN_H);
    Tft::select();
    Tft::setWindow(x, y, w, ht);
    uint16_t left = (uint16_t)w * ht * 2;
    const uint8_t chunk = bufferSize & ~1;
    while (left) {
      int n = file.read(buffer, left < chunk ? left : chunk);
      Tft::restoreSpi();
      if (n <= 0) {
        ok = false;
        break;
      }
      Tft::pushBytes(buffer, n);
      left -= n;
    }
    Tft::deselect();
  }
  close();
  return ok;
}
