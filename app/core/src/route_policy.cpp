#include "glo/route_policy.hpp"

#include <algorithm>
#include <limits>

namespace glo {

double path_score(const PathStats& stats, double jitter_weight, double loss_penalty_ms_per_pct) noexcept {
    if (!stats.valid()) return std::numeric_limits<double>::infinity();
    const double jitter = std::max(0.0, stats.jitter_ms);
    const double loss = std::clamp(stats.loss_pct, 0.0, 100.0);
    return stats.rtt_ms + jitter_weight * jitter + loss_penalty_ms_per_pct * loss;
}

PathChoice recommend_path(const PathStats& direct,
                          const PathStats& relay,
                          double min_gain_ms) noexcept {
    const bool d = direct.valid();
    const bool r = relay.valid();
    if (!d && !r) return PathChoice::Insufficient;
    if (!d) return r ? PathChoice::Relay : PathChoice::Insufficient;
    if (!r) return PathChoice::Direct;

    const double ds = path_score(direct);
    const double rs = path_score(relay);
    const double gain = std::max(0.0, min_gain_ms);
    return rs + gain < ds ? PathChoice::Relay : PathChoice::Direct;
}

}  // namespace glo
