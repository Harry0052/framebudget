# Overlap review

Access date for every source below: 2026-11-23.

FrameBudget asks one question: for a single fixed camera and a labeled set of short staged zone-intrusion proxies, how many events do full-frame inference, uniform time sampling, and a bounded motion-triggered policy miss, and how many frames were decoded versus how many times the same detector ran. The acceptance case is a 10-second clip with a 0.20-second event and a 2-second event: full-frame recall 2/2, uniform sampling at integer seconds recall 1/2, with the miss and the detector-call reduction both visible.

Decision: **proceed**. No checked project already produces that artifact on a license-compatible fixture. Do not integrate NoScope, Chameleon, or framesig as the product. Build a small offline auditor and keep the color-and-region detector replaceable.

## Capability matrix

| Project | Primary URL | Version observed | License | What it already does | Acceptance-case gap | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| NoScope | https://github.com/stanford-futuredata/noscope | commit `6c6aa72e09280530dfdcf87871c1ac43df3b6cc3` (2020-03-06); paper https://arxiv.org/abs/1703.02529 | Optimizer repo has no license file (`license` is null). Paper is CC BY-NC-ND 4.0. Inference tree `stanford-futuredata/tensorflow-noscope` is Apache-2.0, last commit `7534e5b0` (2017-09-15) | Specializes a cascade so a cheap model matches a reference neural net on a fixed camera, and reports how far the cascade stays from that reference | Matches frame-level agreement with a reference net, not labeled event recall by duration. README requires Python 2.7, TensorFlow 0.12, CUDA, and 300+ GB RAM. Example data is an external coral-reef video, which this repo will not download or redistribute | Documentation and README. Run on the shared fixture: unresolved |
| Chameleon | https://www.microsoft.com/en-us/research/publication/chameleon-video-analytics-scale-via-adaptive-configurations-cross-camera-correlations/ | SIGCOMM 2018, DOI 10.1145/3230543.3230574 | ACM copyright on the paper. No public source repository found (Microsoft Research project page lists the publication and does not link a code repo) | Adapts frame rate, resolution, and model to hold accuracy against a golden configuration while cutting resources. A fixed frame rate is an explicit baseline | Accuracy is resource-versus-detection against the golden pipeline. It does not report event-level recall split by event duration, so a short miss can disappear into an aggregate score | Paper. Run: unresolved (no code) |
| framesig | https://github.com/ferinazumaDEV/framesig and https://pypi.org/project/framesig/0.1.2/ | 0.1.2, run in this review | MIT. Depends on NumPy, PyYAML, and `opencv-python-headless` (probe pulled 5.1.0.95) | Color-and-region signatures, `--sample-fps`, JSON intervals | Finds intervals. It does not read labels, score one-to-one event recall, split short versus long, count false alarms against an empty label set, separate decoded frames from detector calls, or compare a motion budget | Observed run on the shared fixture |
| temporal_agent | https://github.com/Rikhil-Nell/temporal_agent | commit `406a445447dce59e20b5642c08990fa3027f9ae7` (2026-01-16) | No license file | YOLO plus an LLM call on state changes. README lists invocation count and event recall | Different cost unit (LLM calls), no license, and weights this repo will not ship | README only. Not run |
| sightline | https://github.com/hudasol/sightline | commit `a07264fe7a50af1f012a391f0d53000e314d7c57` (2026-10-19) | MIT | `detect_every: k` and events timed in seconds for an indoor robot | Person detection is outside this MVP (no identification, no operational alerting). It is not a three-policy audit of a staged zone proxy | README only. Not run |
| STARE | https://github.com/sii-pilab/STARE | pushed 2026-04-03 | GPL-3.0 | Latency-aware evaluation for event-camera streams | Different sensor and metric. GPL code will not be linked | README only. Not run |

Search queries used:

- `GitHub video frame sampling short event recall detector invocation benchmark`
- `video analytics adaptive sampling missed event duration safety staged`
- `GitHub event level recall latency frame sampling timestamp benchmark`
- Package search for a frame-budget event-recall CLI on PyPI

Other registry hits (framesig was the one actually run):

- `video_sampler` 0.13.0 deduplicates frames; it does not score labeled events.
- FrameProbe (https://github.com/SAY-5/frameprobe) measures real-time detector latency and deadline misses, not offline event recall.
- CoMET-Bench counts described events with a VLM frame budget. It needs model APIs and is not a local detector-call audit.
- InfoShot / SynFlash (arXiv 2603.17374) measures whether a sampler captured a sub-second flash for video QA, at a fixed 0.5 fps budget. Frame recall for a VLM is not event-interval recall, delay, or decoded-versus-analyzed work.
- RAKS-TAP (SPIE, DOI 10.1117/12.3136298) is the closest published result: fixed F2/F4/F8 schedules drop reference-event recall while a motion-aware schedule keeps it, and detector invocation rate is reported separately. It is a surveillance perimeter paper, not a reusable offline tool, and its footage is outside the staged-proxy boundary. Nothing from that paper is reused here. The transferable idea, which Chameleon also supports, is a fixed frame-rate baseline next to a motion gate.

## Shared-fixture probe

The fixture was generated locally and was not fetched from a dataset. It is a 320×240, 20 fps, 10.0 s MPEG-4 clip: gray background, an 80×80 red square at (120, 80) while `between(t,2.2,2.4)+between(t,6.0,8.0)`. SHA-256 `6862ea415cadfeca5d7dff65550e6b328b4e3b672760f0392e06b043c310ca0f`. 200 frames. Probe file lived under `/tmp` and is not part of the product fixtures (those are regenerated in P02 with frame-index painting so the 2.40 s frame is included; ffmpeg `between` left t=2.40 gray).

Held constant:

- Input: that clip.
- Expected artifact: event recall against the two labeled intervals, plus decoded-frame count and detector-call count, with the 0.20 s event missed at 1 Hz and found at full rate.
- Metric: missed events by duration band and detector invocations. Full-frame target 2/2 and 200 calls. Uniform 1 Hz target 1/2, short event missed, 10 calls.

Observed reference (OpenCV 5.1.0 headless, used only for this probe; the product dependency is system OpenCV 4, Apache-2.0):

| Policy | Decoded | Detector calls | Recall | Missed |
| --- | --- | --- | --- | --- |
| Full frame | 200 | 200 | 2/2 | none |
| Uniform at integer seconds | 200 | 10 | 1/2 | short event |

framesig 0.1.2, same file, `--no-cache`:

- `sample_fps: 20` wrote two intervals, `[2.20, 2.35]` (4 samples) and `[6.00, 8.00]` (41 samples), `meta.samples` 200. Exit 0.
- `sample_fps: 1` wrote only `[6.00, 8.00]` (3 samples), `meta.samples` 10. Exit 0.

framesig's 1 fps scan is consistent with the short miss, and `samples` is a call count. The JSON has no label file, no recall, no duration band, no false-alarm count, no undefined denominator, and no decoded-versus-analyzed split (`frame_count` is just the container length). A reviewer cannot point at a missed labeled event in that output. Wrapping framesig would still mean writing the auditor, in a stack this C++ tool should not require at runtime.

NoScope and Chameleon were not executed. NoScope's own README describes a machine this environment does not have, and the optimizer repository has no license. Chameleon has no source to build.

## Provenance path for the MVP

| Piece | Terms | How this repo will use it |
| --- | --- | --- |
| Original synthetic clips | Generated here. No third-party footage. Distributed under the repository MIT license | Committed MP4 plus JSON labels. Runtime does not download media |
| FFmpeg 6.1.1 (Ubuntu `7:6.1.1-3ubuntu5`) | Upstream LGPL-2.1+ with GPL-2+ parts; the Debian binary is GPL-2+ because the package enables GPL components (see `/usr/share/doc/ffmpeg/copyright`) | Generation tool only. The binary is not vendored. Regeneration is optional |
| H.264 via the system `libx264` | Encoder is not shipped. Clips are data | High-quality encode so the red proxy survives decode. Noted in the fixture manifest |
| OpenCV 4.6.0 (`libopencv-dev` on Ubuntu 24.04) | Apache-2.0 (verified against the OpenCV 4.10.0 `LICENSE` text and the distro package) | Runtime decoder and pixel access. Linked, not vendored |
| nlohmann/json 3.11.3 | MIT | Vendored single header for the schema |
| Neural weights, surveillance sets, coral-reef video, person-detection models | Not used | Out of scope |

## Remaining uncertainties

- OpenCV's `CAP_PROP_POS_MSEC` is not a trustworthy presentation clock. Product labels will carry an integer millisecond timestamp log, and delay will be computed from that log.
- H.264 4:2:0 shifts pure gray and pure red by a few levels. The detector threshold has to be set on the decoded pixels, on a tune session that is not the eval session.
- NoScope's unpublished license blocks any copy of that code even if a smaller machine could run it.
- Chameleon's per-video profiling cost is not reimplemented. The borrowed piece is only the fixed frame-rate baseline.

## Decision

Proceed with a separate repository. The hypothesis worth testing is the one in the playbook: a sampling audit that counts short missed events, and that refuses to treat detector-call savings as decode savings, shows a failure mode that frame accuracy and a single resource number hide. The probe already showed that failure on a synthetic clip (2/2 versus 1/2, 200 calls versus 10) while the nearest runnable tool could not report it.

Contribution instead of a new tool is not the right path. NoScope cannot be built here and does not score this label file. Chameleon has nothing to patch. framesig is a HUD scanner with a different output contract; sending it a recall patch would still leave the C++ workflow, the motion budget, and the schema checks unbuilt.
