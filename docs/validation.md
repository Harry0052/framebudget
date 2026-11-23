# Validation

Recorded on 2026-11-23. The checks below are measurements on the synthetic fixtures in this repository, not on an external dataset and not on a live camera.

## Host

- Linux 6.12.113+, 4 CPUs
- g++ 13.3.0, CMake 3.28.3
- OpenCV 4.6.0 (`libopencv-dev`), video backend `FFMPEG` as reported by the tool
- libavutil 58.29.100, dynamically linked
- Fixture tree: 29 files, 410805 bytes

## Commands

```sh
CXX=g++ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/framebudget compare --dataset staged --budgets 1,2,5 --output out/demo
CXX=g++ cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DFRAMEBUDGET_SANITIZERS=ON
cmake --build build-san -j
./build-san/framebudget_tests
```

`ctest` on the release build: 1/1 passed in 0.22 s. The ASan/UBSan binary printed `ok` after `VideoCapture` was pinned to `CAP_FFMPEG`. An earlier sanitizer run leaked inside OpenCV's gphoto camera probe; that probe is not used now.

One `compare` of the staged set took 0.15 s. The performance budget for this demo is 5 seconds. 0.15 s is inside it.

## Acceptance evidence

Clip `proof-10s`, SHA-256 `4a53d434632e7e5afb449ee5d64e62d488c5a91734be6049257744228a88f44e`. These numbers match `fixtures/staged/proof-10s/expected.json`, which is the frame-index schedule (200 frames, short frames 44–48, long frames 120–160, uniform 1 Hz indices 0,20,…,180), not a copy of an earlier program dump.

| Policy | Decoded | Detector calls | Recall | Misses | False alarms |
| --- | --- | --- | --- | --- | --- |
| full frame | 200 | 200 | 2/2 | none | 0 |
| uniform 1 Hz | 200 | 10 | 1/2 | `proof-short` (sampling miss) | 0 |
| motion 1 Hz | 200 | 4 | 2/2 | none | 0 |

Short delay and long delay on full frame are 0 ms. The long event's first uniform hit is at 6000 ms. Continuous frames inside one event contribute one true positive. A unit test also covers a negative frame splitting a run so the second run is a false alarm rather than a second true positive, and one detection interval covering two events counting as a single match.

`absent-false` full frame: denominator 0, `recall_defined` false, 1 false alarm. `noisy-absent` is in the same staged aggregate, so its false alarm stays in the full-frame false-alarm total (2 across the set) instead of being dropped as a noisy scene.

`variable-timing` full frame hits `var-long` at frame index 7, presentation time 2500 ms. Index 7 at the container 20 fps would be 350 ms. The report uses the session log. One frame in that clip has a null timestamp and is counted in `frames_missing_timestamp`.

`edge-entry` includes a hit at 0 ms and a hit at the last frame, 9950 ms.

Staged aggregate, decision threshold 0 misses:

- full frame: 6/6, 815 detector calls, 2 false alarms, 816 decoded frames
- uniform 1 Hz: 3 sampling misses (`proof-short`, `edge-end`, `var-short`), 47 calls
- uniform 5 Hz: 0 misses, 214 calls
- motion 1 Hz: 0 misses, 15 calls, and that is the recommendation

The recommendation text states the threshold of 0. Decoded frames are not reduced. The HTML report names `proof-short`, prints `undefined` for the event-free denominator, and states that the result is not a protective or operational control.

## Rejected inputs

| Command | Exit | Category |
| --- | --- | --- |
| `validate` on `fixtures/invalid/overlap` | 3 | `overlapping_labels` |
| `validate` on `fixtures/invalid/truncated.mp4` | 3 | `unreadable_media` |
| `validate --max-bytes 100` on `proof-10s` | 3 | `oversized_media` |
| labels with `class` `person` | 3 | `unsupported_class` |
| `eval` of the truncated file | 3 | `unreadable_media` |
| dataset path containing `..` | thrown | `invalid_labels` |

Stderr for those CLI cases is a JSON object with `status`, `category`, and `message`. Stdout is empty. A successful `validate` prints `status` `ok` on stdout.

## Trust boundary

- Media, labels, timestamps, and the dataset file are local inputs. The dataset loader rejects absolute paths and `..`.
- Decode uses the ffmpeg file backend only. Camera backends are not opened.
- Writes for `compare` are `report.json`, `report.html`, and, with `--score-trace`, `scores.jsonl`, all as single file names under `--output`.
- There is no network call, no shell-out, and no model file on the eval path.
- Label size, frame count, pixel size, duration, and the in-zone buffer are capped.
- nlohmann/json 3.11.3 is vendored (MIT). OpenCV and libavutil come from the distro packages (Apache-2.0 and LGPL-2.1-or-later). No advisory scan beyond pinning these versions was available offline; the input caps are the mitigation for untrusted documents.
- The color threshold is the value frozen on `session-tune`. `compare --dataset staged` does not include `tune-red`.

## Residual limits

- H.264 4:2:0 changes the stored levels. The red test is wide enough that the committed clips decode to fraction 0 or 1, which the generator checks. A different encoder could break that. Regenerate and re-run the pixel check rather than loosening the test.
- Uniform and motion still decode every frame. The report says so. Call-count savings are not decode savings.
- This is one zone, one class, and one synthetic camera. It is not a measurement of a safety system.
