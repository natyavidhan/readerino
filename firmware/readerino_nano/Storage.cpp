#include "Storage.h"
#include <SD.h>
#include <string.h>

namespace {
  const char *CATALOG_PATH = "/catalog.bin";
  const uint8_t CAT_HEADER_SIZE = 8;
  const uint8_t RECORD_SIZE = 156;

  const uint8_t FILENAME_OFF = 0;
  const uint8_t TITLE_OFF = 32;
  const uint8_t TOTALLINES_OFF = 112;
  const uint8_t POSITION_OFF = 116;
  const uint8_t BMCOUNT_OFF = 120;
  const uint8_t KIND_OFF = 121;
  const uint8_t FPS_OFF = 122;
  const uint8_t BOOKMARKS_OFF = 124;

  // Kept open for the session: avoids repeated FAT directory lookups.
  // Opened with O_RDWR, NOT the FILE_WRITE macro — FILE_WRITE includes
  // O_APPEND on this library, which silently forces every write() to
  // seek to end-of-file first, discarding any seek() to a specific
  // record offset. Confirmed by reading SdFile::write() before relying
  // on it: `if ((flags_ & O_APPEND) && curPosition_ != fileSize_) seekEnd();`
  File catalogFile;
  int cachedCount = 0;

  uint32_t readU32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  }

  void writeU32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
  }

  uint32_t recordOffset(int index) {
    return (uint32_t)CAT_HEADER_SIZE + (uint32_t)index * RECORD_SIZE;
  }
}

bool Storage::begin() {
  if (!SD.begin(PIN_SD_CS)) return false;
  return rescan();
}

bool Storage::rescan() {
  if (catalogFile) catalogFile.close();
  cachedCount = 0;

  if (!SD.exists(CATALOG_PATH)) return false;
  catalogFile = SD.open(CATALOG_PATH, O_RDWR);
  if (!catalogFile) return false;

  uint8_t header[CAT_HEADER_SIZE];
  catalogFile.seek(0);
  int n = catalogFile.read(header, CAT_HEADER_SIZE);
  if (n != CAT_HEADER_SIZE || memcmp(header, "RCT1", 4) != 0) {
    catalogFile.close();
    return false;
  }
  cachedCount = header[6] | (header[7] << 8); // recordCount, uint16 LE
  return true;
}

int Storage::bookCount() { return cachedCount; }

namespace {
  bool readAt(uint32_t pos, void *dst, uint8_t n) {
    return catalogFile.seek(pos) && catalogFile.read((uint8_t *)dst, n) == n;
  }
}

// Reads each field straight into `out` rather than the whole 156-byte
// record into a stack buffer: this runs deep inside list drawing and
// opening, where the stack is the scarcest thing on the chip. (Small reads
// are cheap -- the record's block is already in the SD library's cache.)
bool Storage::getEntry(int index, CatalogEntry &out) {
  if (index < 0 || index >= cachedCount || !catalogFile) return false;
  const uint32_t base = recordOffset(index);
  uint8_t num[12]; // totalLines, position, bookmarkCount, kind, fps, pad
  if (!readAt(base + FILENAME_OFF, out.filename, sizeof(out.filename) - 1) ||
      !readAt(base + TITLE_OFF, out.title, sizeof(out.title) - 1) ||
      !readAt(base + TOTALLINES_OFF, num, sizeof(num)) ||
      !readAt(base + BOOKMARKS_OFF, out.bookmarks, sizeof(out.bookmarks))) { // little-endian, like the AVR
    return false;
  }
  out.filename[sizeof(out.filename) - 1] = 0; // disk fields are null-padded
  out.title[sizeof(out.title) - 1] = 0;
  out.totalLines = readU32(num);
  out.position = readU32(num + 4);
  out.bookmarkCount = num[8] > MAX_BOOKMARKS ? MAX_BOOKMARKS : num[8];
  out.kind = num[9];
  out.fps = num[10];
  return true;
}

bool Storage::getTitle(int index, char *out) {
  out[0] = 0;
  if (index < 0 || index >= cachedCount || !catalogFile) return false;
  catalogFile.seek(recordOffset(index) + TITLE_OFF);
  if (catalogFile.read((uint8_t *)out, DISK_TITLE_LEN) != DISK_TITLE_LEN) {
    out[0] = 0;
    return false;
  }
  out[DISK_TITLE_LEN] = 0; // packer null-pads, but a full-width title has no terminator
  return true;
}

void Storage::setPosition(int index, uint32_t line) {
  if (index < 0 || index >= cachedCount || !catalogFile) return;
  catalogFile.seek(recordOffset(index) + POSITION_OFF);
  uint8_t buf[4];
  writeU32(buf, line);
  catalogFile.write(buf, 4);
  catalogFile.flush();
}

namespace {
  // Writes an entry's bookmark count + list back to its record.
  void writeBookmarks(int index, const CatalogEntry &e) {
    catalogFile.seek(recordOffset(index) + BMCOUNT_OFF);
    catalogFile.write(&e.bookmarkCount, 1); // kind/fps bytes after it are left alone
    catalogFile.seek(recordOffset(index) + BOOKMARKS_OFF);
    uint8_t buf[4];
    for (uint8_t i = 0; i < MAX_BOOKMARKS; i++) {
      writeU32(buf, e.bookmarks[i]);
      catalogFile.write(buf, 4);
    }
    catalogFile.flush();
  }
}

void Storage::addBookmark(int index, uint32_t line) {
  CatalogEntry e;
  if (!getEntry(index, e)) return;

  for (uint8_t i = 0; i < e.bookmarkCount; i++) {
    if (e.bookmarks[i] == line) return; // already bookmarked
  }
  if (e.bookmarkCount < MAX_BOOKMARKS) {
    e.bookmarks[e.bookmarkCount] = line;
    e.bookmarkCount++;
  } else {
    for (uint8_t i = 0; i < MAX_BOOKMARKS - 1; i++) e.bookmarks[i] = e.bookmarks[i + 1];
    e.bookmarks[MAX_BOOKMARKS - 1] = line;
  }
  writeBookmarks(index, e);
}

void Storage::removeBookmark(int index, uint32_t line) {
  CatalogEntry e;
  if (!getEntry(index, e)) return;

  uint8_t kept = 0;
  for (uint8_t i = 0; i < e.bookmarkCount; i++) {
    if (e.bookmarks[i] != line) e.bookmarks[kept++] = e.bookmarks[i];
  }
  if (kept == e.bookmarkCount) return; // wasn't bookmarked
  for (uint8_t i = kept; i < MAX_BOOKMARKS; i++) e.bookmarks[i] = 0;
  e.bookmarkCount = kept;
  writeBookmarks(index, e);
}
