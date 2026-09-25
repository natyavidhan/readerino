#!/usr/bin/env python3
"""Pack books (.txt/.pdf/.epub), videos (.mp4/.mkv/.webm/...) and images
(.png/.jpg/...) into the readerino device's binary library format: one
.rbk / .rvd / .rim file each plus a catalog.bin index, ready to push onto
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
import media

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
    return [f for f in files if f.suffix.lower() in EXTRACTORS or media.media_kind(f)]


def media_title(src: Path) -> str:
    """'bad_apple.mp4' -> 'Bad Apple'; drops a trailing yt-dlp '[videoid]'
    and keeps existing capitals ('4K HD' stays '4K HD')."""
    import re
    stem = re.sub(r"\s*\[[A-Za-z0-9_-]{6,}\]$", "", src.stem)
    words = stem.replace("_", " ").replace("-", " ").split()
    return " ".join(w[:1].upper() + w[1:] for w in words) or src.stem


def item_id(manifest: dict, key: str) -> int:
    if key in manifest["books"]:
        return manifest["books"][key]["id"]
    item = manifest["next_id"]
    manifest["next_id"] += 1
    return item


def pack_media(src: Path, kind: str, out_dir: Path, manifest: dict, fps: int, dither: bool, color: bool):
    key = str(src.resolve())
    item = item_id(manifest, key)
    title = to_ascii(media_title(src))
    if kind == "video":
        name = f"v{item:04d}.rvd"
        if color:
            frames = media.encode_color_video(src, out_dir / name, fps)
        else:
            frames = media.encode_video(src, out_dir / name, fps, dither)
        entry = {"total_lines": frames, "fps": fps}
        info = f"{frames} frames @ {fps}fps{', 64-colour' if color else ''}"
    else:
        name = f"i{item:04d}.rim"
        media.encode_image(src, out_dir / name)
        entry = {"total_lines": 1, "fps": 0}
        info = "image"
    size = (out_dir / name).stat().st_size
    manifest["books"][key] = {"id": item, "rbk": name, "title": title, "author": "", "kind": kind, **entry}
    print(f"OK    {src.name} -> {name}  ({info}, {size / 1e6:.2f} MB) {title!r}")


def pack(inputs: list[Path], out_dir: Path, width: int = WRAP_WIDTH, fps: int = 30, dither: bool = False,
         color: bool = False):
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest = load_manifest(out_dir)

    for src in inputs:
        kind = media.media_kind(src)
        if kind:
            try:
                pack_media(src, kind, out_dir, manifest, fps, dither, color)
            except Exception as e:
                print(f"FAIL  {src}: {e}", file=sys.stderr)
            continue
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
        book_id = item_id(manifest, key)

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
            "kind": b.get("kind", "book"),
            "fps": b.get("fps", 0),
        })

    write_catalog(cat_path, records)
    print(f"\ncatalog.bin: {len(records)} items")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="files or directories to pack (books, videos, images)")
    ap.add_argument("-o", "--out", required=True, help="output library directory")
    ap.add_argument("--width", type=int, default=WRAP_WIDTH, help=f"wrap width in characters (default {WRAP_WIDTH}, must match the firmware's RD_COLS)")
    ap.add_argument("--fps", type=int, default=30, help="video frame rate (default 30)")
    ap.add_argument("--dither", action="store_true",
                    help="dither videos to 1-bit instead of a hard black/white threshold -- better for "
                         "ordinary footage, but much bigger and slower to play than clean black-and-white video")
    ap.add_argument("--color", action="store_true",
                    help="colour video: 64-colour palette picked from the clip, up to 160x120 at its aspect "
                         "ratio (16:9 -> 160x90). Use ~15 fps; the default 1-bit mode suits black-and-white clips")
    args = ap.parse_args()

    files = collect_inputs(args.inputs)
    if not files:
        print("no supported input files found", file=sys.stderr)
        sys.exit(1)

    pack(files, Path(args.out), args.width, args.fps, args.dither, args.color)


if __name__ == "__main__":
    main()
