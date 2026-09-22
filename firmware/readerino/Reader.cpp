#include "Reader.h"
#include "Config.h"

namespace {
  const size_t READ_CHUNK = 256;
}

bool Reader::open(const String &path) {
  close();
  _path = path;
  if (!buildIndex()) return false;
  _file = SD.open(_path, FILE_READ);
  return (bool)_file;
}

void Reader::close() {
  if (_file) _file.close();
  _lineOffsets.clear();
  _path = "";
  _fileSize = 0;
}

int Reader::pageCount() const {
  int lines = totalLines();
  if (lines <= 0) return 1;
  return (lines + LINES_PER_PAGE - 1) / LINES_PER_PAGE;
}

bool Reader::buildIndex() {
  _lineOffsets.clear();
  File f = SD.open(_path, FILE_READ);
  if (!f) return false;
  _fileSize = f.size();

  _lineOffsets.push_back(0);
  uint32_t lineLen = 0; // chars committed to the current wrapped line so far
  uint32_t offset = 0;  // absolute offset of the next byte to be read

  uint32_t wordStart = 0;
  uint32_t wordLen = 0;
  bool inWord = false;

  // Places a word (hard-splitting it if it alone is longer than a full line)
  // at the start of a fresh wrapped line, and updates lineLen accordingly.
  auto placeWordAtLineStart = [&](uint32_t wordOffset, uint32_t wLen) {
    uint32_t remaining = wLen;
    uint32_t wOff = wordOffset;
    while (remaining > CHARS_PER_LINE) {
      wOff += CHARS_PER_LINE;
      _lineOffsets.push_back(wOff);
      remaining -= CHARS_PER_LINE;
    }
    lineLen = remaining;
  };

  auto commitWord = [&](uint32_t wordOffset, uint32_t wLen) {
    if (wLen == 0) return;
    if (lineLen == 0) {
      placeWordAtLineStart(wordOffset, wLen);
    } else if (lineLen + 1 + wLen <= CHARS_PER_LINE) {
      lineLen += 1 + wLen; // space + word fit on the current line
    } else {
      _lineOffsets.push_back(wordOffset); // word starts a new line
      placeWordAtLineStart(wordOffset, wLen);
    }
  };

  uint8_t buf[READ_CHUNK];
  int n;
  while ((n = f.read(buf, READ_CHUNK)) > 0) {
    for (int i = 0; i < n; i++) {
      char c = (char)buf[i];
      uint32_t charOffset = offset;
      offset++;

      if (c == '\r') continue; // transparently handle CRLF

      if (c == '\n') {
        commitWord(wordStart, wordLen);
        wordLen = 0;
        inWord = false;
        _lineOffsets.push_back(offset); // next wrapped line starts right after the newline
        lineLen = 0;
        continue;
      }
      if (c == ' ' || c == '\t') {
        commitWord(wordStart, wordLen);
        wordLen = 0;
        inWord = false;
        continue;
      }
      if (!inWord) {
        wordStart = charOffset;
        inWord = true;
      }
      wordLen++;
    }
  }
  if (inWord) commitWord(wordStart, wordLen);

  if (_lineOffsets.back() != _fileSize) {
    _lineOffsets.push_back(_fileSize);
  }
  // drop a dangling empty trailing line when the file ends exactly on a line boundary
  while (_lineOffsets.size() >= 2 &&
         _lineOffsets[_lineOffsets.size() - 2] == _lineOffsets.back()) {
    _lineOffsets.pop_back();
  }

  f.close();
  return true;
}

String Reader::readRange(uint32_t startOffset, uint32_t endOffset) {
  if (!_file || endOffset <= startOffset) return String("");
  uint32_t len = endOffset - startOffset;
  if (len > 200) len = 200; // safety cap; a wrapped line should never be this long

  static uint8_t buf[200];
  _file.seek(startOffset);
  int n = _file.read(buf, len); // one bulk read instead of `len` single-byte reads
  if (n < 0) n = 0;

  String s;
  s.reserve(n);
  for (int i = 0; i < n; i++) {
    char c = (char)buf[i];
    if (c == '\r' || c == '\n') continue;
    s += c;
  }
  while (s.length() > 0 && s.charAt(s.length() - 1) == ' ') {
    s.remove(s.length() - 1);
  }
  return s;
}

void Reader::renderPage(int startLine, std::vector<String> &out) {
  out.clear();
  int total = totalLines();
  for (int row = 0; row < LINES_PER_PAGE; row++) {
    int idx = startLine + row;
    if (idx < 0 || idx >= total) break;
    out.push_back(readRange(_lineOffsets[idx], _lineOffsets[idx + 1]));
  }
}
