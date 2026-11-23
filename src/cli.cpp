// SPDX-License-Identifier: MIT
#include "cli.hpp"

#include "error.hpp"
#include "evaluate.hpp"
#include "labels.hpp"
#include "media.hpp"
#include "report.hpp"
#include "sha256.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifndef FRAMEBUDGET_VERSION
#define FRAMEBUDGET_VERSION "0.1.0"
#endif

namespace fb {
namespace {

struct Args {
  std::vector<std::string> positionals;
  std::vector<std::pair<std::string, std::string>> options;
  bool flag(const std::string& name) const {
    for (const auto& opt : options) {
      if (opt.first == name && opt.second.empty()) return true;
    }
    return false;
  }
  std::optional<std::string> get(const std::string& name) const {
    for (const auto& opt : options) {
      if (opt.first == name) return opt.second;
    }
    return std::nullopt;
  }
};

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string token = argv[i];
    if (token == "--help" || token == "-h" || token == "--version" || token == "--score-trace") {
      args.options.emplace_back(token, "");
      continue;
    }
    if (token.rfind("--", 0) == 0) {
      if (i + 1 >= argc) throw Error("usage", "missing value for " + token);
      args.options.emplace_back(token, argv[++i]);
      continue;
    }
    args.positionals.push_back(token);
  }
  return args;
}

int require_int(const std::string& text, const std::string& flag) {
  try {
    std::size_t idx = 0;
    const int value = std::stoi(text, &idx);
    if (idx != text.size()) throw std::invalid_argument("tail");
    return value;
  } catch (const Error&) {
    throw;
  } catch (const std::exception&) {
    throw Error("usage", flag + " expects an integer, got \"" + text + "\".");
  }
}

std::vector<int> parse_budgets(const std::string& text) {
  std::vector<int> budgets;
  std::stringstream stream(text);
  std::string part;
  while (std::getline(stream, part, ',')) {
    if (part.empty()) continue;
    const int value = require_int(part, "--budgets");
    if (value < 1) throw Error("usage", "each budget must be a positive integer, for example 1,2,5.");
    budgets.push_back(value);
  }
  if (budgets.empty()) throw Error("usage", "--budgets needs at least one positive integer, for example 1,2,5.");
  return budgets;
}

std::filesystem::path data_root(const Args& args) {
  if (const auto data = args.get("--data")) return *data;
  if (std::filesystem::exists("fixtures/detector.json")) return "fixtures";
  throw Error("usage",
              "Pass --data PATH pointing at the fixtures directory, or run from the repository root so ./fixtures is found.");
}

Limits limits_from(const Args& args, const DetectorConfig& detector) {
  Limits limits;
  limits.max_zone_bytes = detector.zone_buffer_bytes;
  if (const auto bytes = args.get("--max-bytes")) limits.max_bytes = static_cast<std::uintmax_t>(require_int(*bytes, "--max-bytes"));
  if (const auto frames = args.get("--max-frames")) limits.max_frames = require_int(*frames, "--max-frames");
  if (limits.max_bytes < 1 || limits.max_frames < 1) {
    throw Error("usage", "--max-bytes and --max-frames must be positive.");
  }
  return limits;
}

void write_under(const std::filesystem::path& directory, const std::string& name, const std::string& body) {
  if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos || name.find("..") != std::string::npos) {
    throw Error("internal", "refusing to write a report outside the output directory.");
  }
  std::filesystem::create_directories(directory);
  const std::filesystem::path dest = directory / name;
  std::ofstream out(dest);
  if (!out) throw Error("io_error", "Could not write " + dest.string() + ". Check that the output directory is writable.");
  out << body;
}

std::string help_text() {
  return
      "framebudget " FRAMEBUDGET_VERSION
      "\n"
      "Compare frame budgets against short staged zone-intrusion events.\n"
      "This scores synthetic clips only. It is not a safety control and it does not detect real emergencies.\n"
      "\n"
      "Usage\n"
      "  framebudget validate --media CLIP --labels LABELS [--timestamps LOG] [--data DIR]\n"
      "  framebudget eval --media CLIP --labels LABELS --policy full|uniform|motion [--budget N]\n"
      "                   [--timestamps LOG] [--output DIR] [--score-trace]\n"
      "  framebudget compare --dataset staged [--budgets 1,2,5] [--data DIR] [--output DIR]\n"
      "                      [--max-misses 0] [--score-trace]\n"
      "  framebudget --help\n"
      "  framebudget --version\n"
      "\n"
      "From the repository root the staged demo is:\n"
      "  framebudget compare --dataset staged --budgets 1,2,5 --output out/demo\n"
      "\n"
      "Stdout is JSON. Diagnostics and the text table go to stderr. Exit 0 is success,\n"
      "2 is usage, 3 is a rejected input, 4 is an unexpected failure.\n"
      "\n"
      "Limits\n"
      "  --max-bytes N     reject media larger than N bytes (default 8388608)\n"
      "  --max-frames N    reject clips longer than N frames (default 3000)\n"
      "  --max-misses N    recommendation threshold, counted on the staged set (default 0)\n";
}

void print_table(const nlohmann::json& aggregate) {
  std::cerr << "policy       budget  decoded  motion  calls  recall      false  sampling_miss  detector_miss\n";
  for (const auto& row : aggregate) {
    std::string recall = "undefined";
    if (row.at("recall_defined").get<bool>()) {
      recall = std::to_string(row.at("true_positives").get<int>()) + "/" + std::to_string(row.at("denominator").get<int>());
    }
    std::string budget = "all";
    if (!row.at("budget_hz").is_null()) budget = std::to_string(row.at("budget_hz").get<int>());
    std::cerr << row.at("policy").get<std::string>() << "  " << budget << "  decoded " << row.at("decoded_frames").get<int>()
              << "  motion " << row.at("motion_checks").get<int>() << "  calls " << row.at("detector_calls").get<int>()
              << "  recall " << recall << "  false " << row.at("false_alarms").get<int>() << "  sampling_miss "
              << row.at("sampling_misses").get<int>() << "  detector_miss " << row.at("detector_misses").get<int>() << "\n";
  }
}

int cmd_validate(const Args& args) {
  const auto media = args.get("--media");
  const auto labels = args.get("--labels");
  if (!media || !labels) throw Error("usage", "validate needs --media and --labels.");
  const auto root = data_root(args);
  const DetectorConfig detector = load_detector((root / "detector.json").string());
  const Limits limits = limits_from(args, detector);
  const std::string timestamps = args.get("--timestamps").value_or("");
  ClipEval eval = evaluate_clip(*media, *labels, timestamps, detector, limits, {{"full_frame", std::nullopt}}, nullptr);
  // validate's job is the contract check. A successful full-frame pass means the
  // media decoded, the labels matched the schema, and the timestamps lined up.
  nlohmann::json out = {{"status", "ok"},
                        {"clip_id", eval.clip_id},
                        {"session_id", eval.session_id},
                        {"frames", eval.frames},
                        {"events", static_cast<int>(eval.labels.events.size())},
                        {"sha256", eval.sha256},
                        {"width", eval.width},
                        {"height", eval.height},
                        {"decoder", eval.decoder},
                        {"backend", eval.backend},
                        {"timestamp_source", eval.timestamp_source},
                        {"frames_missing_timestamp", eval.frames_missing_timestamp}};
  std::cout << out.dump(2) << "\n";
  return 0;
}

int cmd_eval(const Args& args) {
  const auto media = args.get("--media");
  const auto labels = args.get("--labels");
  const auto policy_name = args.get("--policy");
  if (!media || !labels || !policy_name) {
    throw Error("usage", "eval needs --media, --labels, and --policy full|uniform|motion.");
  }
  std::string policy = *policy_name;
  if (policy == "full") policy = "full_frame";
  const auto root = data_root(args);
  const DetectorConfig detector = load_detector((root / "detector.json").string());
  const Limits limits = limits_from(args, detector);
  std::vector<PolicyRequest> jobs = {{"full_frame", std::nullopt}};
  if (policy != "full_frame") {
    if (!args.get("--budget")) throw Error("usage", "uniform and motion need --budget.");
    jobs.push_back(PolicyRequest{policy, require_int(*args.get("--budget"), "--budget")});
  } else if (args.get("--budget")) {
    throw Error("usage", "full frame does not take --budget. The reference runs on every timestamped frame.");
  }
  std::vector<TraceRow> trace;
  const bool want_trace = args.flag("--score-trace");
  ClipEval eval = evaluate_clip(*media, *labels, args.get("--timestamps").value_or(""), detector, limits, jobs,
                                want_trace ? &trace : nullptr);
  nlohmann::json machine = clip_json(eval);
  machine["status"] = "complete";
  machine["tool_version"] = FRAMEBUDGET_VERSION;
  if (const auto output = args.get("--output")) {
    write_under(*output, "eval.json", machine.dump(2) + "\n");
    if (want_trace) {
      std::ostringstream lines;
      for (const TraceRow& row : trace) {
        nlohmann::json line = {{"clip_id", row.clip_id},
                               {"policy", row.policy},
                               {"budget_hz", row.budget_hz ? nlohmann::json(*row.budget_hz) : nlohmann::json(nullptr)},
                               {"index", row.index},
                               {"pts_ms", row.pts_ms},
                               {"score", row.score},
                               {"positive", row.positive}};
        lines << line.dump() << "\n";
      }
      write_under(*output, "scores.jsonl", lines.str());
    }
    std::cerr << "wrote " << (std::filesystem::path(*output) / "eval.json").string() << "\n";
  } else if (want_trace) {
    throw Error("usage", "--score-trace writes scores.jsonl under --output. Pass --output DIR.");
  }
  std::cout << machine.dump(2) << "\n";
  return 0;
}

int cmd_compare(const Args& args) {
  const std::string dataset_name = args.get("--dataset").value_or("staged");
  const auto root = data_root(args);
  const DetectorConfig detector = load_detector((root / "detector.json").string());
  const Limits limits = limits_from(args, detector);
  const auto dataset = load_dataset((root / "datasets" / (dataset_name + ".json")).string(), root.string());
  const std::vector<int> budgets = parse_budgets(args.get("--budgets").value_or("1,2,5"));
  const int max_misses = args.get("--max-misses") ? require_int(*args.get("--max-misses"), "--max-misses") : 0;
  if (max_misses < 0) throw Error("usage", "--max-misses must be >= 0.");

  std::vector<PolicyRequest> jobs = {{"full_frame", std::nullopt}};
  for (int budget : budgets) jobs.push_back(PolicyRequest{"uniform", budget});
  for (int budget : budgets) jobs.push_back(PolicyRequest{"motion", budget});

  std::vector<TraceRow> trace;
  const bool want_trace = args.flag("--score-trace");
  std::vector<ClipEval> clips;
  for (const DatasetClip& clip : dataset.clips) {
    clips.push_back(evaluate_clip(clip.media, clip.labels, clip.timestamps, detector, limits, jobs,
                                  want_trace ? &trace : nullptr));
  }
  CompareResult result = build_comparison(dataset.name, clips, detector, max_misses, FRAMEBUDGET_VERSION);
  const std::filesystem::path output = args.get("--output").value_or("out/framebudget");
  write_under(output, "report.json", result.machine.dump(2) + "\n");
  write_under(output, "report.html", result.html);
  if (want_trace) {
    std::ostringstream lines;
    for (const TraceRow& row : trace) {
      nlohmann::json line = {{"clip_id", row.clip_id},
                             {"policy", row.policy},
                             {"budget_hz", row.budget_hz ? nlohmann::json(*row.budget_hz) : nlohmann::json(nullptr)},
                             {"index", row.index},
                             {"pts_ms", row.pts_ms},
                             {"score", row.score},
                             {"positive", row.positive}};
      lines << line.dump() << "\n";
    }
    write_under(output, "scores.jsonl", lines.str());
  }
  print_table(result.machine.at("aggregate"));
  std::cerr << result.recommendation.reason << "\n";
  std::cerr << "wrote " << (output / "report.json").string() << " and " << (output / "report.html").string() << "\n";

  nlohmann::json summary = {{"status", "complete"},
                            {"dataset", dataset.name},
                            {"report_json", (output / "report.json").string()},
                            {"report_html", (output / "report.html").string()},
                            {"recommendation", result.machine.at("recommendation")},
                            {"aggregate", result.machine.at("aggregate")}};
  std::cout << summary.dump(2) << "\n";
  return 0;
}

}  // namespace

int run_cli(int argc, char** argv) {
  try {
    const Args args = parse_args(argc, argv);
    if (args.flag("--help") || args.flag("-h") || (args.positionals.empty() && !args.flag("--version"))) {
      std::cout << help_text();
      return args.positionals.empty() && !args.flag("--help") && !args.flag("-h") && !args.flag("--version") ? 2 : 0;
    }
    if (args.flag("--version")) {
      std::cout << "framebudget " << FRAMEBUDGET_VERSION << "\n";
      return 0;
    }
    const std::string command = args.positionals.front();
    if (args.positionals.size() != 1) {
      throw Error("usage", "unexpected extra argument \"" + args.positionals[1] + "\". See framebudget --help.");
    }
    if (command == "validate") return cmd_validate(args);
    if (command == "eval") return cmd_eval(args);
    if (command == "compare") return cmd_compare(args);
    throw Error("usage", "unknown command \"" + command + "\". See framebudget --help.");
  } catch (const Error& err) {
    nlohmann::json out = {{"status", "error"}, {"category", err.category}, {"message", err.what()}};
    std::cerr << out.dump(2) << "\n";
    if (err.category == "usage") return 2;
    if (err.category == "internal") return 4;
    return 3;
  } catch (const std::exception& err) {
    nlohmann::json out = {{"status", "error"}, {"category", "internal"}, {"message", err.what()}};
    std::cerr << out.dump(2) << "\n";
    return 4;
  }
}

}  // namespace fb
