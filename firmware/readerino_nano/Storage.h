#pragma once
#include <Arduino.h>
#include "Config.h"

// A single catalog record, decoded from catalog.bin on demand. There is no
// RAM cache of the catalog on this build — at 2KB total SRAM, caching even
// a modest library isn't viable, so every read goes to the SD card.
// Screens must only fetch the rows they actually draw.
//
// Fields are sized for what this build actually uses, not the full on-disk
// widths: title is truncated to what the screen can show (nothing here can
// display more than RD_COLS anyway), filename to what the packer
// actually generates ("/b0001.rbk"), and author is dropped — nothing in
// this UI displays it.
struct CatalogEntry {
  char filename[FILENAME_LEN];
  char title[TITLE_LEN];
  uint32_t totalLines;
  uint32_t position; // last-read line index
  uint8_t bookmarkCount;
  uint32_t bookmarks[MAX_BOOKMARKS];
};

namespace Storage {
  bool begin();
  bool rescan();
  int bookCount();
  bool getEntry(int index, CatalogEntry &out);
  void setPosition(int index, uint32_t line);
  void addBookmark(int index, uint32_t line);    // drops the oldest when full
  void removeBookmark(int index, uint32_t line);
}
