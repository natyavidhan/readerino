#include "Book.h"
#include "Config.h"
#include <string.h>

namespace {
  const uint8_t HEADER_SIZE = 24;

  uint32_t readU32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  }
}

bool Book::open(const char *path) {
  close();
  strncpy(_path, path, sizeof(_path) - 1);
  _path[sizeof(_path) - 1] = 0;

  _file = SD.open(path, O_READ);
  if (!_file) return false;

  uint8_t header[HEADER_SIZE];
  if (_file.read(header, HEADER_SIZE) != HEADER_SIZE || memcmp(header, "RBK1", 4) != 0) {
    close();
    return false;
  }

  uint32_t titleLen = readU32(header + 8);
  uint32_t authorLen = readU32(header + 12);
  _lineCount = readU32(header + 16);

  _offsetTablePos = (uint32_t)HEADER_SIZE + titleLen + authorLen;
  _blobPos = _offsetTablePos + (uint32_t)(_lineCount + 1) * 4;
  return true;
}

void Book::close() {
  if (_file) _file.close();
  _lineCount = 0;
  _offsetTablePos = 0;
  _blobPos = 0;
}

int Book::pageCount() const {
  if (_lineCount == 0) return 1;
  return (_lineCount + LINES_PER_PAGE - 1) / LINES_PER_PAGE;
}

uint32_t Book::readOffsetEntry(uint32_t i) {
  if (!_file) return 0;
  _file.seek(_offsetTablePos + i * 4);
  uint8_t buf[4];
  if (_file.read(buf, 4) != 4) return 0;
  return readU32(buf);
}

void Book::getLine(uint32_t index, char *buf, uint8_t maxLen) {
  buf[0] = 0;
  if (index >= _lineCount) return;
  uint32_t start = readOffsetEntry(index);
  uint32_t end = readOffsetEntry(index + 1);
  if (end <= start) return;
  uint32_t len = end - start;
  if (len > maxLen) len = maxLen;

  _file.seek(_blobPos + start);
  int n = _file.read((uint8_t *)buf, len);
  if (n < 0) n = 0;
  buf[n] = 0;
}
