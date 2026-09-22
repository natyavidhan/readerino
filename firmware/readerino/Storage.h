#pragma once
#include <Arduino.h>

#define MAX_BOOKMARKS 8

struct CatalogEntry {
  String filename; // the .rbk file's SD path, e.g. "/b0001.rbk"
  String title;
  String author;
  uint32_t totalLines;
  uint32_t position; // last-read line index (page-aligned)
  uint8_t bookmarkCount;
  uint32_t bookmarks[MAX_BOOKMARKS];
};

// Reads/writes catalog.bin: a fixed-size-record binary index built by the
// host-side Python packer (see packer/formats.py, which this layout must
// stay in sync with). Every record is the same size, so a single field
// update seeks straight to it and rewrites just that record — no need to
// read, parse and rewrite the whole file the way the old JSON bookmarks
// store did.
namespace Storage {
  bool begin();
  bool rescan(); // re-reads the catalog header after the SD contents may have changed

  int bookCount();
  bool getEntry(int index, CatalogEntry &out);

  void setPosition(int index, uint32_t line);
  void addBookmark(int index, uint32_t line);
}
