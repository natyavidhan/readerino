#include "Storage.h"
#include "Config.h"
#include <SD.h>
#include <string.h>
#include <vector>

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

  // Individual SD reads on this hardware cost ~10ms each regardless of
  // whether the file is already open (measured: 212 reads = 2140ms, 6 reads
  // = 64ms — a flat per-read cost, not a per-open one). Any screen that
  // needs to look at more than a handful of records — the bookmarks screen
  // scans every book — is unusably slow if it hits the SD card per record.
  // So the catalog is parsed into RAM once (~156 bytes/book, ~33KB for a
  // 212-book library) and reads are served from there; only the two writers
  // (setPosition/addBookmark) still touch the SD card, to persist changes.
  struct CachedRecord {
    char filename[FILENAME_LEN];
    char title[TITLE_LEN];
    char author[AUTHOR_LEN];
    uint32_t totalLines;
    uint32_t position;
    uint8_t bookmarkCount;
    uint32_t bookmarks[MAX_BOOKMARKS];
  };

  std::vector<CachedRecord> cache;
  File catalogFile; // kept open for the session so writes can seek in place

  uint32_t readU32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  }

  void writeU32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
  }

  void readFixedCStr(const uint8_t *p, int len, char *out, int outSize) {
    int n = len < outSize - 1 ? len : outSize - 1;
    memcpy(out, p, n);
    out[n] = 0; // packer null-pads every field, so this is always within bounds
  }

  void parseRecord(const uint8_t *rec, CachedRecord &c) {
    readFixedCStr(rec + FILENAME_OFF, FILENAME_LEN, c.filename, sizeof(c.filename));
    readFixedCStr(rec + TITLE_OFF, TITLE_LEN, c.title, sizeof(c.title));
    readFixedCStr(rec + AUTHOR_OFF, AUTHOR_LEN, c.author, sizeof(c.author));
    c.totalLines = readU32(rec + TOTALLINES_OFF);
    c.position = readU32(rec + POSITION_OFF);
    c.bookmarkCount = rec[BMCOUNT_OFF];
    if (c.bookmarkCount > MAX_BOOKMARKS) c.bookmarkCount = MAX_BOOKMARKS;
    for (int i = 0; i < MAX_BOOKMARKS; i++) {
      c.bookmarks[i] = readU32(rec + BOOKMARKS_OFF + i * 4);
    }
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
  cache.clear();
  if (catalogFile) catalogFile.close();

  if (!SD.exists(CATALOG_PATH)) return false;
  // "r+": read/write, no truncation — FILE_WRITE ("w") would truncate the
  // whole catalog on open. Held open for the rest of the session.
  catalogFile = SD.open(CATALOG_PATH, "r+");
  if (!catalogFile) return false;

  uint8_t header[CAT_HEADER_SIZE];
  catalogFile.seek(0);
  int n = catalogFile.read(header, CAT_HEADER_SIZE);
  if (n != CAT_HEADER_SIZE || memcmp(header, "RCT1", 4) != 0) {
    catalogFile.close();
    return false;
  }
  int count = header[6] | (header[7] << 8); // recordCount, uint16 LE
  cache.reserve(count);

  // Load in a handful of large sequential reads rather than one read per
  // record — far fewer SD transactions for the same ~10ms/transaction cost.
  const int RECORDS_PER_CHUNK = 24;
  uint8_t chunkBuf[RECORDS_PER_CHUNK * RECORD_SIZE]; // ~3.7KB, stack-local, freed on return
  int loaded = 0;
  while (loaded < count) {
    int want = count - loaded;
    if (want > RECORDS_PER_CHUNK) want = RECORDS_PER_CHUNK;
    int got = catalogFile.read(chunkBuf, want * RECORD_SIZE);
    int gotRecords = got / RECORD_SIZE;
    for (int i = 0; i < gotRecords; i++) {
      CachedRecord c;
      parseRecord(chunkBuf + i * RECORD_SIZE, c);
      cache.push_back(c);
    }
    loaded += gotRecords;
    if (gotRecords < want) break; // short read; stop rather than loop forever
  }
  return true;
}

int Storage::bookCount() { return (int)cache.size(); }

bool Storage::getEntry(int index, CatalogEntry &out) {
  if (index < 0 || index >= (int)cache.size()) return false;
  const CachedRecord &c = cache[index];
  out.filename = String(c.filename);
  out.title = String(c.title);
  out.author = String(c.author);
  out.totalLines = c.totalLines;
  out.position = c.position;
  out.bookmarkCount = c.bookmarkCount;
  for (int i = 0; i < MAX_BOOKMARKS; i++) out.bookmarks[i] = c.bookmarks[i];
  return true;
}

void Storage::setPosition(int index, uint32_t line) {
  if (index < 0 || index >= (int)cache.size() || !catalogFile) return;
  cache[index].position = line;

  catalogFile.seek(recordOffset(index) + POSITION_OFF);
  uint8_t buf[4];
  writeU32(buf, line);
  catalogFile.write(buf, 4);
  catalogFile.flush();
}

void Storage::addBookmark(int index, uint32_t line) {
  if (index < 0 || index >= (int)cache.size() || !catalogFile) return;
  CachedRecord &c = cache[index];

  for (uint8_t i = 0; i < c.bookmarkCount; i++) {
    if (c.bookmarks[i] == line) return; // already bookmarked
  }

  if (c.bookmarkCount < MAX_BOOKMARKS) {
    c.bookmarks[c.bookmarkCount] = line;
    c.bookmarkCount++;
  } else {
    // full: drop the oldest bookmark to make room
    for (int i = 0; i < MAX_BOOKMARKS - 1; i++) {
      c.bookmarks[i] = c.bookmarks[i + 1];
    }
    c.bookmarks[MAX_BOOKMARKS - 1] = line;
  }

  catalogFile.seek(recordOffset(index) + BMCOUNT_OFF);
  catalogFile.write(&c.bookmarkCount, 1);
  uint8_t pad[3] = {0, 0, 0};
  catalogFile.write(pad, 3);
  uint8_t buf[4];
  for (int i = 0; i < MAX_BOOKMARKS; i++) {
    writeU32(buf, c.bookmarks[i]);
    catalogFile.write(buf, 4);
  }
  catalogFile.flush();
}
