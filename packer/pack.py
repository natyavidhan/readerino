#!/usr/bin/env python3
"""Pack .txt/.pdf/.epub files into the readerino device's binary library
format: one .rbk file per book plus a catalog.bin index, ready to push onto
the SD card over the existing serial transfer protocol.

Usage:
    python3 pack.py <files or directories...> -o <output library dir>

Re-running against a previously-packed output directory updates books in
place (matched by source path via .pack_manifest.json) without losing their
saved reading position or bookmarks, and appends any new books.
"""

import argparse
import json
import sys
from pathlib import Path

from extract import EXTRACTORS
from textutil import wrap_text, to_ascii
from formats import write_rbk, write_catalog, read_catalog, WRAP_WIDTH

MANIFEST_NAME = ".pack_manifest.json"


def load_manifest(out_dir: Path) -> dict:
    p = out_dir / MANIFEST_NAME
    if p.exists():
        return json.loads(p.read_text())
    return {"next_id": 1, "books": {}}


def save_manifest(out_dir: Path, manifest: dict):
    (out_dir / MANIFEST_NAME).write_text(json.dumps(manifest, indent=2))


def collect_inputs(raw_inputs: list[str]) -> list[Path]:
    files = []
    for raw in raw_inputs:
        p = Path(raw)
        if p.is_dir():
            files.extend(sorted(p.rglob("*")))
        elif p.is_file():
            files.append(p)
        else:
            print(f"warning: not found, skipping: {raw}", file=sys.stderr)
    return [f for f in files if f.suffix.lower() in EXTRACTORS]


def pack(inputs: list[Path], out_dir: Path, width: int = WRAP_WIDTH):
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest = load_manifest(out_dir)

    for src in inputs:
        extractor = EXTRACTORS[src.suffix.lower()]
        try:
            title, author, body = extractor(src)
        except Exception as e:
            print(f"FAIL  {src}: {e}", file=sys.stderr)
            continue

        title = to_ascii(title).strip() or src.stem
        author = to_ascii(author).strip()

        lines = wrap_text(body, width)
        if not lines:
            print(f"SKIP  {src} (no extractable text)", file=sys.stderr)
            continue

        key = str(src.resolve())
        if key in manifest["books"]:
            book_id = manifest["books"][key]["id"]
        else:
            book_id = manifest["next_id"]
            manifest["next_id"] += 1

        rbk_name = f"b{book_id:04d}.rbk"
        write_rbk(out_dir / rbk_name, title, author, lines, width)

        manifest["books"][key] = {
            "id": book_id,
            "rbk": rbk_name,
            "title": title,
            "author": author,
            "total_lines": len(lines),
        }
        print(f"OK    {src.name} -> {rbk_name}  ({len(lines)} lines) {title!r}")

    save_manifest(out_dir, manifest)
    build_catalog(out_dir, manifest)


def build_catalog(out_dir: Path, manifest: dict):
    cat_path = out_dir / "catalog.bin"
    existing = {r["filename"]: r for r in read_catalog(cat_path)}

    books = sorted(manifest["books"].values(), key=lambda b: b["id"])
    records = []
    for b in books:
        fname = f"/{b['rbk']}"
        prev = existing.get(fname, {})
        records.append({
            "filename": fname,
            "title": b["title"],
            "author": b.get("author", ""),
            "total_lines": b["total_lines"],
            "position": prev.get("position", 0),
            "bookmarks": prev.get("bookmarks", []),
        })

    write_catalog(cat_path, records)
    print(f"\ncatalog.bin: {len(records)} books")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="files or directories to pack (.txt/.pdf/.epub)")
    ap.add_argument("-o", "--out", required=True, help="output library directory")
    ap.add_argument("--width", type=int, default=WRAP_WIDTH, help=f"wrap width in characters (default {WRAP_WIDTH}, must match the firmware's RD_COLS)")
    args = ap.parse_args()

    files = collect_inputs(args.inputs)
    if not files:
        print("no supported input files found (.txt/.pdf/.epub)", file=sys.stderr)
        sys.exit(1)

    pack(files, Path(args.out), args.width)


if __name__ == "__main__":
    main()
