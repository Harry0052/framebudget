// SPDX-License-Identifier: MIT
#pragma once

#include "types.hpp"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace fb {

struct DecodedClip {
  int width = 0;
  int height = 0;
  double container_fps = 0;
  std::string backend;
  std::string decoder;
  std::string sha256;
  std::string timestamp_source;
  int missing = 0;
  int timeline_end_ms = 0;
  std::vector<int> pts_ms;  // -1 when the presentation timestamp is missing
  std::vector<cv::Mat> zones;
};

DecodedClip decode_clip(const std::string& path, const TimestampLog* timestamps, const DetectorConfig& detector,
                        const Limits& limits, double fps_nominal);

double zone_fraction(const cv::Mat& zone, const DetectorConfig& detector);
double zone_mad(const cv::Mat& previous, const cv::Mat& current);

}  // namespace fb
