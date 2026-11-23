// SPDX-License-Identifier: MIT
#include "evaluate.hpp"

#include "error.hpp"
#include "labels.hpp"
#include "media.hpp"
#include "scoring.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fb {
namespace {

std::vector<FrameObs> run_detector(const DecodedClip& clip, const DetectorConfig& detector,
                                   const std::vector<char>& selected, const std::string& clip_id,
                                   const std::string& policy, std::optional<int> budget, std::vector<TraceRow>* trace,
                                   int& calls) {
  std::vector<FrameObs> ran;
  for (std::size_t i = 0; i < clip.zones.size(); ++i) {
    if (!selected[i] || clip.pts_ms[i] < 0) continue;
    const double score = zone_fraction(clip.zones[i], detector);
    calls += 1;
    FrameObs obs;
    obs.index = static_cast<int>(i);
    obs.pts_ms = clip.pts_ms[i];
    obs.score = score;
    obs.positive = score + 1e-12 >= detector.fraction_threshold;
    ran.push_back(obs);
    if (trace != nullptr) {
      TraceRow row;
      row.clip_id = clip_id;
      row.policy = policy;
      row.budget_hz = budget;
      row.index = obs.index;
      row.pts_ms = obs.pts_ms;
      row.score = score;
      row.positive = obs.positive;
      trace->push_back(row);
    }
  }
  return ran;
}

}  // namespace

Dataset load_dataset(const std::string& path, const std::string& root) {
  std::ifstream in(path);
  if (!in) throw Error("io_error", "Could not open dataset " + path + ".");
  std::ostringstream ss;
  ss << in.rdbuf();
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(ss.str());
  } catch (const nlohmann::json::exception& ex) {
    throw Error("invalid_labels", std::string("dataset is not valid JSON: ") + ex.what());
  }
  if (!j.is_object() || j.value("schema_version", "") != "1.0" || !j.contains("clips") || !j.at("clips").is_array()) {
    throw Error("invalid_labels", "dataset " + path + " must be schema 1.0 with a clips array.");
  }
  Dataset dataset;
  dataset.name = j.value("name", std::string("dataset"));
  for (const auto& clip : j.at("clips")) {
    DatasetClip item;
    item.id = clip.at("id").get<std::string>();
    const auto join = [&](const char* key) {
      const std::string rel = clip.at(key).get<std::string>();
      if (rel.empty() || rel.front() == '/' || rel.find("..") != std::string::npos) {
        throw Error("invalid_labels", std::string("dataset path for ") + key +
                                          " must be a relative path inside the fixtures directory.");
      }
      return root + "/" + rel;
    };
    item.media = join("media");
    item.labels = join("labels");
    item.timestamps = join("timestamps");
    dataset.clips.push_back(std::move(item));
  }
  if (dataset.clips.empty()) throw Error("invalid_labels", "dataset " + path + " has no clips.");
  return dataset;
}

ClipEval evaluate_clip(const std::string& media, const std::string& labels_path, const std::string& timestamps_path,
                       const DetectorConfig& detector, const Limits& limits, const std::vector<PolicyRequest>& policies,
                       std::vector<TraceRow>* trace) {
  if (policies.empty()) throw Error("usage", "evaluate needs at least one policy.");
  const LabelSet labels = load_labels(labels_path, detector, limits);
  const bool have_timestamps = !timestamps_path.empty();
  TimestampLog timestamps;
  const TimestampLog* ts_ptr = nullptr;
  if (have_timestamps) {
    timestamps = load_timestamps(timestamps_path, limits);
    ts_ptr = &timestamps;
  }
  const DecodedClip decoded = decode_clip(media, ts_ptr, detector, limits, labels.fps_nominal);

  ClipEval eval;
  eval.clip_id = labels.clip_id;
  eval.session_id = labels.session_id;
  eval.media = media;
  eval.labels_path = labels_path;
  eval.sha256 = decoded.sha256;
  eval.width = decoded.width;
  eval.height = decoded.height;
  eval.frames = static_cast<int>(decoded.zones.size());
  eval.frames_missing_timestamp = decoded.missing;
  eval.timeline_end_ms = std::max(decoded.timeline_end_ms, 0);
  for (const Event& event : labels.events) eval.timeline_end_ms = std::max(eval.timeline_end_ms, event.end_ms);
  eval.container_fps = decoded.container_fps;
  eval.decoder = decoded.decoder;
  eval.backend = decoded.backend;
  eval.timestamp_source = decoded.timestamp_source;
  eval.labels = labels;

  for (const PolicyRequest& request : policies) {
    if (request.policy != "full_frame" && request.policy != "uniform" && request.policy != "motion") {
      throw Error("usage", "unknown policy \"" + request.policy + "\". Use full, uniform, or motion.");
    }
    if (request.policy == "full_frame" && request.budget_hz) {
      throw Error("usage", "full_frame does not take a budget. Drop --budget for that policy.");
    }
    if (request.policy != "full_frame" && !request.budget_hz) {
      throw Error("usage", request.policy + " needs a positive --budget in samples or calls per second.");
    }

    std::vector<char> selected(decoded.zones.size(), 0);
    int motion_checks = 0;
    if (request.policy == "full_frame") {
      std::fill(selected.begin(), selected.end(), 1);
    } else if (request.policy == "uniform") {
      selected = uniform_mask(decoded.pts_ms, *request.budget_hz);
    } else {
      std::vector<MotionObs> obs(decoded.zones.size());
      bool have_prev = false;
      cv::Mat previous;
      bool forced = false;
      for (std::size_t i = 0; i < decoded.zones.size(); ++i) {
        obs[i].index = static_cast<int>(i);
        obs[i].pts_ms = decoded.pts_ms[i];
        if (decoded.pts_ms[i] < 0) {
          previous = decoded.zones[i];
          have_prev = true;
          continue;
        }
        if (!forced) {
          forced = true;
          previous = decoded.zones[i];
          have_prev = true;
          continue;
        }
        if (have_prev) {
          obs[i].has_motion = true;
          obs[i].motion = zone_mad(previous, decoded.zones[i]);
          motion_checks += 1;
        }
        previous = decoded.zones[i];
        have_prev = true;
      }
      for (int index : select_motion(obs, *request.budget_hz, detector.motion_threshold)) {
        selected[static_cast<std::size_t>(index)] = 1;
      }
    }

    int calls = 0;
    const std::vector<FrameObs> ran =
        run_detector(decoded, detector, selected, labels.clip_id, request.policy, request.budget_hz, trace, calls);
    eval.policies.push_back(score_observations(labels, ran, request.policy, request.budget_hz, eval.frames, 1,
                                               decoded.missing, motion_checks, calls, detector.coalesce_gap_ms,
                                               detector.match_tolerance_ms));
  }
  annotate_outcomes(eval.policies);
  return eval;
}

}  // namespace fb
