# Project contract

Status: accepted for implementation after the P01 overlap review (`docs/overlap.md`). Decision there is **proceed**.

## User and task

The user is a computer-vision engineer measuring what frame sampling misses in an offline evaluation of staged safety-event proxies.

The task this release has to finish:

1. Load one fixed-camera MP4, a single-class interval label file, and a presentation-timestamp log.
2. Run one color-and-region detector, unchanged, under three policies: every frame, uniform time sampling, and a motion-triggered policy capped at the same numeric budget.
3. Write a report with event-level recall (overall and by short/long duration), false-alarm count, detection delay from presentation timestamps, decoded frames, motion checks, and detector calls.
4. On the designated proof clip, show full-frame recall 2/2 and uniform 1 Hz recall 1/2, name the missed short event, and show the detector-call reduction. Do not describe that reduction as fewer decoded frames.

## What this release is not

- Not a detector of real emergencies, and not a protective or operational control.
- Not live alerting, identification, tracking of people, or public-surveillance analytics.
- A synthetic or staged clip only demonstrates metric behavior on that clip.
- No neural weights ship in the default path. An optional score trace can be reviewed later if someone compares a heavier model under the same sampler.

## Minimum dependencies

Runtime, all free and local:

- C++17 compiler (tested with g++ 13.3.0)
- CMake 3.16 or newer (tested with 3.28.3)
- OpenCV 4 with `core`, `imgproc`, and `videoio` (tested with Ubuntu 24.04 `libopencv-dev` 4.6.0, Apache-2.0)

Build-time only, for regenerating fixtures:

- FFmpeg on `PATH` (generation used Ubuntu ffmpeg 6.1.1). Not required to run the committed demo.

Not required: network access at runtime, a GPU, a paid API, or a package download during `compare`.

## Measurable acceptance case

Clip `proof-10s` (session `session-eval`):

- 10 seconds, 20 fps, 320×240.
- Marked zone rectangle `(x=120, y=80, w=80, h=80)`.
- Colored proxy present on frames whose presentation time lies in **[2.20 s, 2.40 s]** (short, 0.20 s) and **[6.00 s, 8.00 s]** (long, 2.00 s).
- Same detector threshold on every policy. Threshold is chosen on `session-tune` and then frozen.
- Full-frame scoring finds both events (recall 2/2), one true positive per event no matter how many frames hit.
- Uniform sampling at the integer timestamps 0,1,...,9 finds the long event and misses the short one (recall 1/2).
- The report names that missed event, its duration band, and the detector-call counts (200 versus 10 on this clip). Decoded frames stay 200 for both policies in this implementation, and the report says so.

Further checks the same scorer must pass before the release is called done:

- An event-free clip reports any false detections and an undefined recall denominator, not zero recall.
- Delay uses presentation timestamps and survives variable frame spacing, inclusive boundary hits, and a missing timestamp.
- Unreadable media, oversized media, and invalid or overlapping labels are rejected with a category and an actionable message.

## Policies

| Policy | What is selected | Budget flag |
| --- | --- | --- |
| `full_frame` | Every timestamped frame | none |
| `uniform` | Frame nearest each `k / budget_hz` second, ties toward the earlier frame | 1, 2, 5 in the demo |
| `motion` | First timestamped frame always, then at most `budget_hz` frames per second whose in-zone mean absolute difference meets the motion threshold, highest motion first, earlier frame on a tie | same numbers |

Motion checks and detector calls are separate counters. A frame that is only differenced is not a detector call.

## Recommendation rule

The demo recommendation is the policy and budget with the fewest detector calls among those whose missed-event count on the staged eval set is at or below a declared threshold. The default threshold is **0 observed misses**. The report states that threshold next to the recommendation. It is not a claim about any camera outside the staged set.
