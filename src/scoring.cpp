// SPDX-License-Identifier: MIT
#include "scoring.hpp"

#include "error.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace fb {
namespace {

std::vector<Detection> coalesce(const std::vector<FrameObs>& ran, int gap_ms) {
  std::vector<Detection> out;
  Detection current;
  bool open = false;
  int previous_pts = 0;
  for (const FrameObs& frame : ran) {
    if (frame.positive) {
      const bool split = open && frame.pts_ms - previous_pts > gap_ms;
      if (!open || split) {
        if (open) out.push_back(current);
        current = Detection{};
        current.start_ms = frame.pts_ms;
        current.end_ms = frame.pts_ms;
        current.frame_indices = {frame.index};
        current.frame_pts = {frame.pts_ms};
        open = true;
      } else {
        current.end_ms = frame.pts_ms;
        current.frame_indices.push_back(frame.index);
        current.frame_pts.push_back(frame.pts_ms);
      }
      previous_pts = frame.pts_ms;
    } else if (open) {
      out.push_back(current);
      open = false;
    }
  }
  if (open) out.push_back(current);
  return out;
}

bool overlaps(const Detection& detection, const Event& event, int tolerance_ms) {
  const int start = detection.start_ms - tolerance_ms;
  const int end = detection.end_ms + tolerance_ms;
  return start <= event.end_ms && event.start_ms <= end;
}

}  // namespace

std::vector<char> uniform_mask(const std::vector<int>& pts_ms, int budget_hz) {
  if (budget_hz < 1) throw Error("usage", "uniform budget must be a positive integer number of samples per second.");
  std::vector<char> mask(pts_ms.size(), 0);
  int last = -1;
  for (int pts : pts_ms) last = std::max(last, pts);
  if (last < 0) return mask;
  int period = 1000 / budget_hz;
  if (period < 1) period = 1;
  if (1000 % budget_hz != 0) {
    period = static_cast<int>(std::llround(1000.0 / static_cast<double>(budget_hz)));
    if (period < 1) period = 1;
  }
  for (int target = 0; target <= last; target += period) {
    int best = -1;
    int best_dist = 0;
    for (int i = 0; i < static_cast<int>(pts_ms.size()); ++i) {
      if (pts_ms[static_cast<std::size_t>(i)] < 0) continue;
      const int dist = std::abs(pts_ms[static_cast<std::size_t>(i)] - target);
      if (best < 0 || dist < best_dist || (dist == best_dist && i < best)) {
        best = i;
        best_dist = dist;
      }
    }
    if (best >= 0) mask[static_cast<std::size_t>(best)] = 1;
  }
  return mask;
}

std::vector<int> select_motion(const std::vector<MotionObs>& frames, int budget_hz, double threshold) {
  if (budget_hz < 1) throw Error("usage", "motion budget must be a positive integer cap on detector calls per second.");
  std::vector<int> chosen;
  bool forced = false;
  struct Item {
    int index;
    double motion;
  };
  std::map<int, std::vector<Item>> bins;
  for (const MotionObs& frame : frames) {
    if (frame.pts_ms < 0) continue;
    if (!forced) {
      chosen.push_back(frame.index);
      forced = true;
      continue;
    }
    if (!frame.has_motion || frame.motion + 1e-9 < threshold) continue;
    bins[frame.pts_ms / 1000].push_back(Item{frame.index, frame.motion});
  }
  for (auto& entry : bins) {
    auto& items = entry.second;
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
      if (std::abs(a.motion - b.motion) > 1e-6) return a.motion > b.motion;
      return a.index < b.index;
    });
    if (static_cast<int>(items.size()) > budget_hz) items.resize(static_cast<std::size_t>(budget_hz));
    for (const Item& item : items) chosen.push_back(item.index);
  }
  std::sort(chosen.begin(), chosen.end());
  chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());
  return chosen;
}

PolicyScore score_observations(const LabelSet& labels, const std::vector<FrameObs>& ran, const std::string& policy,
                               std::optional<int> budget_hz, int decoded_frames, int decode_passes,
                               int frames_missing_timestamp, int motion_checks, int detector_calls, int gap_ms,
                               int tolerance_ms) {
  std::vector<FrameObs> ordered = ran;
  std::sort(ordered.begin(), ordered.end(), [](const FrameObs& a, const FrameObs& b) {
    if (a.pts_ms != b.pts_ms) return a.pts_ms < b.pts_ms;
    return a.index < b.index;
  });

  PolicyScore score;
  score.policy = policy;
  score.budget_hz = budget_hz;
  score.decoded_frames = decoded_frames;
  score.decode_passes = decode_passes;
  score.frames_missing_timestamp = frames_missing_timestamp;
  score.motion_checks = motion_checks;
  score.detector_calls = detector_calls;
  score.detections = coalesce(ordered, gap_ms);
  score.denominator = static_cast<int>(labels.events.size());
  score.recall_defined = score.denominator > 0;

  std::vector<char> used(score.detections.size(), 0);
  double delay_sum = 0;
  int delay_count = 0;
  for (const Event& event : labels.events) {
    EventScore row;
    row.id = event.id;
    row.band = event.band;
    row.start_ms = event.start_ms;
    row.end_ms = event.end_ms;
    row.duration_ms = event.duration_ms();
    if (event.band == "short") score.short_den += 1;
    else score.long_den += 1;

    int best = -1;
    for (int i = 0; i < static_cast<int>(score.detections.size()); ++i) {
      if (used[static_cast<std::size_t>(i)]) continue;
      if (!overlaps(score.detections[static_cast<std::size_t>(i)], event, tolerance_ms)) continue;
      if (best < 0 || score.detections[static_cast<std::size_t>(i)].start_ms <
                          score.detections[static_cast<std::size_t>(best)].start_ms) {
        best = i;
      }
    }
    if (best >= 0) {
      used[static_cast<std::size_t>(best)] = 1;
      const Detection& detection = score.detections[static_cast<std::size_t>(best)];
      int hit_pts = detection.frame_pts.front();
      int hit_index = detection.frame_indices.front();
      for (std::size_t k = 0; k < detection.frame_pts.size(); ++k) {
        const int pts = detection.frame_pts[k];
        if (pts >= event.start_ms - tolerance_ms && pts <= event.end_ms + tolerance_ms) {
          hit_pts = pts;
          hit_index = detection.frame_indices[k];
          break;
        }
      }
      row.matched = true;
      row.outcome = "hit";
      row.delay_ms = hit_pts - event.start_ms;
      row.first_hit_ms = hit_pts;
      row.first_hit_index = hit_index;
      score.true_positives += 1;
      if (event.band == "short") score.short_tp += 1;
      else score.long_tp += 1;
      delay_sum += static_cast<double>(*row.delay_ms);
      delay_count += 1;
    } else {
      row.matched = false;
      row.outcome = "sampling_miss";
      score.missed_event_ids.push_back(event.id);
      if (!score.shortest_missed_ms || event.duration_ms() < *score.shortest_missed_ms) {
        score.shortest_missed_ms = event.duration_ms();
      }
    }
    score.events.push_back(row);
  }
  for (char flag : used) {
    if (!flag) score.false_alarms += 1;
  }
  if (delay_count > 0) score.mean_delay_ms = delay_sum / static_cast<double>(delay_count);
  return score;
}

void annotate_outcomes(std::vector<PolicyScore>& policies) {
  const PolicyScore* full = nullptr;
  for (const PolicyScore& policy : policies) {
    if (policy.policy == "full_frame") full = &policy;
  }
  if (full == nullptr) return;
  for (PolicyScore& policy : policies) {
    policy.sampling_misses = 0;
    policy.detector_misses = 0;
    policy.missed_event_ids.clear();
    policy.shortest_missed_ms.reset();
    for (EventScore& event : policy.events) {
      const EventScore* reference = nullptr;
      for (const EventScore& full_event : full->events) {
        if (full_event.id == event.id) reference = &full_event;
      }
      const bool detector_miss = reference != nullptr && !reference->matched;
      if (event.matched) {
        event.outcome = "hit";
      } else if (detector_miss) {
        event.outcome = "detector_miss";
        policy.detector_misses += 1;
        policy.missed_event_ids.push_back(event.id);
      } else {
        event.outcome = "sampling_miss";
        policy.sampling_misses += 1;
        policy.missed_event_ids.push_back(event.id);
      }
      if (!event.matched) {
        if (!policy.shortest_missed_ms || event.duration_ms < *policy.shortest_missed_ms) {
          policy.shortest_missed_ms = event.duration_ms;
        }
      }
    }
  }
}

}  // namespace fb
