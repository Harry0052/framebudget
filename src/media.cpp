// SPDX-License-Identifier: MIT
#include "media.hpp"

#include "error.hpp"
#include "sha256.hpp"

#include <opencv2/core/utils/logger.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cmath>
#include <filesystem>

#include <cstdarg>

extern "C" {
void av_log_set_callback(void (*callback)(void*, int, const char*, va_list));
}

void silent_av_log(void*, int, const char*, va_list) {}

namespace fb {
namespace {

void quiet_decoder_logs() {
  cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_SILENT);
  // The ffmpeg backend writes unreadable-file errors straight to stderr. A silent
  // callback keeps the CLI error JSON as the only diagnostic.
  av_log_set_callback(silent_av_log);
}

cv::Mat to_bgr(const cv::Mat& frame, const std::string& path) {
  if (frame.empty()) {
    throw Error("unreadable_media", path + " produced an empty frame.");
  }
  if (frame.type() == CV_8UC3) return frame;
  cv::Mat bgr;
  if (frame.type() == CV_8UC1) {
    cv::cvtColor(frame, bgr, cv::COLOR_GRAY2BGR);
    return bgr;
  }
  if (frame.type() == CV_8UC4) {
    cv::cvtColor(frame, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
  }
  throw Error("unreadable_media", path + " has a pixel type this build does not score.");
}

}  // namespace

double zone_fraction(const cv::Mat& zone, const DetectorConfig& detector) {
  if (zone.empty() || zone.type() != CV_8UC3) {
    throw Error("internal", "zone crop is not an 8-bit BGR image.");
  }
  int hits = 0;
  const int total = zone.rows * zone.cols;
  if (total <= 0) throw Error("internal", "zone crop has no pixels.");
  for (int y = 0; y < zone.rows; ++y) {
    const auto* row = zone.ptr<cv::Vec3b>(y);
    for (int x = 0; x < zone.cols; ++x) {
      const int b = row[x][0];
      const int g = row[x][1];
      const int r = row[x][2];
      if (r >= detector.r_min && g <= detector.g_max && b <= detector.b_max) hits += 1;
    }
  }
  return static_cast<double>(hits) / static_cast<double>(total);
}

double zone_mad(const cv::Mat& previous, const cv::Mat& current) {
  if (previous.size() != current.size() || previous.type() != CV_8UC3 || current.type() != CV_8UC3) {
    throw Error("internal", "motion difference requires two equal BGR crops.");
  }
  double sum = 0;
  const int pixels = previous.rows * previous.cols;
  for (int y = 0; y < previous.rows; ++y) {
    const auto* a = previous.ptr<cv::Vec3b>(y);
    const auto* b = current.ptr<cv::Vec3b>(y);
    for (int x = 0; x < previous.cols; ++x) {
      sum += std::abs(a[x][0] - b[x][0]);
      sum += std::abs(a[x][1] - b[x][1]);
      sum += std::abs(a[x][2] - b[x][2]);
    }
  }
  return sum / static_cast<double>(pixels * 3);
}

DecodedClip decode_clip(const std::string& path, const TimestampLog* timestamps, const DetectorConfig& detector,
                        const Limits& limits, double fps_nominal) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec)) {
    throw Error("unreadable_media",
                "Could not decode " + path + ". The path is not a file. Pass an MP4 this OpenCV build can open.");
  }
  const auto bytes = fs::file_size(path, ec);
  if (ec) {
    throw Error("unreadable_media", "Could not read the size of " + path + ".");
  }
  if (bytes > limits.max_bytes) {
    throw Error("oversized_media", path + " is " + std::to_string(bytes) + " bytes, over the limit of " +
                                       std::to_string(limits.max_bytes) +
                                       " bytes. The default limit is 8388608. Raise --max-bytes only for a file you already trust.");
  }
  if (bytes == 0) {
    throw Error("unreadable_media", path + " is empty. Pass an MP4 this OpenCV build can open.");
  }

  quiet_decoder_logs();
  // CAP_FFMPEG only. The default opener also probes camera backends such as
  // gphoto, which is outside this file-based tool and leaks under ASan.
  cv::VideoCapture cap(path, cv::CAP_FFMPEG);
  if (!cap.isOpened()) {
    throw Error("unreadable_media", "Could not decode " + path +
                                        ". The file is missing a readable video stream. Pass an MP4 this OpenCV build can open.");
  }

  DecodedClip clip;
  clip.decoder = cv::getVersionString();
  clip.backend = cap.getBackendName();
  clip.container_fps = cap.get(cv::CAP_PROP_FPS);
  clip.sha256 = sha256_file(path);
  clip.timestamp_source = timestamps != nullptr ? timestamps->source : "nominal_fps";

  cv::Mat frame;
  std::uintmax_t zone_bytes = 0;
  while (cap.read(frame)) {
    if (clip.zones.empty()) {
      clip.width = frame.cols;
      clip.height = frame.rows;
      if (clip.width > limits.max_width || clip.height > limits.max_height) {
        throw Error("oversized_media", path + " is " + std::to_string(clip.width) + "x" +
                                           std::to_string(clip.height) + ", over the limit of " +
                                           std::to_string(limits.max_width) + "x" + std::to_string(limits.max_height) +
                                           ".");
      }
      if (detector.zone_x + detector.zone_w > clip.width || detector.zone_y + detector.zone_h > clip.height) {
        throw Error("invalid_labels", "detector zone does not fit in " + path + " (" + std::to_string(clip.width) +
                                          "x" + std::to_string(clip.height) + ").");
      }
    } else if (frame.cols != clip.width || frame.rows != clip.height) {
      throw Error("unreadable_media", path + " changed frame size mid-clip. This release scores one fixed camera.");
    }
    if (static_cast<int>(clip.zones.size()) >= limits.max_frames) {
      throw Error("oversized_media", path + " has more than " + std::to_string(limits.max_frames) +
                                         " frames. Shorten the clip or raise --max-frames for a file you already trust.");
    }
    cv::Mat bgr = to_bgr(frame, path);
    cv::Mat zone = bgr(cv::Rect(detector.zone_x, detector.zone_y, detector.zone_w, detector.zone_h)).clone();
    zone_bytes += static_cast<std::uintmax_t>(zone.total() * zone.elemSize());
    if (zone_bytes > limits.max_zone_bytes || zone_bytes > detector.zone_buffer_bytes) {
      throw Error("oversized_media",
                  "The in-zone frame buffer for " + path +
                      " exceeded its cap. Use a smaller marked zone or a shorter clip. This cap keeps decode memory bounded.");
    }
    clip.zones.push_back(std::move(zone));
  }
  if (clip.zones.empty()) {
    throw Error("unreadable_media", "Could not decode " + path +
                                        ". OpenCV returned no frames. Pass an MP4 this OpenCV build can open.");
  }

  clip.pts_ms.assign(clip.zones.size(), -1);
  if (timestamps != nullptr) {
    if (timestamps->pts_ms.size() != clip.zones.size()) {
      throw Error("invalid_timestamps", "timestamp log has " + std::to_string(timestamps->pts_ms.size()) +
                                            " entries but " + path + " decoded " + std::to_string(clip.zones.size()) +
                                            " frames. Regenerate the log so one entry matches each decoded frame.");
    }
    for (std::size_t i = 0; i < timestamps->pts_ms.size(); ++i) {
      if (!timestamps->pts_ms[i]) {
        clip.missing += 1;
        continue;
      }
      clip.pts_ms[i] = *timestamps->pts_ms[i];
      clip.timeline_end_ms = std::max(clip.timeline_end_ms, clip.pts_ms[i]);
    }
  } else {
    const double fps = clip.container_fps > 1.0 ? clip.container_fps : fps_nominal;
    if (!(fps > 0.0)) {
      throw Error("invalid_timestamps",
                  "No timestamp log was provided and the container has no usable frame rate. Pass --timestamps.");
    }
    for (std::size_t i = 0; i < clip.zones.size(); ++i) {
      clip.pts_ms[i] = static_cast<int>(std::llround(static_cast<double>(i) * 1000.0 / fps));
      clip.timeline_end_ms = std::max(clip.timeline_end_ms, clip.pts_ms[i]);
    }
  }
  if (clip.timeline_end_ms > limits.max_duration_ms) {
    throw Error("oversized_media", "The last presentation timestamp is " + std::to_string(clip.timeline_end_ms) +
                                       " ms, over the limit of " + std::to_string(limits.max_duration_ms) + " ms.");
  }
  return clip;
}

}  // namespace fb
