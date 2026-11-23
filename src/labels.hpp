// SPDX-License-Identifier: MIT
#pragma once

#include "types.hpp"

#include <string>

namespace fb {

DetectorConfig load_detector(const std::string& path);
LabelSet load_labels(const std::string& path, const DetectorConfig& detector, const Limits& limits);
LabelSet parse_labels(const std::string& text, const DetectorConfig& detector, const Limits& limits);
TimestampLog load_timestamps(const std::string& path, const Limits& limits);
TimestampLog parse_timestamps(const std::string& text);

}  // namespace fb
