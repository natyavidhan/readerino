#pragma once
#include <Arduino.h>
#include <vector>
#include <SD.h>

// Reads a single .rbk file (see packer/formats.py for the byte layout).
// Unlike the old plain-text Reader, there's no on-device indexing pass:
// the word-wrap line table was precomputed by the host packer, so opening
// a book is just parsing a small header, and turning a page is a couple of
// small seeks — no per-open scan of the whole file.
class Book {
public:
  bool open(const String &path);
  void close();

  uint32_t totalLines() const { return _lineCount; }
  int pageCount() const;

  // Fills 'out' with up to LINES_PER_PAGE wrapped lines starting at 'startLine'.
  void renderPage(uint32_t startLine, std::vector<String> &out);

  const String &path() const { return _path; }

private:
  String _path;
  File _file;
  uint32_t _lineCount = 0;
  uint32_t _offsetTablePos = 0; // file offset where the line-offset table starts
  uint32_t _blobPos = 0;        // file offset where the text blob starts

  uint32_t readOffsetEntry(uint32_t i);
  String line(uint32_t index);
};
