"""Binary format definitions shared with the firmware's catalog and book
readers (firmware/readerino_nano/Storage.cpp and Book.cpp).

.rbk book format (little-endian, no implicit padding):
  offset  size  field
  0       4     magic = b"RBK1"
  4       1     version = 1
  5       1     charsPerLine (wrap width used to produce this file)
  6       2     reserved
  8       4     titleLen
  12      4     authorLen
  16      4     lineCount (N)
  20      4     textBlobLen
  24      ...   title bytes (titleLen)
  ...     ...   author bytes (authorLen)
  ...     ...   line offset table: (N+1) x uint32, relative to text blob start
  ...     ...   text blob (textBlobLen bytes), wrapped lines back-to-back

catalog.bin format:
  offset  size  field
  0       4     magic = b"RCT1"
  4       1     version = 1
  5       1     reserved
  6       2     recordCount
  8       ...   records, RECORD_SIZE bytes each

catalog record (156 bytes):
  offset  size  field
  0       32    filename (null-padded), e.g. "/b0001.rbk"
  32      48    title (null-padded)
  80      32    author (null-padded)
  112     4     totalLines
  116     4     position (last-read line index)
  120     1     bookmarkCount (0-8)
  121     3     reserved
  124     32    bookmarks: uint32[8]
"""

import struct

RBK_MAGIC = b"RBK1"
RBK_HEADER_FMT = "<4sBBHIIII"
RBK_HEADER_SIZE = struct.calcsize(RBK_HEADER_FMT)

CAT_MAGIC = b"RCT1"
CAT_HEADER_FMT = "<4sBBH"
CAT_HEADER_SIZE = struct.calcsize(CAT_HEADER_FMT)

MAX_BOOKMARKS = 8
RECORD_FMT = f"<32s48s32sIIB3x{MAX_BOOKMARKS}I"
RECORD_SIZE = struct.calcsize(RECORD_FMT)

WRAP_WIDTH = 24  # the reader's line width, Theme.h RD_COLS


def write_rbk(path, title: str, author: str, lines: list[str], width: int = WRAP_WIDTH):
    title_b = title.encode("ascii", "ignore")[:255]
    author_b = author.encode("ascii", "ignore")[:255]

    blob_parts = []
    offsets = [0]
    pos = 0
    for line in lines:
        b = line.encode("ascii", "ignore")
        blob_parts.append(b)
        pos += len(b)
        offsets.append(pos)
    blob = b"".join(blob_parts)

    header = struct.pack(
        RBK_HEADER_FMT,
        RBK_MAGIC, 1, width, 0,
        len(title_b), len(author_b), len(lines), len(blob),
    )
    with open(path, "wb") as f:
        f.write(header)
        f.write(title_b)
        f.write(author_b)
        for off in offsets:
            f.write(struct.pack("<I", off))
        f.write(blob)


def read_catalog(path) -> list[dict]:
    records = []
    if not path.exists():
        return records
    with open(path, "rb") as f:
        header = f.read(CAT_HEADER_SIZE)
        if len(header) < CAT_HEADER_SIZE:
            return records
        magic, _version, _reserved, count = struct.unpack(CAT_HEADER_FMT, header)
        if magic != CAT_MAGIC:
            return records
        for _ in range(count):
            raw = f.read(RECORD_SIZE)
            if len(raw) < RECORD_SIZE:
                break
            fname, title, author, total_lines, position, bmcount, *bookmarks = \
                struct.unpack(RECORD_FMT, raw)
            records.append({
                "filename": fname.split(b"\x00", 1)[0].decode("ascii", "ignore"),
                "title": title.split(b"\x00", 1)[0].decode("ascii", "ignore"),
                "author": author.split(b"\x00", 1)[0].decode("ascii", "ignore"),
                "total_lines": total_lines,
                "position": position,
                "bookmarks": list(bookmarks[:bmcount]),
            })
    return records


def write_catalog(path, books: list[dict]):
    """`books` is a list of dicts with: filename, title, author, total_lines,
    position, bookmarks — already merged with any previous progress."""
    with open(path, "wb") as f:
        f.write(struct.pack(CAT_HEADER_FMT, CAT_MAGIC, 1, 0, len(books)))
        for b in books:
            bookmarks = list(b.get("bookmarks", []))[:MAX_BOOKMARKS]
            padded = bookmarks + [0] * (MAX_BOOKMARKS - len(bookmarks))
            f.write(struct.pack(
                RECORD_FMT,
                b["filename"].encode("ascii", "ignore"),
                b["title"].encode("ascii", "ignore"),
                b.get("author", "").encode("ascii", "ignore"),
                b.get("total_lines", 0),
                b.get("position", 0),
                len(bookmarks),
                *padded,
            ))
