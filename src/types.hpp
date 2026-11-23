// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fb {

struct Limits {
  std::uintmax_t max_bytes = 8ull * 1024ull * 1024ull;
  std::uintmax_t max_label_bytes = 1ull * 1024ull * 1024ull;
  std::uintmax_t max_zone_bytes = 64ull * 1024ull * 1024ull;
  int max_frames = 3000;
  int max_width = 1280;
  int max_height = 720;
  int max_duration_ms = 120000;
  int max_events = 100;
};

struct DetectorConfig {
  int zone_x = 120;
  int zone_y = 80;
  int zone_w = 80;
  int zone_h = 80;
  int r_min = 200;
  int g_max = 40;
  int b_max = 40;
  double fraction_threshold = 0.5;
  int coalesce_gap_ms = 1250;
  int match_tolerance_ms = 0;
  int short_band_lt_ms = 1000;
  double motion_threshold = 20.0;
  std::string threshold_session = "session-tune";
  std::uintmax_t zone_buffer_bytes = 64ull * 1024ull * 1024ull;
};

struct Event {
  std::string id;
  int start_ms = 0;
  int end_ms = 0;
  std::string band;

  int duration_ms() const { return end_ms - start_ms; }
};

struct LabelSet {
  std::string schema_version;
  std::string clip_id;
  std::string session_id;
  std::string event_class;
  double fps_nominal = 0;
  std::vector<Event> events;
};

struct TimestampLog {
  std::string source = "session_log";
  // nullopt means the frame was decoded but has no presentation timestamp.
  std::vector<std::optional<int>> pts_ms;
};

// One frame the detector actually ran on, in presentation order.
struct FrameObs {
  int index = 0;
  int pts_ms = 0;
  bool positive = false;
  double score = 0;
};

struct Detection {
  int start_ms = 0;
  int end_ms = 0;
  std::vector<int> frame_indices;
  std::vector<int> frame_pts;
};

struct EventScore {
  std::string id;
  std::string band;
  int start_ms = 0;
  int end_ms = 0;
  int duration_ms = 0;
  bool matched = false;
  // hit, sampling_miss, or detector_miss. sampling/detector split is filled
  // once a full-frame result exists to compare against.
  std::string outcome = "hit";
  std::optional<int> delay_ms;
  std::optional<int> first_hit_ms;
  std::optional<int> first_hit_index;
};

struct PolicyScore {
  std::string policy;
  std::optional<int> budget_hz;
  int decoded_frames = 0;
  int decode_passes = 1;
  int frames_missing_timestamp = 0;
  int motion_checks = 0;
  int detector_calls = 0;
  int true_positives = 0;
  int denominator = 0;
  bool recall_defined = false;
  int short_tp = 0;
  int short_den = 0;
  int long_tp = 0;
  int long_den = 0;
  int false_alarms = 0;
  int sampling_misses = 0;
  int detector_misses = 0;
  std::optional<int> shortest_missed_ms;
  std::optional<double> mean_delay_ms;
  std::vector<std::string> missed_event_ids;
  std::vector<EventScore> events;
  std::vector<Detection> detections;
};

struct ClipEval {
  std::string clip_id;
  std::string session_id;
  std::string media;
  std::string labels_path;
  std::string sha256;
  int width = 0;
  int height = 0;
  int frames = 0;
  int frames_missing_timestamp = 0;
  int timeline_end_ms = 0;
  double container_fps = 0;
  std::string decoder;
  std::string backend;
  std::string timestamp_source;
  LabelSet labels;
  std::vector<PolicyScore> policies;
};

struct TraceRow {
  std::string clip_id;
  std::string policy;
  std::optional<int> budget_hz;
  int index = 0;
  int pts_ms = 0;
  double score = 0;
  bool positive = false;
};

struct MotionObs {
  int index = 0;
  int pts_ms = -1;
  bool has_motion = false;
  double motion = 0;
};

struct Recommendation {
  bool available = false;
  std::string policy;
  std::optional<int> budget_hz;
  int max_misses = 0;
  int missed_events = 0;
  int detector_calls = 0;
  int decoded_frames = 0;
  std::string reason;
};

}  // namespace fb
