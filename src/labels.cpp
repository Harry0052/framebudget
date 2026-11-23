// SPDX-License-Identifier: MIT
#include "labels.hpp"

#include "error.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace fb {
namespace {

std::string read_limited(const std::string& path, std::uintmax_t max_bytes, const std::string& category) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw Error(category, "Could not open " + path + ".");
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();
  if (text.size() > max_bytes) {
    throw Error("oversized_media", path + " is " + std::to_string(text.size()) +
                                       " bytes, over the label limit of " + std::to_string(max_bytes) +
                                       " bytes.");
  }
  return text;
}

nlohmann::json parse_json(const std::string& text, const std::string& category, const std::string& what) {
  try {
    return nlohmann::json::parse(text);
  } catch (const nlohmann::json::exception& ex) {
    throw Error(category, what + " is not valid JSON: " + ex.what());
  }
}

void require_object(const nlohmann::json& j, const std::string& what) {
  if (!j.is_object()) throw Error("invalid_labels", what + " must be a JSON object.");
}

std::string require_string(const nlohmann::json& j, const char* key) {
  if (!j.contains(key) || !j.at(key).is_string() || j.at(key).get<std::string>().empty()) {
    throw Error("invalid_labels", std::string(key) + " must be a non-empty string.");
  }
  return j.at(key).get<std::string>();
}

}  // namespace

DetectorConfig load_detector(const std::string& path) {
  const auto text = read_limited(path, 1 << 20, "io_error");
  const auto j = parse_json(text, "invalid_labels", path);
  require_object(j, "detector config");
  if (j.value("schema_version", "") != "1.0") {
    throw Error("invalid_labels", path + " schema_version must be \"1.0\".");
  }
  DetectorConfig cfg;
  try {
    const auto& zone = j.at("zone");
    cfg.zone_x = zone.at("x").get<int>();
    cfg.zone_y = zone.at("y").get<int>();
    cfg.zone_w = zone.at("w").get<int>();
    cfg.zone_h = zone.at("h").get<int>();
    const auto& color = j.at("color");
    cfg.r_min = color.at("r_min").get<int>();
    cfg.g_max = color.at("g_max").get<int>();
    cfg.b_max = color.at("b_max").get<int>();
    cfg.fraction_threshold = j.at("fraction_threshold").get<double>();
    cfg.coalesce_gap_ms = j.at("coalesce_gap_ms").get<int>();
    cfg.match_tolerance_ms = j.at("match_tolerance_ms").get<int>();
    cfg.short_band_lt_ms = j.at("short_band_lt_ms").get<int>();
    cfg.motion_threshold = j.at("motion_threshold").get<double>();
    cfg.threshold_session = j.value("threshold_session", cfg.threshold_session);
    if (j.contains("zone_buffer_bytes")) cfg.zone_buffer_bytes = j.at("zone_buffer_bytes").get<std::uintmax_t>();
  } catch (const Error&) {
    throw;
  } catch (const std::exception& ex) {
    throw Error("invalid_labels", std::string("detector config is incomplete: ") + ex.what());
  }
  if (cfg.zone_w <= 0 || cfg.zone_h <= 0 || cfg.zone_x < 0 || cfg.zone_y < 0) {
    throw Error("invalid_labels", "detector zone must sit on the frame with a positive width and height.");
  }
  if (!(cfg.fraction_threshold > 0.0 && cfg.fraction_threshold <= 1.0)) {
    throw Error("invalid_labels", "fraction_threshold must be in (0, 1].");
  }
  if (cfg.coalesce_gap_ms < 0 || cfg.match_tolerance_ms < 0 || cfg.short_band_lt_ms <= 0) {
    throw Error("invalid_labels", "coalesce, tolerance, and short-band bounds must be non-negative, and the short band must be positive.");
  }
  if (!(cfg.motion_threshold >= 0.0)) {
    throw Error("invalid_labels", "motion_threshold must be >= 0.");
  }
  return cfg;
}

LabelSet parse_labels(const std::string& text, const DetectorConfig& detector, const Limits& limits) {
  const auto j = parse_json(text, "invalid_labels", "labels");
  require_object(j, "labels");
  LabelSet labels;
  labels.schema_version = require_string(j, "schema_version");
  if (labels.schema_version != "1.0") {
    throw Error("invalid_labels", "labels schema_version must be \"1.0\".");
  }
  labels.clip_id = require_string(j, "clip_id");
  labels.session_id = require_string(j, "session_id");
  labels.event_class = require_string(j, "class");
  if (labels.event_class != "zone_intrusion") {
    throw Error("unsupported_class",
                "class \"" + labels.event_class +
                    "\" is not supported. The only class in this release is zone_intrusion.");
  }
  if (j.contains("time_unit") && j.at("time_unit").get<std::string>() != "seconds") {
    throw Error("invalid_labels", "time_unit must be \"seconds\" when it is present.");
  }
  if (!j.contains("fps_nominal") || !j.at("fps_nominal").is_number()) {
    throw Error("invalid_labels", "fps_nominal must be a positive number.");
  }
  labels.fps_nominal = j.at("fps_nominal").get<double>();
  if (!(labels.fps_nominal > 0.0) || !std::isfinite(labels.fps_nominal)) {
    throw Error("invalid_labels", "fps_nominal must be a positive finite number.");
  }
  if (!j.contains("events") || !j.at("events").is_array()) {
    throw Error("invalid_labels", "events must be an array. Use an empty array when the clip has no events.");
  }
  if (static_cast<int>(j.at("events").size()) > limits.max_events) {
    throw Error("invalid_labels", "events has more than the limit of " + std::to_string(limits.max_events) +
                                      " intervals.");
  }
  std::unordered_set<std::string> seen;
  for (const auto& ev : j.at("events")) {
    if (!ev.is_object()) throw Error("invalid_labels", "each event must be an object.");
    Event event;
    event.id = require_string(ev, "id");
    if (!seen.insert(event.id).second) {
      throw Error("invalid_labels", "duplicate event id \"" + event.id + "\".");
    }
    if (!ev.contains("start_ms") || !ev.at("start_ms").is_number_integer() || !ev.contains("end_ms") ||
        !ev.at("end_ms").is_number_integer()) {
      throw Error("invalid_labels", "event " + event.id + " needs integer start_ms and end_ms.");
    }
    if (!ev.contains("start_s") || !ev.at("start_s").is_number() || !ev.contains("end_s") ||
        !ev.at("end_s").is_number()) {
      throw Error("invalid_labels", "event " + event.id + " needs start_s and end_s in seconds.");
    }
    event.start_ms = ev.at("start_ms").get<int>();
    event.end_ms = ev.at("end_ms").get<int>();
    const double start_s = ev.at("start_s").get<double>();
    const double end_s = ev.at("end_s").get<double>();
    if (std::abs(start_s * 1000.0 - event.start_ms) > 0.51 || std::abs(end_s * 1000.0 - event.end_ms) > 0.51) {
      throw Error("invalid_labels", "event " + event.id +
                                        " has start_s/end_s that do not match start_ms/end_ms within 1 ms.");
    }
    if (event.start_ms < 0 || event.end_ms <= event.start_ms) {
      throw Error("invalid_labels", "event " + event.id +
                                        " must have end_ms greater than start_ms, both >= 0. Got " +
                                        std::to_string(event.start_ms) + ".." + std::to_string(event.end_ms) + ".");
    }
    const std::string computed = event.duration_ms() < detector.short_band_lt_ms ? "short" : "long";
    if (ev.contains("duration_band")) {
      if (!ev.at("duration_band").is_string()) {
        throw Error("invalid_labels", "event " + event.id + " duration_band must be a string.");
      }
      event.band = ev.at("duration_band").get<std::string>();
      if (event.band != computed) {
        throw Error("invalid_labels", "event " + event.id + " duration_band is \"" + event.band +
                                          "\" but the interval length is " + computed +
                                          " under short_band_lt_ms " + std::to_string(detector.short_band_lt_ms) +
                                          ".");
      }
    } else {
      event.band = computed;
    }
    labels.events.push_back(event);
  }

  for (std::size_t i = 0; i < labels.events.size(); ++i) {
    for (std::size_t k = i + 1; k < labels.events.size(); ++k) {
      const Event& a = labels.events[i];
      const Event& b = labels.events[k];
      // Closed intervals overlap when they share more than an endpoint.
      if (a.start_ms < b.end_ms && b.start_ms < a.end_ms) {
        std::ostringstream msg;
        msg << "events " << a.id << " [" << a.start_ms / 1000.0 << ", " << a.end_ms / 1000.0 << "] and " << b.id
            << " [" << b.start_ms / 1000.0 << ", " << b.end_ms / 1000.0
            << "] overlap. Split or trim them so closed intervals only touch at an endpoint, then run validate again.";
        throw Error("overlapping_labels", msg.str());
      }
    }
  }
  return labels;
}

LabelSet load_labels(const std::string& path, const DetectorConfig& detector, const Limits& limits) {
  return parse_labels(read_limited(path, limits.max_label_bytes, "io_error"), detector, limits);
}

TimestampLog parse_timestamps(const std::string& text) {
  const auto j = parse_json(text, "invalid_timestamps", "timestamps");
  if (!j.is_object()) throw Error("invalid_timestamps", "timestamps must be a JSON object.");
  if (j.value("schema_version", "") != "1.0") {
    throw Error("invalid_timestamps", "timestamps schema_version must be \"1.0\".");
  }
  if (!j.contains("pts_ms") || !j.at("pts_ms").is_array()) {
    throw Error("invalid_timestamps", "timestamps pts_ms must be an array of milliseconds or null.");
  }
  TimestampLog log;
  log.source = j.value("source", std::string("session_log"));
  int previous = -1;
  bool have_previous = false;
  for (const auto& item : j.at("pts_ms")) {
    if (item.is_null()) {
      log.pts_ms.push_back(std::nullopt);
      continue;
    }
    if (!item.is_number_integer()) {
      throw Error("invalid_timestamps", "each pts_ms entry must be an integer millisecond or null.");
    }
    const int value = item.get<int>();
    if (value < 0) throw Error("invalid_timestamps", "presentation timestamps must be >= 0 ms.");
    if (have_previous && value <= previous) {
      throw Error("invalid_timestamps",
                  "presentation timestamps must be strictly increasing. " + std::to_string(value) +
                      " ms does not follow " + std::to_string(previous) + " ms.");
    }
    previous = value;
    have_previous = true;
    log.pts_ms.push_back(value);
  }
  return log;
}

TimestampLog load_timestamps(const std::string& path, const Limits& limits) {
  return parse_timestamps(read_limited(path, limits.max_label_bytes, "invalid_timestamps"));
}

}  // namespace fb
