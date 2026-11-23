// SPDX-License-Identifier: MIT
#pragma once

#include "types.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fb {

struct PolicyRequest {
  std::string policy;
  std::optional<int> budget_hz;
};

ClipEval evaluate_clip(const std::string& media, const std::string& labels_path, const std::string& timestamps_path,
                       const DetectorConfig& detector, const Limits& limits, const std::vector<PolicyRequest>& policies,
                       std::vector<TraceRow>* trace);

struct DatasetClip {
  std::string id;
  std::string media;
  std::string labels;
  std::string timestamps;
};

struct Dataset {
  std::string name;
  std::vector<DatasetClip> clips;
};

Dataset load_dataset(const std::string& path, const std::string& root);

}  // namespace fb
