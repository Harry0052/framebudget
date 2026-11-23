# Grokbot progress

Summary: P01 PASS. Next phase is P02.

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
- **Open issues:** Product fixtures still need frame-index painting (the ffmpeg `between` expression omitted the frame at t=2.40). OpenCV presentation timestamps will not be trusted; P02 carries an integer millisecond log. Threshold must be frozen on a tune session because H.264 4:2:0 shifts levels.
- **Next phase:** P02 — repository foundation, fixture manifest, and input contracts.
