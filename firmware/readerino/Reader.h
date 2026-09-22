#pragma once
#include <Arduino.h>
#include <vector>
#include <SD.h>

// Streams a .txt file off the SD card and word-wraps it into fixed-width
// lines without holding the whole file in RAM: a single pass over the file
// builds an index of byte offsets where each wrapped line begins, then pages
// are rendered on demand by seeking to those offsets.
class Reader {
public:
  bool open(const String &path);
  void close();

  int totalLines() const { return _lineOffsets.empty() ? 0 : (int)_lineOffsets.size() - 1; }
  int pageCount() const;

  // Fills 'out' with up to LINES_PER_PAGE rendered strings starting at wrapped-line index 'startLine'.
  void renderPage(int startLine, std::vector<String> &out);

  const String &path() const { return _path; }

private:
  String _path;
  File _file;
  std::vector<uint32_t> _lineOffsets; // N+1 entries: start offset of each wrapped line, plus EOF
  uint32_t _fileSize = 0;

  bool buildIndex();
  String readRange(uint32_t startOffset, uint32_t endOffset);
};
