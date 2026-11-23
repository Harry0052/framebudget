// SPDX-License-Identifier: MIT
#pragma once

#include "types.hpp"

#include <vector>

namespace fb {

// Nearest timestamped frame to each k / budget_hz second, ties to the earlier index.
// pts_ms uses -1 for a missing timestamp. Those frames are never selected.
std::vector<char> uniform_mask(const std::vector<int>& pts_ms, int budget_hz);

// First timestamped frame is always kept and does not consume the per-second budget.
// Later frames need motion >= threshold. At most budget_hz of those are kept per
// second, highest motion first, earlier index on a tie.
std::vector<int> select_motion(const std::vector<MotionObs>& frames, int budget_hz, double threshold);

PolicyScore score_observations(const LabelSet& labels, const std::vector<FrameObs>& ran, const std::string& policy,
                               std::optional<int> budget_hz, int decoded_frames, int decode_passes,
                               int frames_missing_timestamp, int motion_checks, int detector_calls, int gap_ms,
                               int tolerance_ms);

// Split misses into detector misses (full frame also missed) and sampling misses.
void annotate_outcomes(std::vector<PolicyScore>& policies);

}  // namespace fb
