#!/usr/bin/env python3
"""Generate the staged FrameBudget fixtures.

The pixels are original and deterministic. FFmpeg is used only as an encoder
and, for the self-check, as a decoder. Nothing is downloaded.
"""

from __future__ import annotations

import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIX = ROOT / "fixtures"
SEED = 19001
WIDTH = 320
HEIGHT = 240
FPS = 20
ZONE = (120, 80, 80, 80)  # x, y, w, h
BG = (128, 128, 128)  # B, G, R
RED = (0, 0, 255)


def lcg(seed: int):
    state = seed & 0x7FFFFFFF

    def nxt() -> int:
        nonlocal state
        state = (1103515245 * state + 12345) & 0x7FFFFFFF
        return state

    return nxt


def blank() -> bytearray:
    return bytearray(BG * (WIDTH * HEIGHT))


def paint_zone(buf: bytearray, color: tuple[int, int, int], jitter: int = 0, rng=None) -> None:
    x, y, w, h = ZONE
    b, g, r = color
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            i = (yy * WIDTH + xx) * 3
            bb, gg, rr = b, g, r
            if jitter and rng is not None:
                bb = min(255, max(0, bb + (rng() % (2 * jitter + 1)) - jitter))
                gg = min(255, max(0, gg + (rng() % (2 * jitter + 1)) - jitter))
                rr = min(255, max(0, rr + (rng() % (2 * jitter + 1)) - jitter))
            buf[i : i + 3] = bytes((bb, gg, rr))


def encode(frames: list[bytearray], dest: Path) -> None:
    raw = b"".join(frames)
    cmd = [
        "ffmpeg",
        "-y",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "bgr24",
        "-s",
        f"{WIDTH}x{HEIGHT}",
        "-r",
        str(FPS),
        "-i",
        "pipe:0",
        "-c:v",
        "libx264",
        "-qp",
        "0",
        "-pix_fmt",
        "yuv420p",
        "-movflags",
        "+faststart",
        str(dest),
    ]
    subprocess.run(cmd, input=raw, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def decode(path: Path) -> list[bytes]:
    cmd = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        str(path),
        "-f",
        "rawvideo",
        "-pix_fmt",
        "bgr24",
        "pipe:1",
    ]
    proc = subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    frame_bytes = WIDTH * HEIGHT * 3
    if len(proc.stdout) % frame_bytes != 0:
        raise SystemExit(f"{path} decoded to a partial frame")
    count = len(proc.stdout) // frame_bytes
    return [proc.stdout[i * frame_bytes : (i + 1) * frame_bytes] for i in range(count)]


def zone_pixels(frame: bytes) -> list[tuple[int, int, int]]:
    x, y, w, h = ZONE
    out = []
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            i = (yy * WIDTH + xx) * 3
            out.append((frame[i], frame[i + 1], frame[i + 2]))
    return out


def red_fraction(frame: bytes) -> float:
    hits = 0
    pix = zone_pixels(frame)
    for b, g, r in pix:
        if r >= 200 and g <= 40 and b <= 40:
            hits += 1
    return hits / len(pix)


def mad(a: bytes, b: bytes) -> float:
    pa, pb = zone_pixels(a), zone_pixels(b)
    total = 0
    for (b1, g1, r1), (b2, g2, r2) in zip(pa, pb):
        total += abs(b1 - b2) + abs(g1 - g2) + abs(r1 - r2)
    return total / (len(pa) * 3)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def labels(clip_id: str, session: str, events: list[dict], fps: float = FPS) -> dict:
    return {
        "schema_version": "1.0",
        "clip_id": clip_id,
        "session_id": session,
        "class": "zone_intrusion",
        "time_unit": "seconds",
        "fps_nominal": fps,
        "events": events,
    }


def event(eid: str, start_ms: int, end_ms: int) -> dict:
    duration = end_ms - start_ms
    band = "short" if duration < 1000 else "long"
    return {
        "id": eid,
        "start_s": start_ms / 1000.0,
        "end_s": end_ms / 1000.0,
        "start_ms": start_ms,
        "end_ms": end_ms,
        "duration_band": band,
    }


def timestamps(pts_ms: list[int | None], source: str = "session_log") -> dict:
    return {"schema_version": "1.0", "unit": "milliseconds", "source": source, "pts_ms": pts_ms}


def make_clip(n: int, red_ranges: list[tuple[int, int]], noise: bool, seed: int) -> list[bytearray]:
    """red_ranges are inclusive frame index ranges."""
    red = set()
    for a, b in red_ranges:
        red.update(range(a, b + 1))
    protected = set(red)
    for a, b in red_ranges:
        if a - 1 >= 0:
            protected.add(a - 1)
        if b + 1 < n:
            protected.add(b + 1)
    rng = lcg(seed)
    frames = []
    for i in range(n):
        buf = blank()
        if i in red:
            paint_zone(buf, RED)
        elif noise and i not in protected:
            paint_zone(buf, BG, jitter=1, rng=rng)
        frames.append(buf)
    return frames


def check_clip(name: str, path: Path, n: int, red_ranges: list[tuple[int, int]]) -> None:
    frames = decode(path)
    if len(frames) != n:
        raise SystemExit(f"{name}: expected {n} frames, decoded {len(frames)}")
    red = set()
    for a, b in red_ranges:
        red.update(range(a, b + 1))
    for i, frame in enumerate(frames):
        frac = red_fraction(frame)
        if i in red:
            if frac != 1.0:
                raise SystemExit(f"{name}: frame {i} red fraction {frac}, expected 1")
        elif frac != 0.0:
            raise SystemExit(f"{name}: frame {i} red fraction {frac}, expected 0")
    for a, b in red_ranges:
        if a - 1 >= 0:
            onset = mad(frames[a - 1], frames[a])
            if onset < 50:
                raise SystemExit(f"{name}: weak onset at {a}: {onset}")
        if b + 1 < n:
            onset = mad(frames[a - 1], frames[a]) if a - 1 >= 0 else None
            offset = mad(frames[b], frames[b + 1])
            if offset < 50:
                raise SystemExit(f"{name}: weak offset at {b + 1}: {offset}")
            if onset is not None and offset > onset + 0.05:
                raise SystemExit(f"{name}: offset {offset} exceeds onset {onset} at [{a},{b}]")
    if noise_name(name):
        for i in range(1, n):
            if i in red or (i - 1) in red:
                continue
            delta = mad(frames[i - 1], frames[i])
            if delta >= 20:
                raise SystemExit(f"{name}: noise MAD {delta} at frame {i} crosses the motion threshold")


def noise_name(name: str) -> bool:
    return name == "noisy-absent"


def cfr_pts(n: int) -> list[int]:
    return [i * (1000 // FPS) for i in range(n)]


def main() -> None:
    if shutil.which("ffmpeg") is None:
        raise SystemExit("ffmpeg is required to regenerate fixtures")
    staged = FIX / "staged"
    invalid = FIX / "invalid"
    if staged.exists():
        shutil.rmtree(staged)
    if invalid.exists():
        shutil.rmtree(invalid)

    specs = []

    def add(name: str, session: str, n: int, red_ranges, events, pts, noise=False, role="eval"):
        frames = make_clip(n, red_ranges, noise, SEED + len(specs) * 17)
        folder = staged / name
        folder.mkdir(parents=True, exist_ok=True)
        media = folder / "clip.mp4"
        encode(frames, media)
        check_clip(name, media, n, red_ranges)
        write_json(folder / "labels.json", labels(name, session, events))
        write_json(folder / "timestamps.json", timestamps(pts))
        specs.append(
            {
                "id": name,
                "session_id": session,
                "role": role,
                "frames": n,
                "red_inclusive_ranges": red_ranges,
                "sha256": sha256(media),
                "bytes": media.stat().st_size,
                "media": f"staged/{name}/clip.mp4",
                "labels": f"staged/{name}/labels.json",
                "timestamps": f"staged/{name}/timestamps.json",
            }
        )

    add(
        "proof-10s",
        "session-eval",
        200,
        [(44, 48), (120, 160)],
        [event("proof-short", 2200, 2400), event("proof-long", 6000, 8000)],
        cfr_pts(200),
    )
    add(
        "absent-false",
        "session-eval",
        200,
        [(64, 68)],
        [],
        cfr_pts(200),
    )
    add(
        "edge-entry",
        "session-eval",
        200,
        [(0, 6), (194, 199)],
        [event("edge-start", 0, 300), event("edge-end", 9700, 9950)],
        cfr_pts(200),
    )
    # Short and long hits are more than coalesce_gap_ms (1250) apart so a
    # motion trigger on each onset stays two detections. Index 5 has no
    # presentation timestamp. Container fps is still 20; the log is the clock.
    var_pts = [0, 50, 100, 400, 450, None, 1500, 2500, 2550, 3000, 3500, 4000, 4600, 5200, 6000, 7000]
    add(
        "variable-timing",
        "session-eval",
        16,
        [(2, 2), (7, 11)],
        [event("var-short", 100, 200), event("var-long", 2500, 4000)],
        var_pts,
    )
    add(
        "noisy-absent",
        "session-eval",
        200,
        [(64, 68)],
        [],
        cfr_pts(200),
        noise=True,
    )
    add(
        "tune-red",
        "session-tune",
        200,
        [(20, 80)],
        [event("tune-long", 1000, 4000)],
        cfr_pts(200),
        role="tune",
    )

    # Invalid fixtures. The overlap clip itself decodes; the labels are the fault.
    tiny_dir = invalid / "overlap"
    tiny_dir.mkdir(parents=True)
    tiny_frames = make_clip(20, [], False, SEED)
    encode(tiny_frames, tiny_dir / "clip.mp4")
    check_clip("overlap-media", tiny_dir / "clip.mp4", 20, [])
    write_json(
        tiny_dir / "labels.json",
        labels(
            "overlap",
            "session-eval",
            [event("over-a", 100, 500), event("over-b", 400, 800)],
        ),
    )
    write_json(tiny_dir / "timestamps.json", timestamps(cfr_pts(20)))

    bad = invalid / "bad-order"
    bad.mkdir(parents=True)
    write_json(
        bad / "labels.json",
        {
            "schema_version": "1.0",
            "clip_id": "bad-order",
            "session_id": "session-eval",
            "class": "zone_intrusion",
            "time_unit": "seconds",
            "fps_nominal": 20,
            "events": [
                {
                    "id": "backwards",
                    "start_s": 2.0,
                    "end_s": 1.0,
                    "start_ms": 2000,
                    "end_ms": 1000,
                    "duration_band": "long",
                }
            ],
        },
    )

    bad_class = invalid / "bad-class"
    bad_class.mkdir(parents=True)
    write_json(
        bad_class / "labels.json",
        {
            "schema_version": "1.0",
            "clip_id": "bad-class",
            "session_id": "session-eval",
            "class": "person",
            "time_unit": "seconds",
            "fps_nominal": 20,
            "events": [],
        },
    )

    (invalid / "truncated.mp4").write_bytes(b"\x00\x00\x00\x18ftypmp42not-a-video")

    # Authored arithmetic oracle for the proof clip. These counts are the
    # frame-index schedule, not a measurement copied from the C++ scorer.
    write_json(
        staged / "proof-10s" / "expected.json",
        {
            "note": "Hand schedule. 200 frames at 50 ms. Short frames 44-48. Long frames 120-160. Uniform 1 Hz selects indices 0,20,...,180.",
            "full_frame": {
                "decoded_frames": 200,
                "detector_calls": 200,
                "motion_checks": 0,
                "true_positives": 2,
                "denominator": 2,
                "false_alarms": 0,
                "missed_event_ids": [],
                "short_true_positives": 1,
                "long_true_positives": 1,
                "proof_short_delay_ms": 0,
                "proof_long_delay_ms": 0,
            },
            "uniform_1": {
                "decoded_frames": 200,
                "detector_calls": 10,
                "true_positives": 1,
                "denominator": 2,
                "false_alarms": 0,
                "missed_event_ids": ["proof-short"],
                "proof_long_delay_ms": 0,
                "proof_long_first_hit_ms": 6000,
            },
        },
    )

    eval_ids = [s["id"] for s in specs if s["role"] == "eval"]
    tune_ids = [s["id"] for s in specs if s["role"] == "tune"]

    def dataset(name: str, ids: list[str]) -> None:
        clips = []
        for spec in specs:
            if spec["id"] not in ids:
                continue
            clips.append(
                {
                    "id": spec["id"],
                    "media": spec["media"],
                    "labels": spec["labels"],
                    "timestamps": spec["timestamps"],
                }
            )
        write_json(
            FIX / "datasets" / f"{name}.json",
            {"schema_version": "1.0", "name": name, "clips": clips},
        )

    dataset("staged", eval_ids)
    dataset("tune", tune_ids)

    ffmpeg_line = subprocess.run(["ffmpeg", "-version"], check=True, capture_output=True, text=True).stdout.splitlines()[0]
    write_json(
        FIX / "manifest.json",
        {
            "schema_version": "1.0",
            "generator": "tools/generate_fixtures.py",
            "seed": SEED,
            "ffmpeg": ffmpeg_line,
            "encode": "libx264 QP 0, yuv420p, +faststart, container mp4",
            "license": "Original synthetic media created for this repository. No third-party footage or model weights. The clips are distributed under the repository MIT license. FFmpeg and libx264 are generation tools and are not vendored; the Ubuntu ffmpeg package is GPL-2+ because that build enables GPL components. OpenCV, used at runtime to decode, is Apache-2.0.",
            "zone": {"x": 120, "y": 80, "w": 80, "h": 80},
            "geometry": {"width": WIDTH, "height": HEIGHT, "nominal_fps": FPS},
            "clips": specs,
            "invalid": [
                {
                    "id": "overlap",
                    "media": "invalid/overlap/clip.mp4",
                    "labels": "invalid/overlap/labels.json",
                    "sha256": sha256(invalid / "overlap" / "clip.mp4"),
                    "fault": "overlapping zone_intrusion intervals",
                },
                {
                    "id": "bad-order",
                    "labels": "invalid/bad-order/labels.json",
                    "fault": "end_ms before start_ms",
                },
                {
                    "id": "bad-class",
                    "labels": "invalid/bad-class/labels.json",
                    "fault": "class is not zone_intrusion",
                },
                {
                    "id": "truncated",
                    "media": "invalid/truncated.mp4",
                    "sha256": sha256(invalid / "truncated.mp4"),
                    "fault": "not a readable video",
                },
            ],
        },
    )
    write_json(
        FIX / "detector.json",
        {
            "schema_version": "1.0",
            "zone": {"x": 120, "y": 80, "w": 80, "h": 80},
            "color": {"r_min": 200, "g_max": 40, "b_max": 40},
            "fraction_threshold": 0.5,
            "threshold_session": "session-tune",
            "threshold_note": "Chosen on tune-red, where the decoded in-zone fraction is 1 during the labeled interval and 0 outside, then frozen. session-eval clips do not move this number.",
            "coalesce_gap_ms": 1250,
            "match_tolerance_ms": 0,
            "short_band_lt_ms": 1000,
            "motion_threshold": 20.0,
            "zone_buffer_bytes": 67108864,
        },
    )
    print(f"wrote {len(specs)} clips under {FIX}")
    for spec in specs:
        print(f"  {spec['id']} {spec['bytes']} bytes {spec['sha256'][:12]}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        sys.exit(f"ffmpeg failed: {exc}")
