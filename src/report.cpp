// SPDX-License-Identifier: MIT
#include "report.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace fb {
namespace {

std::string esc(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

nlohmann::json band_json(int tp, int den) {
  return {{"true_positives", tp}, {"denominator", den}, {"defined", den > 0}};
}

}  // namespace

nlohmann::json policy_json(const PolicyScore& policy) {
  nlohmann::json events = nlohmann::json::array();
  for (const EventScore& event : policy.events) {
    nlohmann::json row = {{"id", event.id},
                          {"band", event.band},
                          {"start_ms", event.start_ms},
                          {"end_ms", event.end_ms},
                          {"duration_ms", event.duration_ms},
                          {"matched", event.matched},
                          {"outcome", event.outcome}};
    if (event.delay_ms) row["delay_ms"] = *event.delay_ms;
    else row["delay_ms"] = nullptr;
    if (event.first_hit_ms) row["first_hit_ms"] = *event.first_hit_ms;
    else row["first_hit_ms"] = nullptr;
    if (event.first_hit_index) row["first_hit_index"] = *event.first_hit_index;
    else row["first_hit_index"] = nullptr;
    events.push_back(std::move(row));
  }
  nlohmann::json detections = nlohmann::json::array();
  for (const Detection& detection : policy.detections) {
    detections.push_back({{"start_ms", detection.start_ms},
                          {"end_ms", detection.end_ms},
                          {"frame_indices", detection.frame_indices}});
  }
  nlohmann::json j = {{"policy", policy.policy},
                      {"budget_hz", policy.budget_hz ? nlohmann::json(*policy.budget_hz) : nlohmann::json(nullptr)},
                      {"decoded_frames", policy.decoded_frames},
                      {"decode_passes", policy.decode_passes},
                      {"frames_missing_timestamp", policy.frames_missing_timestamp},
                      {"motion_checks", policy.motion_checks},
                      {"detector_calls", policy.detector_calls},
                      {"true_positives", policy.true_positives},
                      {"denominator", policy.denominator},
                      {"recall_defined", policy.recall_defined},
                      {"false_alarms", policy.false_alarms},
                      {"sampling_misses", policy.sampling_misses},
                      {"detector_misses", policy.detector_misses},
                      {"missed_event_ids", policy.missed_event_ids},
                      {"short", band_json(policy.short_tp, policy.short_den)},
                      {"long", band_json(policy.long_tp, policy.long_den)},
                      {"events", std::move(events)},
                      {"detections", std::move(detections)}};
  if (policy.shortest_missed_ms) j["shortest_missed_ms"] = *policy.shortest_missed_ms;
  else j["shortest_missed_ms"] = nullptr;
  if (policy.mean_delay_ms) j["mean_delay_ms"] = *policy.mean_delay_ms;
  else j["mean_delay_ms"] = nullptr;
  return j;
}

nlohmann::json clip_json(const ClipEval& clip) {
  nlohmann::json ground = nlohmann::json::array();
  for (const Event& event : clip.labels.events) {
    ground.push_back({{"id", event.id},
                      {"band", event.band},
                      {"start_ms", event.start_ms},
                      {"end_ms", event.end_ms},
                      {"duration_ms", event.duration_ms()}});
  }
  nlohmann::json policies = nlohmann::json::array();
  nlohmann::json full_recall = {{"defined", false}, {"true_positives", 0}, {"denominator", 0}};
  for (const PolicyScore& policy : clip.policies) {
    if (policy.policy == "full_frame" && !policy.budget_hz) {
      full_recall = {{"defined", policy.recall_defined},
                     {"true_positives", policy.true_positives},
                     {"denominator", policy.denominator},
                     {"false_alarms", policy.false_alarms},
                     {"detector_calls", policy.detector_calls},
                     {"decoded_frames", policy.decoded_frames}};
    }
    policies.push_back(policy_json(policy));
  }
  return {{"clip_id", clip.clip_id},
          {"session_id", clip.session_id},
          {"media", clip.media},
          {"labels", clip.labels_path},
          {"sha256", clip.sha256},
          {"width", clip.width},
          {"height", clip.height},
          {"frames", clip.frames},
          {"frames_missing_timestamp", clip.frames_missing_timestamp},
          {"timeline_end_ms", clip.timeline_end_ms},
          {"container_fps", clip.container_fps},
          {"decoder", clip.decoder},
          {"backend", clip.backend},
          {"timestamp_source", clip.timestamp_source},
          {"ground_truth", ground},
          {"full_frame_recall", full_recall},
          {"policies", std::move(policies)}};
}

CompareResult build_comparison(const std::string& dataset, const std::vector<ClipEval>& clips, const DetectorConfig& detector,
                               int max_misses, const std::string& tool_version) {
  struct Key {
    std::string policy;
    int budget;  // -1 for full frame
    bool operator<(const Key& other) const {
      if (policy != other.policy) return policy < other.policy;
      return budget < other.budget;
    }
  };
  // Preserve the first clip's policy order instead of sorting names.
  std::vector<Key> order;
  if (!clips.empty()) {
    for (const PolicyScore& policy : clips.front().policies) {
      order.push_back(Key{policy.policy, policy.budget_hz ? *policy.budget_hz : -1});
    }
  }

  nlohmann::json aggregate = nlohmann::json::array();
  struct Totals {
    PolicyScore score;
  };
  std::map<Key, Totals> totals;
  for (const Key& key : order) {
    Totals item;
    item.score.policy = key.policy;
    if (key.budget >= 0) item.score.budget_hz = key.budget;
    item.score.recall_defined = false;
    totals[key] = item;
  }
  for (const ClipEval& clip : clips) {
    for (const PolicyScore& policy : clip.policies) {
      Key key{policy.policy, policy.budget_hz ? *policy.budget_hz : -1};
      PolicyScore& sum = totals[key].score;
      sum.decoded_frames += policy.decoded_frames;
      sum.motion_checks += policy.motion_checks;
      sum.detector_calls += policy.detector_calls;
      sum.true_positives += policy.true_positives;
      sum.denominator += policy.denominator;
      sum.false_alarms += policy.false_alarms;
      sum.sampling_misses += policy.sampling_misses;
      sum.detector_misses += policy.detector_misses;
      sum.short_tp += policy.short_tp;
      sum.short_den += policy.short_den;
      sum.long_tp += policy.long_tp;
      sum.long_den += policy.long_den;
      sum.frames_missing_timestamp += policy.frames_missing_timestamp;
      for (const std::string& id : policy.missed_event_ids) sum.missed_event_ids.push_back(clip.clip_id + ":" + id);
      if (policy.shortest_missed_ms &&
          (!sum.shortest_missed_ms || *policy.shortest_missed_ms < *sum.shortest_missed_ms)) {
        sum.shortest_missed_ms = policy.shortest_missed_ms;
      }
      if (policy.mean_delay_ms && policy.true_positives > 0) {
        const int previous_hits = sum.true_positives - policy.true_positives;
        const double previous_sum = sum.mean_delay_ms.value_or(0.0) * static_cast<double>(previous_hits);
        sum.mean_delay_ms = (previous_sum + (*policy.mean_delay_ms * static_cast<double>(policy.true_positives))) /
                            static_cast<double>(sum.true_positives);
      }
    }
  }
  for (const Key& key : order) {
    PolicyScore& sum = totals[key].score;
    sum.recall_defined = sum.denominator > 0;
    aggregate.push_back(policy_json(sum));
  }

  Recommendation recommendation;
  recommendation.max_misses = max_misses;
  int best_calls = 0;
  int best_motion = 0;
  bool have = false;
  std::ostringstream considered;
  considered << "Decision threshold is " << max_misses << " observed misses on this staged set. ";
  for (const Key& key : order) {
    const PolicyScore& sum = totals[key].score;
    const int missed = sum.sampling_misses + sum.detector_misses;
    const bool eligible = missed <= max_misses;
    considered << sum.policy;
    if (sum.budget_hz) considered << "@" << *sum.budget_hz << "Hz";
    considered << " missed " << missed << " with " << sum.detector_calls << " detector calls";
    considered << (eligible ? " (eligible). " : " (above the threshold). ");
    if (!eligible) continue;
    const int motion = sum.motion_checks;
    const bool better = !have || sum.detector_calls < best_calls ||
                        (sum.detector_calls == best_calls && motion < best_motion) ||
                        (sum.detector_calls == best_calls && motion == best_motion && sum.policy < recommendation.policy);
    if (better) {
      have = true;
      best_calls = sum.detector_calls;
      best_motion = motion;
      recommendation.available = true;
      recommendation.policy = sum.policy;
      recommendation.budget_hz = sum.budget_hz;
      recommendation.missed_events = missed;
      recommendation.detector_calls = sum.detector_calls;
      recommendation.decoded_frames = sum.decoded_frames;
    }
  }
  if (recommendation.available) {
    std::ostringstream reason;
    reason << "Selected " << recommendation.policy;
    if (recommendation.budget_hz) reason << " at " << *recommendation.budget_hz << " Hz";
    reason << " because missed events " << recommendation.missed_events << " is within the decision threshold of "
           << max_misses << " and detector calls are " << recommendation.detector_calls
           << ", the fewest among policies that met the threshold. "
           << "Decoded frames stay listed separately and are not counted as savings. "
           << "This threshold is a count of misses on the staged eval set, not a claim about any other camera. ";
    reason << considered.str();
    recommendation.reason = reason.str();
  } else {
    recommendation.reason = "No compared policy stayed within the decision threshold of " + std::to_string(max_misses) +
                            " observed misses. " + considered.str();
  }

  nlohmann::json clips_json = nlohmann::json::array();
  for (const ClipEval& clip : clips) clips_json.push_back(clip_json(clip));

  nlohmann::json machine = {
      {"schema_version", "1.0"},
      {"tool_version", tool_version},
      {"dataset", dataset},
      {"status", "complete"},
      {"limitations",
       {"Synthetic staged proxies only. A result describes this clip, not a real emergency, and it is not a protective or operational control.",
        "Detector-call reductions are not decode reductions. Every policy in this release decodes the clip so motion checks and presentation timestamps stay defined.",
        "Full-frame recall is the detector reference. A miss that full frame also misses is a detector miss. A miss that only a sampled policy misses is a sampling miss.",
        "Recall is undefined when the label file has no events. That is not a recall of zero. False alarms on those clips still count, including noisy motion clips.",
        "The color threshold was frozen on session-tune. The staged eval set does not move it.",
        "One class (zone_intrusion), one marked zone, one fixed camera."}},
      {"provenance",
       {{"decoder", clips.empty() ? "" : clips.front().decoder},
        {"backend", clips.empty() ? "" : clips.front().backend},
        {"threshold_session", detector.threshold_session},
        {"fraction_threshold", detector.fraction_threshold},
        {"motion_threshold", detector.motion_threshold},
        {"coalesce_gap_ms", detector.coalesce_gap_ms},
        {"match_tolerance_ms", detector.match_tolerance_ms},
        {"short_band_lt_ms", detector.short_band_lt_ms},
        {"zone",
         {{"x", detector.zone_x}, {"y", detector.zone_y}, {"w", detector.zone_w}, {"h", detector.zone_h}}},
        {"decision_threshold_max_misses", max_misses}}},
      {"recommendation",
       {{"available", recommendation.available},
        {"policy", recommendation.available ? nlohmann::json(recommendation.policy) : nlohmann::json(nullptr)},
        {"budget_hz", recommendation.budget_hz ? nlohmann::json(*recommendation.budget_hz) : nlohmann::json(nullptr)},
        {"max_misses", max_misses},
        {"missed_events", recommendation.missed_events},
        {"detector_calls", recommendation.detector_calls},
        {"decoded_frames", recommendation.decoded_frames},
        {"reason", recommendation.reason}}},
      {"aggregate", aggregate},
      {"clips", std::move(clips_json)}};

  CompareResult result;
  result.machine = std::move(machine);
  result.recommendation = recommendation;
  result.html = render_html(result.machine);
  return result;
}

std::string render_html(const nlohmann::json& machine) {
  std::ostringstream html;
  html << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
       << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
       << "<title>FrameBudget staged comparison</title>\n<style>\n"
       << "body{margin:0;background:#f4f1ea;color:#1c1915;font:16px/1.45 Georgia,serif;}\n"
       << "main{max-width:960px;margin:0 auto;padding:24px 16px 64px;}\n"
       << "h1{font-family:sans-serif;font-size:1.6rem;margin:0 0 8px;}\n"
       << "h2{font-family:sans-serif;font-size:1.15rem;margin:28px 0 8px;}\n"
       << "p,li{margin:8px 0;}\n"
       << "table{border-collapse:collapse;width:100%;background:#fff;font-family:sans-serif;font-size:0.86rem;}\n"
       << "th,td{border:1px solid #d9d2c5;padding:6px 8px;text-align:left;vertical-align:top;}\n"
       << "th{background:#ebe4d6;}\n"
       << ".note{background:#fff;border:1px solid #d9d2c5;padding:12px 14px;}\n"
       << "code{font-family:ui-monospace,monospace;font-size:0.9em;}\n"
       << "svg{width:100%;height:auto;background:#fff;border:1px solid #d9d2c5;}\n"
       << "</style>\n</head>\n<body>\n<main>\n";
  html << "<h1>FrameBudget staged comparison</h1>\n";
  html << "<p>Dataset <code>" << esc(machine.at("dataset").get<std::string>()) << "</code>, tool "
       << esc(machine.at("tool_version").get<std::string>()) << ", decoder <code>"
       << esc(machine.at("provenance").at("decoder").get<std::string>()) << "</code>, backend <code>"
       << esc(machine.at("provenance").at("backend").get<std::string>()) << "</code>.</p>\n";
  html << "<div class=\"note\"><strong>Limitations.</strong><ul>\n";
  for (const auto& item : machine.at("limitations")) {
    html << "<li>" << esc(item.get<std::string>()) << "</li>\n";
  }
  html << "</ul></div>\n";
  html << "<h2>Recommendation</h2>\n<p>" << esc(machine.at("recommendation").at("reason").get<std::string>())
       << "</p>\n";
  html << "<p>Threshold source: <code>decision_threshold_max_misses = "
       << machine.at("provenance").at("decision_threshold_max_misses").get<int>()
       << "</code>. Color threshold " << machine.at("provenance").at("fraction_threshold").get<double>()
       << " was frozen on <code>" << esc(machine.at("provenance").at("threshold_session").get<std::string>())
       << "</code>.</p>\n";

  html << "<h2>Cost versus recall</h2>\n<table>\n<thead><tr>"
       << "<th>Policy</th><th>Budget</th><th>Decoded frames</th><th>Motion checks</th><th>Detector calls</th>"
       << "<th>Recall</th><th>Short</th><th>Long</th><th>False alarms</th><th>Sampling misses</th>"
       << "<th>Detector misses</th><th>Shortest missed</th><th>Mean delay</th></tr></thead>\n<tbody>\n";
  for (const auto& row : machine.at("aggregate")) {
    const bool defined = row.at("recall_defined").get<bool>();
    html << "<tr><td>" << esc(row.at("policy").get<std::string>()) << "</td><td>";
    if (row.at("budget_hz").is_null()) html << "all frames";
    else html << row.at("budget_hz").get<int>() << " Hz";
    html << "</td><td>" << row.at("decoded_frames").get<int>() << "</td><td>" << row.at("motion_checks").get<int>()
         << "</td><td>" << row.at("detector_calls").get<int>() << "</td><td>";
    if (!defined) html << "undefined (no labeled events in the denominator)";
    else html << row.at("true_positives").get<int>() << "/" << row.at("denominator").get<int>();
    html << "</td><td>" << row.at("short").at("true_positives").get<int>() << "/"
         << row.at("short").at("denominator").get<int>() << "</td><td>" << row.at("long").at("true_positives").get<int>()
         << "/" << row.at("long").at("denominator").get<int>() << "</td><td>" << row.at("false_alarms").get<int>()
         << "</td><td>" << row.at("sampling_misses").get<int>() << "</td><td>" << row.at("detector_misses").get<int>()
         << "</td><td>";
    if (row.at("shortest_missed_ms").is_null()) html << "none";
    else html << row.at("shortest_missed_ms").get<int>() << " ms";
    html << "</td><td>";
    if (row.at("mean_delay_ms").is_null()) html << "n/a";
    else html << row.at("mean_delay_ms").get<double>() << " ms";
    html << "</td></tr>\n";
  }
  html << "</tbody></table>\n";
  html << "<p>Decoded frames and detector calls are different columns on purpose. A smaller call count with the same "
          "decoded-frame count is not a claim that the decoder did less work.</p>\n";

  for (const auto& clip : machine.at("clips")) {
    html << "<h2>Clip <code>" << esc(clip.at("clip_id").get<std::string>()) << "</code></h2>\n";
    html << "<p>Session <code>" << esc(clip.at("session_id").get<std::string>()) << "</code>. Media <code>"
         << esc(clip.at("media").get<std::string>()) << "</code>. Labels <code>" << esc(clip.at("labels").get<std::string>())
         << "</code>. SHA-256 <code>" << esc(clip.at("sha256").get<std::string>()) << "</code>. "
         << clip.at("frames").get<int>() << " decoded frames, timestamp source <code>"
         << esc(clip.at("timestamp_source").get<std::string>()) << "</code>, missing timestamps "
         << clip.at("frames_missing_timestamp").get<int>() << ". Full-frame recall: ";
    const auto& full = clip.at("full_frame_recall");
    if (!full.at("defined").get<bool>()) html << "undefined (denominator " << full.at("denominator").get<int>() << ")";
    else html << full.at("true_positives").get<int>() << "/" << full.at("denominator").get<int>();
    html << ". False alarms at full frame: " << full.at("false_alarms").get<int>() << ".</p>\n";

    if (clip.at("ground_truth").empty()) {
      html << "<p>Ground truth has no intervals, so recall is undefined. False detections below are not a recall of zero.</p>\n";
    } else {
      html << "<p>Ground truth:</p><ul>\n";
      for (const auto& event : clip.at("ground_truth")) {
        html << "<li><code>" << esc(event.at("id").get<std::string>()) << "</code> " << esc(event.at("band").get<std::string>())
             << " " << event.at("start_ms").get<int>() << "–" << event.at("end_ms").get<int>() << " ms</li>\n";
      }
      html << "</ul>\n";
    }

    const int axis = std::max(1, clip.at("timeline_end_ms").get<int>());
    const int rows = 1 + static_cast<int>(clip.at("policies").size());
    const int height = 36 + rows * 28;
    html << "<svg viewBox=\"0 0 760 " << height << "\" role=\"img\">\n<title>Timeline for "
         << esc(clip.at("clip_id").get<std::string>()) << "</title>\n";
    auto x_of = [axis](int ms) { return 120.0 + (720.0 - 140.0) * (static_cast<double>(ms) / static_cast<double>(axis)); };
    html << "<text x=\"8\" y=\"24\" font-size=\"12\" font-family=\"sans-serif\">truth</text>\n";
    for (const auto& event : clip.at("ground_truth")) {
      const double x1 = x_of(event.at("start_ms").get<int>());
      const double x2 = std::max(x1 + 2.0, x_of(event.at("end_ms").get<int>()));
      html << "<rect x=\"" << x1 << "\" y=\"12\" width=\"" << (x2 - x1) << "\" height=\"14\" fill=\"#1b7f4e\"></rect>\n";
    }
    int row = 0;
    for (const auto& policy : clip.at("policies")) {
      const int y = 40 + row * 28;
      std::string name = policy.at("policy").get<std::string>();
      if (!policy.at("budget_hz").is_null()) name += " " + std::to_string(policy.at("budget_hz").get<int>()) + "Hz";
      html << "<text x=\"8\" y=\"" << (y + 12) << "\" font-size=\"12\" font-family=\"sans-serif\">" << esc(name)
           << "</text>\n";
      for (const auto& detection : policy.at("detections")) {
        const double x1 = x_of(detection.at("start_ms").get<int>());
        const double x2 = std::max(x1 + 2.0, x_of(detection.at("end_ms").get<int>()));
        html << "<rect x=\"" << x1 << "\" y=\"" << y << "\" width=\"" << (x2 - x1)
             << "\" height=\"14\" fill=\"#1d4e89\"></rect>\n";
      }
      for (const auto& event : policy.at("events")) {
        if (event.at("matched").get<bool>()) continue;
        const double x1 = x_of(event.at("start_ms").get<int>());
        const double x2 = std::max(x1 + 3.0, x_of(event.at("end_ms").get<int>()));
        html << "<rect x=\"" << x1 << "\" y=\"" << (y + 16) << "\" width=\"" << (x2 - x1)
             << "\" height=\"6\" fill=\"#a33b32\"></rect>\n";
      }
      row += 1;
    }
    html << "</svg>\n<p>Green is labeled truth. Blue is a coalesced detection. Red is a labeled interval this policy missed.</p>\n<ul>\n";
    for (const auto& policy : clip.at("policies")) {
      for (const auto& event : policy.at("events")) {
        if (event.at("outcome").get<std::string>() == "hit") continue;
        html << "<li><code>" << esc(event.at("id").get<std::string>()) << "</code> is a "
             << esc(event.at("outcome").get<std::string>()) << " under " << esc(policy.at("policy").get<std::string>());
        if (!policy.at("budget_hz").is_null()) html << " at " << policy.at("budget_hz").get<int>() << " Hz";
        html << " (" << event.at("duration_ms").get<int>() << " ms, " << esc(event.at("band").get<std::string>())
             << "). Source labels <code>" << esc(clip.at("labels").get<std::string>()) << "</code>, media SHA-256 <code>"
             << esc(clip.at("sha256").get<std::string>()) << "</code>.</li>\n";
      }
    }
    html << "</ul>\n";
  }
  html << "</main>\n</body>\n</html>\n";
  return html.str();
}

}  // namespace fb
