#include "Book.h"
#include "Config.h"
#include <string.h>

namespace {
  const int HEADER_SIZE = 24;

  uint32_t readU32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  }
}

bool Book::open(const String &path) {
  close();
  _path = path;
  _file = SD.open(path, FILE_READ);
  if (!_file) return false;

  uint8_t header[HEADER_SIZE];
  if (_file.read(header, HEADER_SIZE) != HEADER_SIZE || memcmp(header, "RBK1", 4) != 0) {
    close();
    return false;
  }

  uint32_t titleLen = readU32(header + 8);
  uint32_t authorLen = readU32(header + 12);
  _lineCount = readU32(header + 16);

  _offsetTablePos = HEADER_SIZE + titleLen + authorLen;
  _blobPos = _offsetTablePos + (uint32_t)(_lineCount + 1) * 4;
  return true;
}

void Book::close() {
  if (_file) _file.close();
  _path = "";
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

String Book::line(uint32_t index) {
  if (index >= _lineCount) return String("");
  uint32_t start = readOffsetEntry(index);
  uint32_t end = readOffsetEntry(index + 1);
  if (end <= start) return String("");
  uint32_t len = end - start;
  if (len > 200) len = 200; // safety cap; a wrapped line should never be this long

  _file.seek(_blobPos + start);
  static uint8_t buf[200];
  int n = _file.read(buf, len);
  if (n < 0) n = 0;

  String s;
  s.reserve(n);
  for (int i = 0; i < n; i++) s += (char)buf[i];
  return s;
}

void Book::renderPage(uint32_t startLine, std::vector<String> &out) {
  out.clear();
  for (int row = 0; row < LINES_PER_PAGE; row++) {
    uint32_t idx = startLine + row;
    if (idx >= _lineCount) break;
    out.push_back(line(idx));
  }
}
