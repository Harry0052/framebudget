# Changelog

## 0.1.0

First working release.

- Staged synthetic clips and single-class interval labels for a short event, a long event, an event-free false alarm, an edge-entry pair, a variable presentation clock, a noisy event-free clip, and a tune clip that is not part of the eval set.
- `validate`, `eval`, and `compare` commands. `compare --dataset staged --budgets 1,2,5` writes `report.json` and `report.html`.
- The same color-and-region detector runs under full-frame, uniform, and bounded motion policies. Recall is event-level, split into short and long bands. Detector calls, motion checks, and decoded frames are separate counts.
- On `proof-10s`, full frame scores 2/2 and uniform sampling at 1 Hz scores 1/2, missing `proof-short`, while both policies decode 200 frames.
