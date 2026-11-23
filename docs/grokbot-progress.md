# Grokbot progress

Summary: P01–P06 PASS. Next phase is P07 (publish the branch, wait for GitHub Actions, tag the green commit).

## P06 — Prepare a reproducible open-source release

- **Phase and status:** P06 PASS
- **Files changed:** `README.md`, `CONTRIBUTING.md`, `CHANGELOG.md`, `SECURITY.md`, `NOTICE`, `.github/workflows/ci.yml`, plus the tree those docs describe
- **Commands and results:**
  - Release build and `ctest --test-dir build --output-on-failure`: 1/1 passed, 0.22 s.
  - Demo `./build/framebudget compare --dataset staged --budgets 1,2,5 --output out/demo`: exit 0 in 0.15 s. Recommendation motion at 1 Hz, 0 misses, 15 detector calls, threshold 0.
  - ASan/UBSan `./build-san/framebudget_tests`: `ok` after pinning decode to `CAP_FFMPEG` (the gphoto probe leak is no longer on the path).
  - Workflow uses `contents: read`, pins `actions/checkout` at `11bd71901bbe5b1630ceea73d27597364c9af683` (v4.2.2) and `actions/upload-artifact` at `b4b15b8c7c6ac21ea08fcf65892d2ee8f75cf882` (v4.4.3), and runs the same install, ctest, and demo without secrets.
- **Artifact paths:** `out/demo/report.json`, `out/demo/report.html` (local). CI uploads those two files. Hosted CI itself is P07.
- **Open issues:** None in the local clean commands. Tag and public CI remain P07.
- **Next phase:** P07

## P05 — Verify correctness, security, and performance

- **Phase and status:** P05 PASS
- **Files changed:** `docs/validation.md`, `src/media.cpp` (ffmpeg backend only, decoder log silenced), `src/evaluate.cpp` (dataset paths cannot leave the fixtures root), `tests/unit.cpp`
- **Commands and results:** Recorded in `docs/validation.md`. Proof clip matches the hand schedule 2/2 versus 1/2, 200 versus 10 detector calls, both decoded 200. Event-free recall is undefined with false alarms counted, including `noisy-absent`. Variable-clock delay uses the session log. Overlap, truncated media, oversized media, and a non-`zone_intrusion` class are rejected. Demo 0.15 s against a 5 s budget. Sanitizer run passed.
- **Artifact paths:** `docs/validation.md`, `out/demo/report.json`
- **Open issues:** H.264 4:2:0 can change levels under a different encoder. The generator refuses a clip that no longer decodes to fraction 0 or 1. No offline CVE database was queried; versions are pinned and inputs are capped.
- **Next phase:** P06

## P04 — Ship the user workflow and reports

- **Phase and status:** P04 PASS
- **Files changed:** `src/cli.cpp`, `src/report.cpp`, `src/report.hpp`, `README.md`
- **Commands and results:**
  - `./build/framebudget compare --dataset staged --budgets 1,2,5 --output out/demo` exit 0. Stdout JSON `status` `complete`. Stderr table separates decoded frames, motion checks, and detector calls. HTML contains `proof-short`, `undefined`, and the limitation that this is not a protective or operational control.
  - `./build/framebudget eval --policy full --media fixtures/invalid/truncated.mp4 --labels fixtures/staged/proof-10s/labels.json` exit 3, category `unreadable_media`, actionable message, empty stdout.
  - A finding in the HTML names the event id, the policy, the labels path, and the media SHA-256.
- **Artifact paths:** `out/demo/report.json`, `out/demo/report.html`
- **Open issues:** None for the CLI workflow. There is no web UI.
- **Next phase:** P05

## P03 — Build the working core

- **Phase and status:** P03 PASS
- **Files changed:** `src/scoring.cpp`, `src/scoring.hpp`, `src/evaluate.cpp`, `src/evaluate.hpp`, `src/media.cpp`, `fixtures/staged/proof-10s/expected.json`, `tests/unit.cpp`
- **Commands and results:** `./build/framebudget_tests` printed `ok`. Proof full frame recall 2/2, 200 calls, delays 0. Uniform 1 Hz recall 1/2, missed `proof-short`, 10 calls, long-event first hit 6000 ms. Motion 1 Hz on that clip recall 2/2 with 4 calls. Multiple hits in one interval stay one true positive. An event-free observation is an undefined denominator plus a false alarm.
- **Artifact paths:** `fixtures/staged/proof-10s/expected.json`, test binary `build/framebudget_tests`
- **Open issues:** None on the proof schedule.
- **Next phase:** P04

## P02 — Create the repository and data contracts

- **Phase and status:** P02 PASS
- **Files changed:** `CMakeLists.txt`, `src/labels.cpp`, `src/sha256.cpp`, `tools/generate_fixtures.py`, `fixtures/**`, `docs/schema.md`, `third_party/nlohmann/**`, `README.md`
- **Commands and results:**
  - `python3 tools/generate_fixtures.py` wrote 6 clips. Pixel check (red fraction 0 or 1, onset not weaker than offset) passed. `proof-10s` SHA-256 `4a53d434632e7e5afb449ee5d64e62d488c5a91734be6049257744228a88f44e`.
  - `CXX=g++ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` succeeded with OpenCV 4.6.0.
  - `./build/framebudget validate --media fixtures/staged/proof-10s/clip.mp4 --labels fixtures/staged/proof-10s/labels.json --timestamps fixtures/staged/proof-10s/timestamps.json` exit 0, `status` `ok`, 200 frames, 2 events.
  - Overlap labels, truncated media, and `--max-bytes 100` exit 3 with categories `overlapping_labels`, `unreadable_media`, and `oversized_media`.
- **Artifact paths:** `fixtures/manifest.json`, `fixtures/detector.json`, `fixtures/datasets/staged.json`, `docs/schema.md`
- **Open issues:** None for the contract. Scoring of the policies is P03.
- **Next phase:** P03

## P01 — Research overlap and confirm the MVP

- **Phase and status:** P01 PASS
- **Files changed:** `docs/overlap.md`, `docs/project-contract.md`, `docs/grokbot-progress.md`
- **Commands and results:**
  - GitHub API, 2026-11-23: NoScope `stanford-futuredata/noscope` commit `6c6aa72e09280530dfdcf87871c1ac43df3b6cc3`, license null. `tensorflow-noscope` Apache-2.0, commit `7534e5b0`. temporal_agent commit `406a445447dce59e20b5642c08990fa3027f9ae7`, license null. sightline commit `a07264fe7a50af1f012a391f0d53000e314d7c57`, MIT. STARE GPL-3.0.
  - `gh search repos "chameleon video analytics frame rate"` returned no repository. Microsoft Research page lists the paper and no code link.
  - Shared fixture generated with system ffmpeg 6.1.1, SHA-256 `6862ea415cadfeca5d7dff65550e6b328b4e3b672760f0392e06b043c310ca0f`, 200 frames, 320×240, 20 fps.
  - OpenCV 5.1.0 probe (comparison runtime only): full frame recall 2/2 with 200 detector calls; uniform integer-second sampling recall 1/2 with 10 detector calls; missed band `short`. Both policies decoded 200 frames.
  - framesig 0.1.2 (MIT) `scan --no-cache`: 20 fps output has two proxy intervals and `samples: 200`; 1 fps output has only the long interval and `samples: 10`. Exit code 0. JSON has no labeled recall, duration band, false-alarm count, or decoded-versus-detector split.
  - NoScope was not run. README requires Python 2.7, TensorFlow 0.12, CUDA, and 300+ GB RAM, and the optimizer repo has no license.
- **Artifact paths:** `docs/overlap.md`, `docs/project-contract.md`. Probe media and framesig JSON were left in `/tmp/fb-p01` and are not repository fixtures.
- **Open issues:** Closed in later phases. Product fixtures use frame-index painting. Delay uses the millisecond log. The threshold is frozen on `session-tune`.
- **Next phase:** P02
