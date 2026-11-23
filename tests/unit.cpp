// SPDX-License-Identifier: MIT
#include "cli.hpp"
#include "error.hpp"
#include "evaluate.hpp"
#include "labels.hpp"
#include "scoring.hpp"
#include "sha256.hpp"
#include "types.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool cond, const char* expr, int line) {
  if (!cond) {
    std::cerr << "FAIL " << line << " " << expr << "\n";
    g_failures += 1;
  }
}

#define CHECK(cond) check(static_cast<bool>(cond), #cond, __LINE__)

std::string fixture(const std::string& rel) {
  return std::string(FRAMEBUDGET_FIXTURES) + "/" + rel;
}

nlohmann::json parse_last_json(const std::string& text) {
  const auto pos = text.rfind("\n{");
  const auto start = pos == std::string::npos ? text.find('{') : pos + 1;
  if (start == std::string::npos) throw std::runtime_error("no JSON object in: " + text);
  return nlohmann::json::parse(text.substr(start));
}

nlohmann::json read_json(const std::string& path) {
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return nlohmann::json::parse(buffer.str());
}

nlohmann::json policy_named(const nlohmann::json& policies, const std::string& name, int budget_or_neg1) {
  for (const auto& policy : policies) {
    if (policy.at("policy").get<std::string>() != name) continue;
    if (budget_or_neg1 < 0 && policy.at("budget_hz").is_null()) return policy;
    if (budget_or_neg1 >= 0 && !policy.at("budget_hz").is_null() && policy.at("budget_hz").get<int>() == budget_or_neg1) {
      return policy;
    }
  }
  throw std::runtime_error("missing policy " + name);
}

fb::LabelSet labels_with(std::vector<fb::Event> events) {
  fb::LabelSet labels;
  labels.schema_version = "1.0";
  labels.clip_id = "synthetic";
  labels.session_id = "session-eval";
  labels.event_class = "zone_intrusion";
  labels.fps_nominal = 20;
  labels.events = std::move(events);
  return labels;
}

fb::Event ev(std::string id, int start, int end, std::string band) {
  fb::Event event;
  event.id = std::move(id);
  event.start_ms = start;
  event.end_ms = end;
  event.band = std::move(band);
  return event;
}

int call_cli(const std::vector<std::string>& args, std::string& out, std::string& err) {
  std::vector<std::string> storage = args;
  std::vector<char*> ptrs;
  ptrs.reserve(storage.size() + 1);
  for (std::string& item : storage) ptrs.push_back(item.data());
  ptrs.push_back(nullptr);
  std::stringstream bout;
  std::stringstream berr;
  auto* old_out = std::cout.rdbuf(bout.rdbuf());
  auto* old_err = std::cerr.rdbuf(berr.rdbuf());
  const int code = fb::run_cli(static_cast<int>(storage.size()), ptrs.data());
  std::cout.rdbuf(old_out);
  std::cerr.rdbuf(old_err);
  out = bout.str();
  err = berr.str();
  return code;
}

void test_sha_and_schedule() {
  CHECK(fb::sha256_bytes("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(fb::sha256_bytes("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  std::vector<int> pts;
  for (int i = 0; i < 200; ++i) pts.push_back(i * 50);
  const auto mask = fb::uniform_mask(pts, 1);
  int selected = 0;
  for (char bit : mask) selected += bit ? 1 : 0;
  CHECK(selected == 10);
  CHECK(mask[44] == 0);
  CHECK(mask[120] == 1);
  CHECK(mask[140] == 1);
  CHECK(mask[160] == 1);
  CHECK(mask[180] == 1);

  std::vector<fb::MotionObs> motion(4);
  motion[0] = fb::MotionObs{0, 0, false, 0};
  motion[1] = fb::MotionObs{1, 50, true, 1};
  motion[2] = fb::MotionObs{2, 100, true, 100};
  motion[3] = fb::MotionObs{3, 400, true, 100};
  const auto chosen = fb::select_motion(motion, 1, 20);
  CHECK(chosen.size() == 2);
  CHECK(chosen[0] == 0);
  CHECK(chosen[1] == 2);
}

void test_scorer_contracts() {
  std::vector<fb::FrameObs> many;
  for (int i = 0; i < 50; ++i) {
    fb::FrameObs frame;
    frame.index = i;
    frame.pts_ms = i * 50;
    frame.positive = true;
    frame.score = 1;
    many.push_back(frame);
  }
  auto labels = labels_with({ev("only", 0, 5000, "long")});
  auto score = fb::score_observations(labels, many, "full_frame", std::nullopt, 50, 1, 0, 0, 50, 1250, 0);
  CHECK(score.true_positives == 1);
  CHECK(score.denominator == 1);
  CHECK(score.detections.size() == 1);
  CHECK(score.false_alarms == 0);
  CHECK(score.events[0].delay_ms && *score.events[0].delay_ms == 0);

  // A negative sample between two bursts keeps them from inflating into one event match twice.
  std::vector<fb::FrameObs> split = many;
  split[10].positive = false;
  auto split_score = fb::score_observations(labels, split, "full_frame", std::nullopt, 50, 1, 0, 0, 50, 1250, 0);
  CHECK(split_score.true_positives == 1);
  CHECK(split_score.false_alarms == 1);

  auto empty = labels_with({});
  std::vector<fb::FrameObs> blip(1);
  blip[0].index = 64;
  blip[0].pts_ms = 3200;
  blip[0].positive = true;
  blip[0].score = 1;
  auto absent = fb::score_observations(empty, blip, "full_frame", std::nullopt, 200, 1, 0, 0, 200, 1250, 0);
  CHECK(!absent.recall_defined);
  CHECK(absent.denominator == 0);
  CHECK(absent.false_alarms == 1);
  CHECK(absent.true_positives == 0);

  // Sparse 1 Hz hits inside one long event are one true positive.
  std::vector<fb::FrameObs> sparse;
  for (int pts_ms : {6000, 7000, 8000}) {
    fb::FrameObs frame;
    frame.index = pts_ms / 50;
    frame.pts_ms = pts_ms;
    frame.positive = true;
    frame.score = 1;
    sparse.push_back(frame);
  }
  auto two = labels_with({ev("proof-short", 2200, 2400, "short"), ev("proof-long", 6000, 8000, "long")});
  auto uniform = fb::score_observations(two, sparse, "uniform", 1, 200, 1, 0, 0, 10, 1250, 0);
  CHECK(uniform.true_positives == 1);
  CHECK(uniform.denominator == 2);
  CHECK(uniform.missed_event_ids.size() == 1);
  CHECK(uniform.missed_event_ids[0] == "proof-short");
  CHECK(uniform.events[1].matched);
  CHECK(uniform.events[1].delay_ms && *uniform.events[1].delay_ms == 0);
  CHECK(uniform.false_alarms == 0);
  CHECK(uniform.short_tp == 0);
  CHECK(uniform.long_tp == 1);

  // Inclusive boundaries.
  std::vector<fb::FrameObs> edges(2);
  edges[0].index = 0;
  edges[0].pts_ms = 0;
  edges[0].positive = true;
  edges[0].score = 1;
  edges[1].index = 199;
  edges[1].pts_ms = 9950;
  edges[1].positive = true;
  edges[1].score = 1;
  auto edge_labels = labels_with({ev("edge-start", 0, 300, "short"), ev("edge-end", 9700, 9950, "short")});
  auto edge = fb::score_observations(edge_labels, edges, "full_frame", std::nullopt, 200, 1, 0, 0, 2, 1250, 0);
  CHECK(edge.true_positives == 2);
  CHECK(edge.events[0].first_hit_index && *edge.events[0].first_hit_index == 0);
  CHECK(edge.events[1].delay_ms && *edge.events[1].delay_ms == 250);

  // One detection interval cannot satisfy two events.
  std::vector<fb::FrameObs> cover;
  for (int i = 0; i <= 40; ++i) {
    fb::FrameObs frame;
    frame.index = i;
    frame.pts_ms = i * 50;
    frame.positive = true;
    frame.score = 1;
    cover.push_back(frame);
  }
  auto pair = labels_with({ev("a", 0, 100, "short"), ev("b", 200, 400, "short")});
  auto once = fb::score_observations(pair, cover, "full_frame", std::nullopt, 41, 1, 0, 0, 41, 1250, 0);
  CHECK(once.true_positives == 1);
  CHECK(once.false_alarms == 0);
  CHECK(once.missed_event_ids.size() == 1);
}

void test_label_rejection() {
  fb::DetectorConfig detector;
  fb::Limits limits;
  bool threw = false;
  try {
    fb::parse_labels(R"({
      "schema_version": "1.0",
      "clip_id": "overlap",
      "session_id": "session-eval",
      "class": "zone_intrusion",
      "time_unit": "seconds",
      "fps_nominal": 20,
      "events": [
        {"id": "over-a", "start_s": 0.1, "end_s": 0.5, "start_ms": 100, "end_ms": 500, "duration_band": "short"},
        {"id": "over-b", "start_s": 0.4, "end_s": 0.8, "start_ms": 400, "end_ms": 800, "duration_band": "short"}
      ]
    })",
                     detector, limits);
  } catch (const fb::Error& err) {
    threw = true;
    CHECK(err.category == "overlapping_labels");
    CHECK(std::string(err.what()).find("over-a") != std::string::npos);
  }
  CHECK(threw);

  threw = false;
  try {
    fb::parse_labels(R"({
      "schema_version": "1.0",
      "clip_id": "bad",
      "session_id": "session-eval",
      "class": "person",
      "fps_nominal": 20,
      "events": []
    })",
                     detector, limits);
  } catch (const fb::Error& err) {
    threw = true;
    CHECK(err.category == "unsupported_class");
  }
  CHECK(threw);
}

void test_fixtures() {
  const auto manifest = read_json(fixture("manifest.json"));
  const auto proof_sha = fb::sha256_file(fixture("staged/proof-10s/clip.mp4"));
  bool found = false;
  for (const auto& clip : manifest.at("clips")) {
    if (clip.at("id") == "proof-10s") {
      found = true;
      CHECK(clip.at("sha256").get<std::string>() == proof_sha);
    }
  }
  CHECK(found);

  fb::DetectorConfig detector = fb::load_detector(fixture("detector.json"));
  CHECK(detector.threshold_session == "session-tune");
  CHECK(detector.fraction_threshold == 0.5);
  fb::Limits limits;
  limits.max_zone_bytes = detector.zone_buffer_bytes;

  std::vector<fb::PolicyRequest> jobs = {{"full_frame", std::nullopt}, {"uniform", 1}, {"uniform", 2},
                                         {"uniform", 5},           {"motion", 1}, {"motion", 2},
                                         {"motion", 5}};
  fb::ClipEval proof = fb::evaluate_clip(fixture("staged/proof-10s/clip.mp4"), fixture("staged/proof-10s/labels.json"),
                                         fixture("staged/proof-10s/timestamps.json"), detector, limits, jobs, nullptr);
  const auto expected = read_json(fixture("staged/proof-10s/expected.json"));
  auto as_json = [&](const fb::ClipEval& clip) {
    nlohmann::json policies = nlohmann::json::array();
    // Local projection matching the fields the oracle cares about.
    for (const fb::PolicyScore& policy : clip.policies) {
      nlohmann::json missed = nlohmann::json::array();
      for (const auto& id : policy.missed_event_ids) missed.push_back(id);
      policies.push_back({{"policy", policy.policy},
                          {"budget_hz", policy.budget_hz ? nlohmann::json(*policy.budget_hz) : nlohmann::json(nullptr)},
                          {"decoded_frames", policy.decoded_frames},
                          {"detector_calls", policy.detector_calls},
                          {"motion_checks", policy.motion_checks},
                          {"true_positives", policy.true_positives},
                          {"denominator", policy.denominator},
                          {"false_alarms", policy.false_alarms},
                          {"missed_event_ids", missed},
                          {"events", [&] {
                             nlohmann::json events = nlohmann::json::array();
                             for (const auto& event : policy.events) {
                               nlohmann::json row = {{"id", event.id}, {"matched", event.matched}, {"outcome", event.outcome}};
                               if (event.delay_ms) row["delay_ms"] = *event.delay_ms;
                               if (event.first_hit_ms) row["first_hit_ms"] = *event.first_hit_ms;
                               if (event.first_hit_index) row["first_hit_index"] = *event.first_hit_index;
                               events.push_back(row);
                             }
                             return events;
                           }()}});
    }
    return policies;
  };
  const auto policies = as_json(proof);
  const auto& full_row = policy_named(policies, "full_frame", -1);
  const auto& exp_full = expected.at("full_frame");
  CHECK(full_row.at("decoded_frames") == exp_full.at("decoded_frames"));
  CHECK(full_row.at("detector_calls") == exp_full.at("detector_calls"));
  CHECK(full_row.at("motion_checks") == exp_full.at("motion_checks"));
  CHECK(full_row.at("true_positives") == exp_full.at("true_positives"));
  CHECK(full_row.at("denominator") == exp_full.at("denominator"));
  CHECK(full_row.at("false_alarms") == exp_full.at("false_alarms"));
  CHECK(full_row.at("missed_event_ids").empty());
  CHECK(full_row.at("events")[0].at("delay_ms") == exp_full.at("proof_short_delay_ms"));
  CHECK(full_row.at("events")[1].at("delay_ms") == exp_full.at("proof_long_delay_ms"));

  const auto& uni = policy_named(policies, "uniform", 1);
  const auto& exp_uni = expected.at("uniform_1");
  CHECK(uni.at("decoded_frames") == exp_uni.at("decoded_frames"));
  CHECK(uni.at("detector_calls") == exp_uni.at("detector_calls"));
  CHECK(uni.at("true_positives") == exp_uni.at("true_positives"));
  CHECK(uni.at("missed_event_ids") == exp_uni.at("missed_event_ids"));
  CHECK(uni.at("events")[0].at("outcome") == "sampling_miss");
  CHECK(uni.at("events")[1].at("delay_ms") == exp_uni.at("proof_long_delay_ms"));
  CHECK(uni.at("events")[1].at("first_hit_ms") == exp_uni.at("proof_long_first_hit_ms"));
  CHECK(uni.at("decoded_frames") == full_row.at("decoded_frames"));
  CHECK(uni.at("detector_calls").get<int>() < full_row.at("detector_calls").get<int>());

  const auto& motion = policy_named(policies, "motion", 1);
  CHECK(motion.at("decoded_frames") == 200);
  CHECK(motion.at("motion_checks") == 199);
  CHECK(motion.at("true_positives") == 2);
  CHECK(motion.at("missed_event_ids").empty());
  CHECK(motion.at("detector_calls").get<int>() < uni.at("detector_calls").get<int>());
  CHECK(motion.at("events")[0].at("outcome") == "hit");

  fb::ClipEval absent =
      fb::evaluate_clip(fixture("staged/absent-false/clip.mp4"), fixture("staged/absent-false/labels.json"),
                        fixture("staged/absent-false/timestamps.json"), detector, limits, {{"full_frame", std::nullopt}},
                        nullptr);
  CHECK(absent.policies[0].denominator == 0);
  CHECK(!absent.policies[0].recall_defined);
  CHECK(absent.policies[0].false_alarms == 1);

  fb::ClipEval variable = fb::evaluate_clip(
      fixture("staged/variable-timing/clip.mp4"), fixture("staged/variable-timing/labels.json"),
      fixture("staged/variable-timing/timestamps.json"), detector, limits, {{"full_frame", std::nullopt}}, nullptr);
  CHECK(variable.frames_missing_timestamp == 1);
  CHECK(variable.timestamp_source == "session_log");
  CHECK(variable.policies[0].true_positives == 2);
  const auto& long_event = variable.policies[0].events[1];
  CHECK(long_event.id == "var-long");
  CHECK(long_event.first_hit_index && *long_event.first_hit_index == 7);
  CHECK(long_event.first_hit_ms && *long_event.first_hit_ms == 2500);
  CHECK(long_event.delay_ms && *long_event.delay_ms == 0);

  const auto dataset = fb::load_dataset(fixture("datasets/staged.json"), fixture(""));
  CHECK(dataset.clips.size() == 5);
  for (const auto& clip : dataset.clips) CHECK(clip.id != "tune-red");

  const std::string bad_dataset = "/tmp/framebudget-bad-dataset.json";
  {
    std::ofstream out(bad_dataset);
    out << R"({"schema_version":"1.0","name":"bad","clips":[{"id":"x","media":"../secret.mp4","labels":"a.json","timestamps":"b.json"}]})";
  }
  bool threw = false;
  try {
    fb::load_dataset(bad_dataset, fixture(""));
  } catch (const fb::Error& err) {
    threw = true;
    CHECK(err.category == "invalid_labels");
  }
  CHECK(threw);
}

void test_cli() {
  std::string out;
  std::string err;
  const std::string root = FRAMEBUDGET_FIXTURES;
  int code = call_cli({"framebudget", "validate", "--data", root, "--media", fixture("staged/proof-10s/clip.mp4"),
                       "--labels", fixture("staged/proof-10s/labels.json"), "--timestamps",
                       fixture("staged/proof-10s/timestamps.json")},
                      out, err);
  CHECK(code == 0);
  auto ok = nlohmann::json::parse(out);
  CHECK(ok.at("status") == "ok");
  CHECK(ok.at("frames") == 200);
  CHECK(ok.at("events") == 2);

  code = call_cli({"framebudget", "validate", "--data", root, "--media", fixture("invalid/overlap/clip.mp4"), "--labels",
                   fixture("invalid/overlap/labels.json"), "--timestamps", fixture("invalid/overlap/timestamps.json")},
                  out, err);
  CHECK(code == 3);
  auto overlap = parse_last_json(err);
  CHECK(overlap.at("category") == "overlapping_labels");
  CHECK(overlap.at("message").get<std::string>().find("over-a") != std::string::npos);

  code = call_cli({"framebudget", "validate", "--data", root, "--media", fixture("invalid/truncated.mp4"), "--labels",
                   fixture("staged/proof-10s/labels.json")},
                  out, err);
  CHECK(code == 3);
  auto broken = parse_last_json(err);
  CHECK(broken.at("category") == "unreadable_media");

  code = call_cli({"framebudget", "validate", "--data", root, "--max-bytes", "100", "--media",
                   fixture("staged/proof-10s/clip.mp4"), "--labels", fixture("staged/proof-10s/labels.json"),
                   "--timestamps", fixture("staged/proof-10s/timestamps.json")},
                  out, err);
  CHECK(code == 3);
  auto oversized = parse_last_json(err);
  CHECK(oversized.at("category") == "oversized_media");

  code = call_cli({"framebudget", "validate", "--data", root, "--media", fixture("invalid/overlap/clip.mp4"), "--labels",
                   fixture("invalid/bad-class/labels.json")},
                  out, err);
  CHECK(code == 3);
  CHECK(parse_last_json(err).at("category") == "unsupported_class");

  code = call_cli({"framebudget", "eval", "--data", root, "--policy", "full", "--media", fixture("invalid/truncated.mp4"),
                   "--labels", fixture("staged/proof-10s/labels.json")},
                  out, err);
  CHECK(code == 3);
  CHECK(parse_last_json(err).at("category") == "unreadable_media");

  const auto started = std::chrono::steady_clock::now();
  const std::string report_dir = "out/unit-demo";
  std::filesystem::remove_all(report_dir);
  code = call_cli({"framebudget", "compare", "--data", root, "--dataset", "staged", "--budgets", "1,2,5", "--output",
                   report_dir, "--score-trace"},
                  out, err);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  CHECK(code == 0);
  CHECK(elapsed < std::chrono::seconds(5));
  auto summary = nlohmann::json::parse(out);
  CHECK(summary.at("status") == "complete");
  CHECK(summary.at("recommendation").at("available") == true);
  CHECK(summary.at("recommendation").at("policy") == "motion");
  CHECK(summary.at("recommendation").at("budget_hz") == 1);
  CHECK(summary.at("recommendation").at("max_misses") == 0);
  CHECK(summary.at("recommendation").at("missed_events") == 0);
  CHECK(summary.at("recommendation").at("reason").get<std::string>().find("threshold of 0") != std::string::npos);

  const auto report = read_json(report_dir + "/report.json");
  bool saw_proof = false;
  int full_false = 0;
  int full_den = 0;
  int full_tp = 0;
  for (const auto& clip : report.at("clips")) {
    if (clip.at("clip_id") == "proof-10s") {
      saw_proof = true;
      const auto& uni = policy_named(clip.at("policies"), "uniform", 1);
      CHECK(uni.at("missed_event_ids")[0] == "proof-short");
      CHECK(clip.at("sha256").get<std::string>().size() == 64);
    }
    if (clip.at("session_id") != "session-eval") CHECK(false);
    const auto& full_row = policy_named(clip.at("policies"), "full_frame", -1);
    full_false += full_row.at("false_alarms").get<int>();
    full_den += full_row.at("denominator").get<int>();
    full_tp += full_row.at("true_positives").get<int>();
  }
  CHECK(saw_proof);
  CHECK(full_tp == 6);
  CHECK(full_den == 6);
  CHECK(full_false == 2);
  const std::string html = [&] {
    std::ifstream in(report_dir + "/report.html");
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
  }();
  CHECK(html.find("proof-short") != std::string::npos);
  CHECK(html.find("undefined") != std::string::npos);
  CHECK(html.find("not a protective or operational control") != std::string::npos);
  CHECK(html.find("sampling_miss") != std::string::npos);
  CHECK(std::filesystem::exists(report_dir + "/scores.jsonl"));
  CHECK(err.find("calls") != std::string::npos);
  CHECK(out.find("\"status\"") != std::string::npos);
}

}  // namespace

int main() {
  try {
    test_sha_and_schedule();
    test_scorer_contracts();
    test_label_rejection();
    test_fixtures();
    test_cli();
  } catch (const std::exception& err) {
    std::cerr << "exception: " << err.what() << "\n";
    return 1;
  }
  if (g_failures != 0) {
    std::cerr << g_failures << " checks failed\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}
