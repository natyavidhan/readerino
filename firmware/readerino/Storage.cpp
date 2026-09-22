#include "Storage.h"
#include "Config.h"
#include <SD.h>
#include <string.h>

namespace {
  const char *CATALOG_PATH = "/catalog.bin";
  const int CAT_HEADER_SIZE = 8;
  const int RECORD_SIZE = 156;

  const int FILENAME_OFF = 0, FILENAME_LEN = 32;
  const int TITLE_OFF = 32, TITLE_LEN = 48;
  const int AUTHOR_OFF = 80, AUTHOR_LEN = 32;
  const int TOTALLINES_OFF = 112;
  const int POSITION_OFF = 116;
  const int BMCOUNT_OFF = 120;
  const int BOOKMARKS_OFF = 124;

  // Kept open for the whole session instead of re-opening per call: opening
  // a file (FAT directory lookup) is far more expensive than a seek on an
  // already-open handle, and the library screen can call getEntry() several
  // times per redraw.
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

  String readFixedString(const uint8_t *p, int len) {
    char buf[49]; // max(FILENAME_LEN, TITLE_LEN, AUTHOR_LEN) + 1
    int n = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
    memcpy(buf, p, n);
    buf[n] = 0; // packer null-pads every field, so this is always within bounds
    return String(buf);
  }

  uint32_t recordOffset(int index) {
    return CAT_HEADER_SIZE + (uint32_t)index * RECORD_SIZE;
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
  // "r+": read/write, no truncation. Held open for both reads (getEntry)
  // and in-place writes (setPosition/addBookmark) for the rest of the
  // session; FILE_WRITE ("w") would truncate the whole catalog on open.
  catalogFile = SD.open(CATALOG_PATH, "r+");
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

  out.filename = readFixedString(rec + FILENAME_OFF, FILENAME_LEN);
  out.title = readFixedString(rec + TITLE_OFF, TITLE_LEN);
  out.author = readFixedString(rec + AUTHOR_OFF, AUTHOR_LEN);
  out.totalLines = readU32(rec + TOTALLINES_OFF);
  out.position = readU32(rec + POSITION_OFF);
  out.bookmarkCount = rec[BMCOUNT_OFF];
  if (out.bookmarkCount > MAX_BOOKMARKS) out.bookmarkCount = MAX_BOOKMARKS;
  for (int i = 0; i < MAX_BOOKMARKS; i++) {
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
  CatalogEntry entry;
  if (!getEntry(index, entry)) return;

  for (uint8_t i = 0; i < entry.bookmarkCount; i++) {
    if (entry.bookmarks[i] == line) return; // already bookmarked
  }

  if (entry.bookmarkCount < MAX_BOOKMARKS) {
    entry.bookmarks[entry.bookmarkCount] = line;
    entry.bookmarkCount++;
  } else {
    // full: drop the oldest bookmark to make room
    for (int i = 0; i < MAX_BOOKMARKS - 1; i++) {
      entry.bookmarks[i] = entry.bookmarks[i + 1];
    }
    entry.bookmarks[MAX_BOOKMARKS - 1] = line;
  }

  catalogFile.seek(recordOffset(index) + BMCOUNT_OFF);
  catalogFile.write(&entry.bookmarkCount, 1);
  uint8_t pad[3] = {0, 0, 0};
  catalogFile.write(pad, 3);
  uint8_t buf[4];
  for (int i = 0; i < MAX_BOOKMARKS; i++) {
    writeU32(buf, entry.bookmarks[i]);
    catalogFile.write(buf, 4);
  }
  catalogFile.flush();
}
