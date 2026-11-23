# FrameBudget

FrameBudget compares frame budgets against short staged events. It answers a narrow question for one fixed camera: if the same color-and-region detector runs on every frame, on a uniform clock, or only after a bounded motion trigger, which labeled zone intrusions does each policy miss, and how many frames were decoded versus how many times the detector ran.

The clips in this repository are synthetic. A red square stands in for a foam parcel entering a marked empty zone. The report describes that clip. It is not a detector of real emergencies, and it is not a protective or operational control.

## Install

Tested on Ubuntu 24.04 with g++ 13 and OpenCV 4.6.

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libopencv-dev libavutil-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

If `c++` is not g++, configure with `CXX=g++`. No network access is required after those packages are installed. The demo does not download weights or video.

Optional: address and undefined-behavior sanitizers.

```sh
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DFRAMEBUDGET_SANITIZERS=ON
cmake --build build-san -j
ctest --test-dir build-san --output-on-failure
```

## Demo

From the repository root:

```sh
./build/framebudget compare --dataset staged --budgets 1,2,5 --output out/demo
```

Stdout is a JSON summary. The table and the recommendation are on stderr. The files to read are:

- `out/demo/report.json`
- `out/demo/report.html`

On the committed `proof-10s` clip (10 seconds, 20 fps, zone at x=120 y=80 w=80 h=80):

| Policy | Decoded frames | Detector calls | Recall | Missed |
| --- | --- | --- | --- | --- |
| Full frame | 200 | 200 | 2/2 | none |
| Uniform at 1 Hz | 200 | 10 | 1/2 | `proof-short` (0.20 s, a sampling miss) |

The long event is 6.00–8.00 s and is found by both. The short event is 2.20–2.40 s. Sampling at integer seconds does not land inside it. Both policies still decode all 200 frames. The call reduction is not a decode reduction.

Across the staged eval set the recommendation at a decision threshold of **0 observed misses** is **motion at 1 Hz** (15 detector calls, 0 misses). Uniform at 1 Hz misses three short events. Uniform at 5 Hz also has 0 misses and uses more detector calls. Full-frame recall on the set is 6/6, with 2 false alarms on the event-free clips. Those clips have an undefined recall denominator, not a recall of zero. The HTML report states the threshold next to the recommendation.

Other checks the same command covers:

- `edge-entry` hits the first frame and the last frame.
- `variable-timing` times `var-long` from the session log (frame 7 at 2500 ms), not from index divided by container fps. One frame has no timestamp.
- `absent-false` and `noisy-absent` contribute false alarms and no events. The noisy clip stays in that count.

`tune-red` is `session-tune`. It is how the color threshold 0.5 was chosen (decoded fraction 1 inside the interval, 0 outside) and it is not part of `--dataset staged`.

## Other commands

```sh
./build/framebudget validate \
  --media fixtures/staged/proof-10s/clip.mp4 \
  --labels fixtures/staged/proof-10s/labels.json \
  --timestamps fixtures/staged/proof-10s/timestamps.json

./build/framebudget eval \
  --media fixtures/staged/proof-10s/clip.mp4 \
  --labels fixtures/staged/proof-10s/labels.json \
  --timestamps fixtures/staged/proof-10s/timestamps.json \
  --policy uniform --budget 1 --output out/eval
```

`validate` exits 0 for that clip and prints `status: ok`. It exits 3 for `fixtures/invalid/overlap` (`overlapping_labels`), `fixtures/invalid/truncated.mp4` (`unreadable_media`), and a real clip passed with `--max-bytes 100` (`oversized_media`). The message names the category and what to change.

`--score-trace` writes `scores.jsonl` under `--output` for a later comparison against another detector. This release does not ship a neural model. Replacing the color test is future work; the sampler and the scorer stay the ones measured here.

Exit codes: 0 success, 2 usage, 3 rejected input, 4 unexpected failure. Contract details are in [docs/schema.md](docs/schema.md).

## What the policies are

- **Full frame** runs the detector on every timestamped frame. That column is the detector reference.
- **Uniform** picks the frame nearest each `k / budget` second.
- **Motion** always scores the first timestamped frame, then at most `budget` frames per second whose in-zone change clears the motion threshold.

A miss that full frame also misses is a detector miss. A miss that only the sampled policy misses is a sampling miss. Contiguous hits inside one labeled interval are one true positive.

## Fixtures and licenses

Clips are original synthetic media produced by `tools/generate_fixtures.py` (seed 19001) and checked into the repo. Terms, checksums, and the encoder line are in `fixtures/manifest.json`. Regenerating them needs `ffmpeg` on `PATH` and is optional for the demo.

Code is MIT. See [LICENSE](LICENSE), [NOTICE](NOTICE), and [CHANGELOG.md](CHANGELOG.md). OpenCV is Apache-2.0. nlohmann/json 3.11.3 is MIT and vendored. libavutil is LGPL-2.1-or-later and is linked dynamically. The Ubuntu ffmpeg used to encode the clips is a GPL build and is not shipped.

To contribute, see [CONTRIBUTING.md](CONTRIBUTING.md). To report a vulnerability, see [SECURITY.md](SECURITY.md).
