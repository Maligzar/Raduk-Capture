#!/usr/bin/env python3
"""
ser_exposure_audit.py -- how exposed were the planets in these SER files?

Phase 140.0. Reads every .ser under a folder, samples a few frames from
each, and reports how much of the TARGET is clipped, not how much of the
frame. It also reports the figure auto-exposure steers by today (the
whole-frame 99.9th percentile) and how much of the frame the target
covers, because the suspected fault is that a planet covering under 0.1%
of the frame leaves that percentile on the sky, so AE keeps adding light.

Standard library only. Read-only: it never writes beside the captures.

    python tools\\ser_exposure_audit.py D:\\RadukCapture\\captures
    python tools\\ser_exposure_audit.py <folder> --frames 8 --csv audit.csv

Paste the printed table into the session, or attach the CSV.

How it reads a file:
  - Header is the 178-byte SER v3 header. Samples are little-endian
    whatever the LittleEndian flag says (RadukCapture writes 0 over
    little-endian data, as FireCapture does; see SERWriter.cpp).
  - 16-bit samples are read by their high byte, i.e. as 8-bit levels.
    That is right for ZWO RAW16, which fills the top of the word; if a
    file's peak reads oddly low, the data may be right-aligned instead.
  - FrameCount 0 (a run that never closed) is replaced by the number of
    whole frames the file size holds.
  - Exposure and gain come from <base>_observation.txt when it is there.

What the columns mean:
  peak%       level of the 99.99th-percentile pixel, % of full scale
  ae_hl       whole-frame 99.9th-percentile level (0-255): what AE sees
  tgt_px      pixels at or above half the peak: the target, roughly
  tgt_frame%  tgt_px as % of the frame; under 0.1 means AE is metering sky
  clip_tgt%   % of target pixels at 250 or more: clipped
  verdict     OVER if clip_tgt% > 1, HOT if peak% > 90, else ok
"""

import argparse
import collections
import csv
import os
import struct
import sys

HEADER = 178
CLIP_LEVEL = 250


def read_header(f):
    raw = f.read(HEADER)
    if len(raw) < HEADER or raw[:14] != b"LUCAM-RECORDER":
        return None
    lu, color, le, w, h, depth, count = struct.unpack_from("<7i", raw, 14)
    return {"color": color, "w": w, "h": h, "depth": depth, "count": count}


def planes(color):
    # 100 RGB and 101 BGR are three planes per pixel; everything else one.
    return 3 if color in (100, 101) else 1


def observation(ser_path):
    base = os.path.splitext(ser_path)[0]
    out = {}
    try:
        with open(base + "_observation.txt", encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                out[k.strip()] = v.strip()
    except OSError:
        pass
    return out


def frame_hist(f, offset, nbytes, depth):
    f.seek(offset)
    data = f.read(nbytes)
    if len(data) < nbytes:
        return None
    if depth > 8:
        data = data[1::2]          # high byte of each little-endian sample
    return collections.Counter(data)


def level_at(hist, total, frac):
    """Lowest level with more than frac of the samples at or below it."""
    want = total * frac
    seen = 0
    for lv in range(256):
        seen += hist.get(lv, 0)
        if seen > want:
            return lv
    return 255


def audit(path, nframes):
    obs = observation(path)
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        h = read_header(f)
        if not h:
            return {"file": path, "error": "not a SER file"}
        bpp = 2 if h["depth"] > 8 else 1
        fbytes = h["w"] * h["h"] * planes(h["color"]) * bpp
        if fbytes <= 0:
            return {"file": path, "error": "zero-size frames"}
        whole = (size - HEADER) // fbytes
        count = h["count"] if 0 < h["count"] <= whole else whole
        if count <= 0:
            return {"file": path, "error": "no whole frames"}
        n = max(1, min(nframes, count))
        picks = sorted({int(i * (count - 1) / max(1, n - 1)) for i in range(n)})

        rows = []
        for i in picks:
            hist = frame_hist(f, HEADER + i * fbytes, fbytes, h["depth"])
            if hist is None:
                continue
            total = sum(hist.values())
            peak = level_at(hist, total, 0.9999)
            ae_hl = level_at(hist, total, 0.999)
            half = max(1, peak // 2)
            tgt = sum(c for lv, c in hist.items() if lv >= half)
            clip = sum(c for lv, c in hist.items() if lv >= CLIP_LEVEL)
            rows.append((peak, ae_hl, tgt, clip, total))
    if not rows:
        return {"file": path, "error": "could not read frames"}

    def med(xs):
        xs = sorted(xs)
        return xs[len(xs) // 2]

    peak = med([r[0] for r in rows])
    ae_hl = med([r[1] for r in rows])
    tgt = med([r[2] for r in rows])
    total = rows[0][4]
    clip_tgt = med([100.0 * r[3] / r[2] if r[2] else 0.0 for r in rows])
    peak_pct = 100.0 * peak / 255.0
    verdict = "OVER" if clip_tgt > 1.0 else ("HOT" if peak_pct > 90.0 else "ok")
    return {
        "file": os.path.basename(path),
        "target": obs.get("target", ""),
        "exp_ms": obs.get("exposure_ms", ""),
        "gain": obs.get("gain", ""),
        "size": f'{h["w"]}x{h["h"]}',
        "bits": h["depth"],
        "frames": count,
        "sampled": len(rows),
        "peak%": round(peak_pct, 1),
        "ae_hl": ae_hl,
        "tgt_px": tgt,
        "tgt_frame%": round(100.0 * tgt / total, 3),
        "clip_tgt%": round(clip_tgt, 1),
        "verdict": verdict,
    }


COLS = ["file", "target", "exp_ms", "gain", "size", "bits", "frames",
        "peak%", "ae_hl", "tgt_px", "tgt_frame%", "clip_tgt%", "verdict"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folder", nargs="?", default=".")
    ap.add_argument("--frames", type=int, default=5,
                    help="frames sampled per file, spread across it (default 5)")
    ap.add_argument("--csv", help="also write the table to this CSV file")
    a = ap.parse_args()

    paths = []
    for root, _, files in os.walk(a.folder):
        paths += [os.path.join(root, n) for n in files if n.lower().endswith(".ser")]
    paths.sort()
    if not paths:
        print(f"No .ser files under {a.folder}")
        return 1

    results = []
    for p in paths:
        try:
            r = audit(p, a.frames)
        except OSError as e:
            r = {"file": p, "error": str(e)}
        results.append(r)
        if "error" in r:
            print(f'{os.path.basename(r["file"])}: {r["error"]}', file=sys.stderr)

    good = [r for r in results if "error" not in r]
    widths = {c: max(len(c), *(len(str(r[c])) for r in good)) for c in COLS} if good else {}
    if good:
        print("  ".join(c.ljust(widths[c]) for c in COLS))
        for r in good:
            print("  ".join(str(r[c]).ljust(widths[c]) for c in COLS))
        over = sum(r["verdict"] == "OVER" for r in good)
        sky = sum(r["tgt_frame%"] < 0.1 for r in good)
        print(f"\n{len(good)} files: {over} with over 1% of the target clipped; "
              f"{sky} where the target is under 0.1% of the frame "
              f"(AE's percentile lands on the sky).")
    if a.csv and good:
        with open(a.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=COLS, extrasaction="ignore")
            w.writeheader()
            w.writerows(good)
    return 0


if __name__ == "__main__":
    sys.exit(main())
