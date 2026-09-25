#include "Media.h"
#include "Config.h"
#include "Theme.h"
#include "Tft.h"
#include <SD.h>

namespace {
  const uint8_t RVD_HEADER_SIZE = 16;
  const uint8_t RIM_HEADER_SIZE = 8;
  const uint8_t END_OF_FRAME = 0xFF;

  File file;
  uint32_t frames = 0, next = 0;
  uint16_t keyInt = 1, keys = 0;
  uint8_t rate = 30, vidW = SCREEN_W, vidH = VIDEO_H, originX = 0;

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

  // Decodes one frame (see packer/media.py encode_frame) onto the screen.
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
      Tft::setWindow(originX + x, y, n, 1);
      while (n) {
        uint8_t r = get();
        uint8_t len = (r & 0x7F) + 1;
        if (len > n) len = n;
        Tft::pushColor(r & 0x80 ? COL_VIDEO_WHITE : COL_VIDEO_BLACK, len);
        n -= len;
      }
    }
    Tft::deselect();
    return ok && !atEof;
  }
}

bool Media::openVideo(const char *path, uint8_t *buffer, uint8_t bufferSize) {
  close();
  ring = buffer;
  ringCap = bufferSize;
  file = SD.open(path, O_READ);
  if (!file) return false;
  ringReset();
  // header: "RVD1", version, fps, width, height, frameCount u32, keyInterval u16, keyCount u16
  if (get() != 'R' || get() != 'V' || get() != 'D' || get() != '1') {
    close();
    return false;
  }
  get(); // version
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
    Tft::fillRect(0, 0, SCREEN_W, y, COL_VIDEO_BLACK);
    Tft::fillRect(0, y + ht, SCREEN_W, SCREEN_H - y - ht, COL_VIDEO_BLACK);
    Tft::fillRect(0, y, x, ht, COL_VIDEO_BLACK);
    Tft::fillRect(x + w, y, SCREEN_W - x - w, ht, COL_VIDEO_BLACK);
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
