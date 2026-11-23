// SPDX-License-Identifier: MIT
#pragma once

#include "types.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace fb {

struct CompareResult {
  nlohmann::json machine;
  std::string html;
  Recommendation recommendation;
};

nlohmann::json policy_json(const PolicyScore& policy);
nlohmann::json clip_json(const ClipEval& clip);

CompareResult build_comparison(const std::string& dataset, const std::vector<ClipEval>& clips, const DetectorConfig& detector,
                               int max_misses, const std::string& tool_version);

std::string render_html(const nlohmann::json& machine);

}  // namespace fb
