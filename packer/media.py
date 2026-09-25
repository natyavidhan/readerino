"""Video and image encoders for the device (see formats in the docstrings of
encode_video / encode_image). Needs ffmpeg on PATH for video, Pillow for
images, numpy for both.

The device has 2KB of RAM and no frame buffer: the TFT's own memory *is*
the frame buffer. So video is stored as the exact pixels to rewrite each
frame -- nothing on the device ever has to compare or reconstruct frames.
"""

import struct
import subprocess
from pathlib import Path

import numpy as np

VIDEO_EXTS = {".mp4", ".mkv", ".webm", ".mov", ".avi", ".gif", ".m4v"}
IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".webp"}

VID_W, VID_H = 160, 120  # 4:3 in landscape; the bottom 8px row is the player's progress strip
IMG_W, IMG_H = 160, 128

RVD_MAGIC = b"RVD1"
RVD_1BIT = 1     # header version byte: black/white runs (see encode_frame)
RVD_PALETTE = 2  # header version byte: 64-colour palette (see encode_color_frame)
PALETTE_SIZE = 64  # 128 bytes of RGB565 -- all the player can spare for it in RAM
COLOR_MAX_W, COLOR_MAX_H = 160, 120
RVD_HEADER_FMT = "<4sBBBBIHH"  # magic, version, fps, width, height, frameCount, keyInterval, keyCount
RVD_HEADER_SIZE = struct.calcsize(RVD_HEADER_FMT)
END_OF_FRAME = 0xFF

RIM_MAGIC = b"RIM1"
RIM_HEADER_FMT = "<4sBBH"  # magic, width, height, reserved
RIM_HEADER_SIZE = struct.calcsize(RIM_HEADER_FMT)

# Two changed runs in a row separated by fewer unchanged pixels than this
# are sent as one span: opening a new address window on the TFT costs about
# as much as rewriting a few pixels.
MERGE_GAP = 4


def media_kind(path: Path):
    ext = path.suffix.lower()
    if ext in VIDEO_EXTS:
        return "video"
    if ext in IMAGE_EXTS:
        return "image"
    return None


# ---------------------------------------------------------------------------
# Video
# ---------------------------------------------------------------------------

BAYER4 = (np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) + 0.5) * 16


def read_frames(src: Path, fps: int, dither: bool):
    """Yields 1-bit frames (VID_H x VID_W bool arrays, True = white)."""
    vf = (f"fps={fps},scale={VID_W}:{VID_H}:force_original_aspect_ratio=decrease,"
          f"pad={VID_W}:{VID_H}:(ow-iw)/2:(oh-ih)/2,format=gray")
    proc = subprocess.Popen(["ffmpeg", "-v", "error", "-i", str(src), "-vf", vf, "-f", "rawvideo", "-"],
                            stdout=subprocess.PIPE)
    size = VID_W * VID_H
    threshold = np.tile(BAYER4, (VID_H // 4, VID_W // 4)) if dither else 128
    try:
        while True:
            raw = proc.stdout.read(size)
            if len(raw) < size:
                break
            gray = np.frombuffer(raw, np.uint8).reshape(VID_H, VID_W)
            yield gray >= threshold
    finally:
        proc.stdout.close()
        if proc.wait() != 0:
            raise RuntimeError(f"ffmpeg failed on {src}")


def encode_runs(pixels) -> bytes:
    """One run byte per same-color stretch: bit 7 = color (1 = white),
    bits 0-6 = length - 1 (so 1..128 pixels)."""
    out = bytearray()
    i, n = 0, len(pixels)
    while i < n:
        color = pixels[i]
        j = i + 1
        while j < n and pixels[j] == color and j - i < 128:
            j += 1
        out.append((0x80 if color else 0) | (j - i - 1))
        i = j
    return bytes(out)


def encode_frame(frame, prev) -> bytes:
    """Spans of pixels to rewrite: y, x, n, then run bytes covering exactly
    n pixels; 0xFF ends the frame. prev=None encodes a keyframe (every row
    in full)."""
    out = bytearray()
    for y in range(VID_H):
        row = frame[y]
        if prev is None:
            segments = [(0, VID_W)]
        else:
            changed = np.flatnonzero(row != prev[y])
            if changed.size == 0:
                continue
            segments = []
            start = last = int(changed[0])
            for x in changed[1:]:
                x = int(x)
                if x - last > MERGE_GAP:
                    segments.append((start, last + 1))
                    start = x
                last = x
            segments.append((start, last + 1))
        for x0, x1 in segments:
            out += bytes((y, x0, x1 - x0))
            out += encode_runs(row[x0:x1].tolist())
    out.append(END_OF_FRAME)
    return bytes(out)


def encode_video(src: Path, dst: Path, fps: int = 30, dither: bool = False) -> int:
    """.rvd layout (little-endian):
      header    RVD_HEADER_FMT (16 bytes)
      keytable  keyCount x (uint32 snapshotOffset, uint32 nextOffset)
      frames    frameCount delta frames back to back (frame 0 is a full
                redraw), each as encode_frame() describes
      snapshots keyCount full redraws of frames 0, K, 2K, ... (K = keyInterval)

    Plain playback only ever reads `frames`, so it never pays for a full
    redraw after the first frame. Starting anywhere else (resume, bookmark,
    seek) goes to keyframe k: draw snapshot k, then carry on with the delta
    frames at nextOffset (frame k*K + 1; the file's end if there isn't one).
    Bookmarks and resume points snap to keyframes. Returns the frame count."""
    key_interval = fps  # one keyframe per second
    deltas, snapshots = [], []
    prev = None
    for i, frame in enumerate(read_frames(src, fps, dither)):
        deltas.append(encode_frame(frame, prev))
        if i % key_interval == 0:
            snapshots.append(encode_frame(frame, None))
        prev = frame
    count = len(deltas)
    if count == 0:
        raise RuntimeError(f"no frames decoded from {src}")
    key_count = len(snapshots)

    frames_start = RVD_HEADER_SIZE + 8 * key_count
    frame_offsets = []
    pos = frames_start
    for d in deltas:
        frame_offsets.append(pos)
        pos += len(d)
    frames_end = pos
    table = []
    for k, snap in enumerate(snapshots):
        nxt = k * key_interval + 1
        table += [pos, frame_offsets[nxt] if nxt < count else frames_end]
        pos += len(snap)

    with open(dst, "wb") as f:
        f.write(struct.pack(RVD_HEADER_FMT, RVD_MAGIC, RVD_1BIT, fps, VID_W, VID_H, count, key_interval, key_count))
        f.write(struct.pack(f"<{2 * key_count}I", *table))
        for d in deltas:
            f.write(d)
        for snap in snapshots:
            f.write(snap)
    return count


def _draw_frame(data, pos, screen):
    """Applies one encoded frame at data[pos:] to screen; returns the offset
    just past it."""
    while True:
        y = data[pos]
        pos += 1
        if y == END_OF_FRAME:
            return pos
        x, n = data[pos], data[pos + 1]
        pos += 2
        while n:
            r = data[pos]
            pos += 1
            length = (r & 0x7F) + 1
            screen[y, x:x + length] = bool(r & 0x80)
            x += length
            n -= length


def decode_video(path: Path, start_key: int = 0):
    """Reference decoder -- the device does the same thing, drawing straight
    to the TFT. Yields every frame from keyframe start_key to the end as a
    VID_H x VID_W bool array."""
    data = Path(path).read_bytes()
    magic, _ver, fps, w, h, count, key_interval, key_count = struct.unpack_from(RVD_HEADER_FMT, data)
    assert magic == RVD_MAGIC
    table = struct.unpack_from(f"<{2 * key_count}I", data, RVD_HEADER_SIZE)
    screen = np.zeros((h, w), bool)
    _draw_frame(data, table[2 * start_key], screen)
    yield screen.copy()
    pos = table[2 * start_key + 1]
    for _ in range(start_key * key_interval + 1, count):
        pos = _draw_frame(data, pos, screen)
        yield screen.copy()


# ---------------------------------------------------------------------------
# Colour video (palette)
# ---------------------------------------------------------------------------

def color_size(src: Path):
    """Largest size fitting COLOR_MAX_W x COLOR_MAX_H at the source's aspect
    ratio (16:9 -> 160x90)."""
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                          "stream=width,height", "-of", "csv=p=0", str(src)],
                         capture_output=True, text=True, check=True).stdout
    iw, ih = (int(v) for v in out.strip().split(",")[:2])
    if iw * COLOR_MAX_H >= ih * COLOR_MAX_W:
        return COLOR_MAX_W, max(2, round(COLOR_MAX_W * ih / iw))
    return max(2, round(COLOR_MAX_H * iw / ih)), COLOR_MAX_H


def read_rgb_frames(src: Path, fps: int, w: int, h: int):
    vf = f"fps={fps},scale={w}:{h}:flags=area"
    proc = subprocess.Popen(["ffmpeg", "-v", "error", "-i", str(src), "-vf", vf, "-pix_fmt", "rgb24",
                             "-f", "rawvideo", "-"], stdout=subprocess.PIPE)
    size = w * h * 3
    try:
        while True:
            raw = proc.stdout.read(size)
            if len(raw) < size:
                break
            yield np.frombuffer(raw, np.uint8).reshape(h, w, 3)
    finally:
        proc.stdout.close()
        if proc.wait() != 0:
            raise RuntimeError(f"ffmpeg failed on {src}")


def build_palette(frames):
    """One palette for the whole video: median cut (as GIF does) over a
    mosaic of up to 64 frames spread across it, snapped to RGB565 so what
    the encoder compares is exactly what the screen shows."""
    from PIL import Image
    step = max(1, len(frames) // 64)
    sample = np.concatenate(frames[::step][:64], axis=0)
    pal = Image.fromarray(sample).quantize(PALETTE_SIZE, method=Image.Quantize.MEDIANCUT).getpalette()
    pal = np.array(pal[:PALETTE_SIZE * 3], np.int32).reshape(-1, 3)
    if len(pal) < PALETTE_SIZE:
        pal = np.vstack([pal, np.zeros((PALETTE_SIZE - len(pal), 3), np.int32)])
    return (pal >> [3, 2, 3]) << [3, 2, 3]


def to_indices(frame, pal):
    d = ((frame[:, :, None, :].astype(np.int32) - pal[None, None]) ** 2).sum(-1)
    return d.argmin(-1).astype(np.uint8)


def encode_color_frame(idx, shown, changed) -> bytes:
    """Spans (y, x, n) as for 1-bit video, then records covering exactly n
    pixels: a header byte with bit 7 set = that many (low 7 bits + 1)
    literal palette indices follow; bit 7 clear = a run of (low 7 bits + 1)
    pixels of the one index that follows. 0xFF ends the frame. Every pixel
    inside a span is (re)drawn, so `shown` is updated for the whole span."""
    h, w = idx.shape
    out = bytearray()
    for y in range(h):
        xs = np.flatnonzero(changed[y])
        if xs.size == 0:
            continue
        segments = []
        start = last = int(xs[0])
        for x in xs[1:]:
            x = int(x)
            if x - last > MERGE_GAP:
                segments.append((start, last + 1))
                start = x
            last = x
        segments.append((start, last + 1))
        for x0, x1 in segments:
            row = idx[y, x0:x1].tolist()
            shown[y, x0:x1] = idx[y, x0:x1]
            out += bytes((y, x0, x1 - x0))
            i, lit = 0, []
            def flush():
                while lit:
                    chunk = lit[:128]
                    del lit[:128]
                    out.append(0x80 | (len(chunk) - 1))
                    out.extend(chunk)
            while i < len(row):
                j = i + 1
                while j < len(row) and row[j] == row[i] and j - i < 128:
                    j += 1
                if j - i >= 3:          # worth a run record
                    flush()
                    out += bytes((j - i - 1, row[i]))
                else:
                    lit.extend(row[i:j])
                i = j
            flush()
    out.append(END_OF_FRAME)
    return bytes(out)


def encode_color_video(src: Path, dst: Path, fps: int = 20, threshold: int = 48, keep=None) -> int:
    """.rvd with header version RVD_PALETTE: same header / key table /
    frames / snapshots layout as encode_video, plus the 64-entry RGB565
    palette (big-endian) right after the key table. A pixel is only
    redrawn when its colour moved more than `threshold` (sum of |dR|+|dG|+
    |dB| on 0-255 channels) from what's on screen, so compression noise
    doesn't count as motion. `keep`, if a list, receives the exact screen
    state after every frame (for verification)."""
    w, h = color_size(src)
    frames = list(read_rgb_frames(src, fps, w, h))
    if not frames:
        raise RuntimeError(f"no frames decoded from {src}")
    pal = build_palette(frames)
    dist = np.abs(pal[:, None, :] - pal[None, :, :]).sum(-1)
    key_interval = fps
    deltas, snapshots = [], []
    shown = None
    for i, frame in enumerate(frames):
        idx = to_indices(frame, pal)
        if shown is None:
            shown = np.zeros_like(idx)
            changed = np.ones(idx.shape, bool)
        else:
            changed = dist[shown, idx] > threshold
        deltas.append(encode_color_frame(idx, shown, changed))
        if i % key_interval == 0:
            snapshots.append(encode_color_frame(shown.copy(), shown.copy(), np.ones(idx.shape, bool)))
        if keep is not None:
            keep.append(shown.copy())
    count, key_count = len(deltas), len(snapshots)
    rgb565 = ((pal[:, 0] >> 3) << 11) | ((pal[:, 1] >> 2) << 5) | (pal[:, 2] >> 3)
    palette_bytes = rgb565.astype(">u2").tobytes()

    frames_start = RVD_HEADER_SIZE + 8 * key_count + len(palette_bytes)
    frame_offsets, pos = [], frames_start
    for d in deltas:
        frame_offsets.append(pos)
        pos += len(d)
    frames_end, table = pos, []
    for k, snap in enumerate(snapshots):
        nxt = k * key_interval + 1
        table += [pos, frame_offsets[nxt] if nxt < count else frames_end]
        pos += len(snap)
    with open(dst, "wb") as f:
        f.write(struct.pack(RVD_HEADER_FMT, RVD_MAGIC, RVD_PALETTE, fps, w, h, count, key_interval, key_count))
        f.write(struct.pack(f"<{2 * key_count}I", *table))
        f.write(palette_bytes)
        for d in deltas:
            f.write(d)
        for snap in snapshots:
            f.write(snap)
    return count


def _draw_color_frame(data, pos, screen):
    while True:
        y = data[pos]
        pos += 1
        if y == END_OF_FRAME:
            return pos
        x, n = data[pos], data[pos + 1]
        pos += 2
        while n:
            hdr = data[pos]
            length = (hdr & 0x7F) + 1
            if hdr & 0x80:
                screen[y, x:x + length] = list(data[pos + 1:pos + 1 + length])
                pos += 1 + length
            else:
                screen[y, x:x + length] = data[pos + 1]
                pos += 2
            x += length
            n -= length


def decode_color_video(path: Path, start_key: int = 0):
    """Reference decoder for RVD_PALETTE files: yields the screen as palette
    indices (h x w uint8) after every frame from keyframe start_key on."""
    data = Path(path).read_bytes()
    magic, ver, fps, w, h, count, key_interval, key_count = struct.unpack_from(RVD_HEADER_FMT, data)
    assert magic == RVD_MAGIC and ver == RVD_PALETTE
    table = struct.unpack_from(f"<{2 * key_count}I", data, RVD_HEADER_SIZE)
    screen = np.zeros((h, w), np.uint8)
    _draw_color_frame(data, table[2 * start_key], screen)
    yield screen.copy()
    pos = table[2 * start_key + 1]
    for _ in range(start_key * key_interval + 1, count):
        pos = _draw_color_frame(data, pos, screen)
        yield screen.copy()


# ---------------------------------------------------------------------------
# Image
# ---------------------------------------------------------------------------

def encode_image(src: Path, dst: Path) -> None:
    """.rim layout: header RIM_HEADER_FMT (8 bytes), then width*height
    pixels as big-endian RGB565 -- exactly the bytes the TFT takes, so the
    device streams them from the card straight to the screen. Scaled to fit
    160x128 keeping its aspect ratio; the device centers it."""
    from PIL import Image
    img = Image.open(src).convert("RGB")
    img.thumbnail((IMG_W, IMG_H), Image.LANCZOS)
    a = np.asarray(img, dtype=np.uint16)
    rgb565 = ((a[..., 0] & 0xF8) << 8) | ((a[..., 1] & 0xFC) << 3) | (a[..., 2] >> 3)
    with open(dst, "wb") as f:
        f.write(struct.pack(RIM_HEADER_FMT, RIM_MAGIC, img.width, img.height, 0))
        f.write(rgb565.astype(">u2").tobytes())
