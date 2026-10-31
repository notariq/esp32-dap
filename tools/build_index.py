#!/usr/bin/env python3
"""
Build the ESP32-DAP library index.

Usage:
    python build_index.py /path/to/sdcard-root [--cover-size 128] [--no-covers]

Writes to <root>/.dap/ :
    artists.bin  albums.bin  tracks.bin   fixed-size binary tables (see README)
    covers/<albumIdx>.jpg                 square baseline JPEGs, one per album

Run it again whenever you add or change music.
"""
import argparse
import io
import os
import re
import shutil
import struct
import sys
from pathlib import Path

try:
    import mutagen
    from mutagen.flac import FLAC
    from mutagen.id3 import ID3
    from mutagen.mp4 import MP4, MP4Tags
    from PIL import Image
except ImportError as exc:  # pragma: no cover
    sys.exit(f"Missing dependency ({exc}). Run: pip install -r requirements.txt")

AUDIO_EXTS = {".mp3", ".flac", ".wav", ".aac", ".m4a"}
SKIP_DIRS = {"System Volume Information", "$RECYCLE.BIN"}
COVER_STEMS = ["cover", "folder", "front", "album", "albumart"]
COVER_EXTS = [".jpg", ".jpeg", ".png"]

# Must match src/library.h
FMT_ARTIST = "<IHH56s"          # firstAlbum, albumCount, trackCount, name
FMT_ALBUM = "<IIHHB3x112s"      # artistIdx, firstTrack, trackCount, year, hasCover, name
FMT_TRACK = "<IIHHIHH80s156s"   # albumIdx, artistIdx, trackNo, discNo, duration, year, rsv, title, path
assert struct.calcsize(FMT_ARTIST) == 64
assert struct.calcsize(FMT_ALBUM) == 128
assert struct.calcsize(FMT_TRACK) == 256
MAX_PATH = 155


def clip(text, size):
    """UTF-8 encode and truncate to size-1 bytes without cutting a character."""
    raw = text.encode("utf-8")[: size - 1]
    return raw.decode("utf-8", "ignore").encode("utf-8")


def as_text(value):
    if value is None:
        return ""
    if isinstance(value, (list, tuple)):
        value = value[0] if value else ""
    return str(value).strip()


def as_num(value):
    if isinstance(value, (list, tuple)):
        value = value[0] if value else 0
    if isinstance(value, tuple):
        value = value[0]
    m = re.match(r"\d+", str(value or ""))
    return int(m.group()) if m else 0


def as_year(value):
    m = re.search(r"(\d{4})", as_text(value))
    return int(m.group(1)) if m else 0


def read_tags(raw):
    t = dict(artist="", albumartist="", album="", title="", track=0, disc=0, year=0)
    tags = raw.tags
    if tags is None:
        return t
    if isinstance(tags, ID3):
        t["artist"] = as_text(tags.get("TPE1"))
        t["albumartist"] = as_text(tags.get("TPE2"))
        t["album"] = as_text(tags.get("TALB"))
        t["title"] = as_text(tags.get("TIT2"))
        t["track"] = as_num(as_text(tags.get("TRCK")))
        t["disc"] = as_num(as_text(tags.get("TPOS")))
        t["year"] = as_year(tags.get("TDRC") or tags.get("TYER"))
    elif isinstance(tags, MP4Tags):
        t["artist"] = as_text(tags.get("\xa9ART"))
        t["albumartist"] = as_text(tags.get("aART"))
        t["album"] = as_text(tags.get("\xa9alb"))
        t["title"] = as_text(tags.get("\xa9nam"))
        t["track"] = as_num(tags.get("trkn"))
        t["disc"] = as_num(tags.get("disk"))
        t["year"] = as_year(tags.get("\xa9day"))
    else:  # Vorbis comments (FLAC, ...)
        g = lambda k: tags.get(k) if hasattr(tags, "get") else None
        t["artist"] = as_text(g("artist"))
        t["albumartist"] = as_text(g("albumartist"))
        t["album"] = as_text(g("album"))
        t["title"] = as_text(g("title"))
        t["track"] = as_num(as_text(g("tracknumber")))
        t["disc"] = as_num(as_text(g("discnumber")))
        t["year"] = as_year(g("date") or g("year"))
    return t


def embedded_cover(raw):
    tags = raw.tags
    if isinstance(tags, ID3):
        pics = tags.getall("APIC")
        return pics[0].data if pics else None
    if isinstance(raw, FLAC):
        return raw.pictures[0].data if raw.pictures else None
    if isinstance(raw, MP4) and tags and "covr" in tags and tags["covr"]:
        return bytes(tags["covr"][0])
    return None


def folder_cover(directory):
    try:
        names = {p.name.lower(): p for p in directory.iterdir() if p.is_file()}
    except OSError:
        return None
    for stem in COVER_STEMS:
        for ext in COVER_EXTS:
            p = names.get(stem + ext)
            if p:
                return p.read_bytes()
    return None


def make_cover(data, size):
    try:
        img = Image.open(io.BytesIO(data)).convert("RGB")
        w, h = img.size
        s = min(w, h)
        img = img.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s))
        img = img.resize((size, size), Image.LANCZOS)
        out = io.BytesIO()
        img.save(out, "JPEG", quality=85, progressive=False)   # baseline: required by the ESP32 decoder
        return out.getvalue()
    except Exception:
        return None


def find_audio(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith(".") and d not in SKIP_DIRS)
        for name in sorted(filenames):
            if name.startswith("."):
                continue
            if Path(name).suffix.lower() in AUDIO_EXTS:
                yield Path(dirpath) / name


def main():
    ap = argparse.ArgumentParser(description="Build the ESP32-DAP library index")
    ap.add_argument("root", type=Path, help="SD card root (or a folder that will become it)")
    ap.add_argument("--cover-size", type=int, default=128, help="cover edge in px (match COVER_SIZE in config.h)")
    ap.add_argument("--no-covers", action="store_true")
    args = ap.parse_args()

    root = args.root.resolve()
    if not root.is_dir():
        sys.exit(f"{root} is not a directory")

    albums = {}   # (albumartist, album) casefolded -> dict
    n_files = n_skipped = 0

    for path in find_audio(root):
        n_files += 1
        if n_files % 200 == 0:
            print(f"  scanned {n_files} files...", flush=True)
        rel = "/" + path.relative_to(root).as_posix()
        if len(rel.encode("utf-8")) > MAX_PATH:
            print(f"  skip (path > {MAX_PATH} bytes): {rel}")
            n_skipped += 1
            continue
        try:
            raw = mutagen.File(path)
        except Exception as exc:
            print(f"  skip (unreadable: {exc}): {rel}")
            n_skipped += 1
            continue
        if raw is None or raw.info is None:
            print(f"  skip (not audio?): {rel}")
            n_skipped += 1
            continue

        t = read_tags(raw)
        folder = path.parent
        title = t["title"] or path.stem
        album = t["album"] or (folder.name if folder != root else "Unknown Album")
        artist = t["albumartist"] or t["artist"]
        if not artist:
            artist = folder.parent.name if folder.parent != root and folder != root else "Unknown Artist"

        key = (artist.casefold(), album.casefold())
        a = albums.setdefault(key, dict(artist=artist, album=album, year=0, tracks=[],
                                        cover=None, tries=0, folder=folder))
        a["year"] = a["year"] or t["year"]
        a["tracks"].append(dict(title=title, disc=t["disc"], no=t["track"],
                                dur=int(getattr(raw.info, "length", 0) or 0),
                                year=t["year"], path=rel))

        if not args.no_covers and a["cover"] is None and a["tries"] < 3:
            a["tries"] += 1
            data = embedded_cover(raw) or folder_cover(folder)
            if data:
                a["cover"] = make_cover(data, args.cover_size)

    if not albums:
        sys.exit("No supported audio files found.")

    # ---- sort: artists, then albums (year, name), then tracks (disc, no, title)
    artist_names = sorted({a["artist"] for a in albums.values()}, key=str.casefold)
    artist_idx = {n: i for i, n in enumerate(artist_names)}
    album_list = sorted(albums.values(),
                        key=lambda a: (artist_idx[a["artist"]], a["year"] or 9999, a["album"].casefold()))

    out_dir = root / ".dap"
    cov_dir = out_dir / "covers"
    if cov_dir.exists():
        shutil.rmtree(cov_dir)
    cov_dir.mkdir(parents=True, exist_ok=True)

    artist_rows = [[0, 0, 0, n] for n in artist_names]       # first, albums, tracks, name
    for ar in artist_rows:
        ar[0] = -1
    album_bin, track_bin = [], []
    n_tracks = n_covers = 0

    for ai, a in enumerate(album_list):
        a["tracks"].sort(key=lambda x: (x["disc"], x["no"], x["title"].casefold(), x["path"]))
        ar = artist_rows[artist_idx[a["artist"]]]
        if ar[0] < 0:
            ar[0] = ai
        ar[1] += 1
        ar[2] += len(a["tracks"])
        has_cover = 1 if a["cover"] else 0
        if has_cover:
            (cov_dir / f"{ai}.jpg").write_bytes(a["cover"])
            n_covers += 1
        album_bin.append(struct.pack(FMT_ALBUM, artist_idx[a["artist"]], n_tracks, len(a["tracks"]),
                                     a["year"], has_cover, clip(a["album"], 112)))
        for tr in a["tracks"]:
            track_bin.append(struct.pack(FMT_TRACK, ai, artist_idx[a["artist"]], tr["no"], tr["disc"],
                                         tr["dur"], tr["year"] or a["year"], 0,
                                         clip(tr["title"], 80), clip(tr["path"], 156)))
            n_tracks += 1

    artist_bin = [struct.pack(FMT_ARTIST, first, cnt, min(trk, 65535), clip(name, 56))
                  for first, cnt, trk, name in artist_rows]

    (out_dir / "artists.bin").write_bytes(b"".join(artist_bin))
    (out_dir / "albums.bin").write_bytes(b"".join(album_bin))
    (out_dir / "tracks.bin").write_bytes(b"".join(track_bin))

    print(f"\nIndexed {n_tracks} tracks / {len(album_list)} albums / {len(artist_names)} artists "
          f"({n_covers} covers, {n_skipped} files skipped) -> {out_dir}")
    print(f"RAM needed on the ESP32: ~{(n_tracks * 256 + len(album_list) * 128 + len(artist_names) * 64) // 1024} KB (PSRAM)")


if __name__ == "__main__":
    main()
