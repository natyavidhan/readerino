#pragma once
#include <Arduino.h>
#include <SD.h>
#include "Config.h"

// Reads a single .rbk file (see packer/formats.py). No String, no caching:
// getLine() writes into a caller-owned buffer so callers control exactly
// how much stack/static RAM is spent per line.
class Book {
public:
  bool open(const char *path);
  void close();

  uint32_t totalLines() const { return _lineCount; }
  int pageCount() const;

  // Fills buf (size maxLen+1) with the wrapped line's text, null-terminated.
  void getLine(uint32_t index, char *buf, uint8_t maxLen);

  const char *path() const { return _path; }

private:
  char _path[FILENAME_LEN];
  File _file;
  uint32_t _lineCount = 0;
  uint32_t _offsetTablePos = 0;
  uint32_t _blobPos = 0;

  uint32_t readOffsetEntry(uint32_t i);
};
