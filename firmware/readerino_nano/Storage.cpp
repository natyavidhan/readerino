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

  void readFixedCStr(const uint8_t *p, uint8_t len, char *out, uint8_t outSize) {
    uint8_t n = len < outSize - 1 ? len : outSize - 1;
    memcpy(out, p, n);
    out[n] = 0; // packer null-pads every field, so this is always within bounds
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

bool Storage::getEntry(int index, CatalogEntry &out) {
  if (index < 0 || index >= cachedCount || !catalogFile) return false;
  catalogFile.seek(recordOffset(index));
  uint8_t rec[RECORD_SIZE];
  int n = catalogFile.read(rec, RECORD_SIZE);
  if (n != RECORD_SIZE) return false;

  readFixedCStr(rec + FILENAME_OFF, DISK_FILENAME_LEN, out.filename, sizeof(out.filename));
  readFixedCStr(rec + TITLE_OFF, DISK_TITLE_LEN, out.title, sizeof(out.title));
  out.totalLines = readU32(rec + TOTALLINES_OFF);
  out.position = readU32(rec + POSITION_OFF);
  out.bookmarkCount = rec[BMCOUNT_OFF];
  if (out.bookmarkCount > MAX_BOOKMARKS) out.bookmarkCount = MAX_BOOKMARKS;
  for (uint8_t i = 0; i < MAX_BOOKMARKS; i++) {
    out.bookmarks[i] = readU32(rec + BOOKMARKS_OFF + i * 4);
  }
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

void Storage::addBookmark(int index, uint32_t line) {
  if (index < 0 || index >= cachedCount || !catalogFile) return;
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

  catalogFile.seek(recordOffset(index) + BMCOUNT_OFF);
  catalogFile.write(&e.bookmarkCount, 1);
  uint8_t pad[3] = {0, 0, 0};
  catalogFile.write(pad, 3);
  uint8_t buf[4];
  for (uint8_t i = 0; i < MAX_BOOKMARKS; i++) {
    writeU32(buf, e.bookmarks[i]);
    catalogFile.write(buf, 4);
  }
  catalogFile.flush();
}
