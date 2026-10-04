#pragma once

#include <cstddef>

namespace glo {

struct PathStats {
    double rtt_ms{0.0};
    double jitter_ms{0.0};
    double loss_pct{100.0};
    int sent{0};
    int received{0};

    bool valid() const noexcept { return sent > 0 && received > 0 && rtt_ms > 0.0; }
};

enum class PathChoice { Insufficient, Direct, Relay };

double path_score(const PathStats& stats,
                  double jitter_weight = 2.0,
                  double loss_penalty_ms_per_pct = 8.0) noexcept;

// Telemetry/pre-flow recommendation only. v0.3.15 deliberately has no
// previous-route/hysteresis input because this function is not a switcher.
PathChoice recommend_path(const PathStats& direct,
                          const PathStats& relay,
                          double min_gain_ms) noexcept;

}  // namespace glo
