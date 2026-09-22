#include "Storage.h"
#include "Config.h"
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>

namespace {
  bool loadDoc(JsonDocument &doc) {
    if (!SD.exists(BOOKMARKS_FILE)) return true; // empty doc is fine
    File f = SD.open(BOOKMARKS_FILE, FILE_READ);
    if (!f) return false;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    return !err;
  }

  bool saveDoc(JsonDocument &doc) {
    // Arduino's FILE_WRITE appends, so remove the old file before rewriting it.
    if (SD.exists(BOOKMARKS_FILE)) SD.remove(BOOKMARKS_FILE);
    File f = SD.open(BOOKMARKS_FILE, FILE_WRITE);
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
  }
}

bool Storage::begin() {
  return SD.begin(PIN_SD_CS);
}

bool Storage::listBooks(std::vector<BookInfo> &out) {
  out.clear();
  File root = SD.open("/");
  if (!root) return false;
  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      String lower = name;
      lower.toLowerCase();
      if (lower.endsWith(".txt")) {
        BookInfo b;
        b.name = name.startsWith("/") ? name : ("/" + name);
        out.push_back(b);
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  return true;
}

int Storage::getSavedPosition(const String &filename) {
  JsonDocument doc;
  if (!loadDoc(doc)) return 0;
  return doc[filename]["position"] | 0;
}

void Storage::savePosition(const String &filename, int lineIndex) {
  JsonDocument doc;
  loadDoc(doc);
  doc[filename]["position"] = lineIndex;
  saveDoc(doc);
}

std::vector<int> Storage::getBookmarks(const String &filename) {
  std::vector<int> result;
  JsonDocument doc;
  if (!loadDoc(doc)) return result;
  JsonArrayConst arr = doc[filename]["bookmarks"];
  if (arr.isNull()) return result;
  for (JsonVariantConst v : arr) result.push_back(v.as<int>());
  return result;
}

void Storage::addBookmark(const String &filename, int lineIndex) {
  JsonDocument doc;
  loadDoc(doc);
  JsonArray arr = doc[filename]["bookmarks"];
  if (arr.isNull()) arr = doc[filename]["bookmarks"].to<JsonArray>();
  for (JsonVariant v : arr) {
    if (v.as<int>() == lineIndex) return; // already bookmarked, nothing to save
  }
  arr.add(lineIndex);
  saveDoc(doc);
}
