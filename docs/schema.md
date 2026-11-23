# Input and output contracts

Schema version `1.0`. Times in labels are seconds. The canonical integer clock inside each file is milliseconds, and `start_s` / `end_s` must agree with `start_ms` / `end_ms` within 1 ms. Scoring uses the millisecond values so binary floating point is not the clock.

## Limits

| Bound | Default | Error category |
| --- | --- | --- |
| Media file size | 8388608 bytes | `oversized_media` |
| Frames | 3000 | `oversized_media` |
| Frame size | 1280×720 | `oversized_media` |
| Last presentation timestamp | 120000 ms | `oversized_media` |
| In-zone buffer | 67108864 bytes | `oversized_media` |
| Label or timestamp file | 1048576 bytes | `oversized_media` |
| Events in one file | 100 | `invalid_labels` |

`--max-bytes` and `--max-frames` override the first two. Raising them is a decision about a file you already trust.

## Labels

```json
{
  "schema_version": "1.0",
  "clip_id": "proof-10s",
  "session_id": "session-eval",
  "class": "zone_intrusion",
  "time_unit": "seconds",
  "fps_nominal": 20,
  "events": [
    {
      "id": "proof-short",
      "start_s": 2.2,
      "end_s": 2.4,
      "start_ms": 2200,
      "end_ms": 2400,
      "duration_band": "short"
    }
  ]
}
```

- `class` must be `zone_intrusion`. Anything else is `unsupported_class`.
- `events` may be empty. Recall is then undefined. That is not a recall of zero.
- `end_ms` must be greater than `start_ms`.
- `duration_band` is `short` when `end_ms - start_ms < short_band_lt_ms` (1000) and `long` otherwise. A provided band that disagrees is `invalid_labels`.
- Closed intervals that share more than an endpoint are `overlapping_labels`. Touching at a single millisecond is allowed.

`session-tune` is the clip used to freeze the color threshold. `session-eval` is the staged set that `compare --dataset staged` scores. The eval set does not change the threshold.

## Timestamps

```json
{
  "schema_version": "1.0",
  "unit": "milliseconds",
  "source": "session_log",
  "pts_ms": [0, 50, null, 1500]
}
```

One entry per decoded frame. `null` is a missing presentation timestamp: the frame is decoded and counted, then skipped for motion and for the detector. Non-null values must be strictly increasing. The length must match the decoded frame count (`invalid_timestamps` otherwise).

Delay is `first_hit_ms - start_ms` using this log. The container frame rate is recorded and is not the clock when a log is present.

## Detector

`fixtures/detector.json` fixes the zone, the red test (`R >= 200`, `G <= 40`, `B <= 40`), `fraction_threshold` 0.5, `coalesce_gap_ms` 1250, `match_tolerance_ms` 0, and `motion_threshold` 20. The threshold note records that `tune-red` decodes to fraction 1 inside the labeled interval and 0 outside.

A frame is positive when the zone fraction is at least the threshold. The same config is used for every policy.

## Policies

| Policy | Selection | What the budget means |
| --- | --- | --- |
| `full_frame` | Every frame that has a timestamp | none |
| `uniform` | The frame nearest each `k / budget_hz` second, earlier index on a tie | samples per second |
| `motion` | The first timestamped frame always, and it does not consume the cap. Then at most `budget_hz` frames per second whose in-zone mean absolute difference is at least `motion_threshold`, highest difference first, earlier index on a tie | detector calls per second |

Coalesce rule: successive positive detector calls belong to one detection while the gap is at most `coalesce_gap_ms`. A negative analyzed frame, or a larger gap, starts a new detection.

Match rule: each detection matches at most one truth interval, and each truth interval matches at most one detection. A match requires a closed overlap expanded by `match_tolerance_ms` (0, so the boundary millisecond counts and nothing outside it does). Extra detections are false alarms. Extra truth intervals are misses.

Outcome, once full frame has also been scored:

- `hit` — this policy matched the event
- `detector_miss` — full frame missed it too
- `sampling_miss` — full frame hit it and this policy did not

## Report

`report.json` is schema 1.0. The fields that carry the acceptance case are, per policy:

- `decoded_frames`, `motion_checks`, `detector_calls`
- `true_positives`, `denominator`, `recall_defined`
- `short` and `long` objects with the same three ideas
- `false_alarms`, `sampling_misses`, `detector_misses`
- `missed_event_ids`, `shortest_missed_ms`, `mean_delay_ms`
- `events[]` with `id`, `outcome`, `delay_ms`, `first_hit_ms`, `first_hit_index`

Each clip also has `ground_truth`, `full_frame_recall`, `sha256`, `media`, and `labels`, so a row can be traced to the file that produced it.

`recommendation.max_misses` is the decision threshold. The chosen policy is the eligible row with the fewest detector calls. Eligibility is `sampling_misses + detector_misses <= max_misses` on the whole dataset. The reason string repeats that threshold.

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Success. Stdout is JSON. |
| 2 | Usage. Stderr is a JSON error with `category` `usage`. |
| 3 | Rejected input. `category` is one of the tokens above. |
| 4 | Unexpected failure. |
