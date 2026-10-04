#pragma once

#include "glo/route_events.hpp"

#include <cstdint>
#include <limits>

namespace glo {

// Gameplay is considered current only when actual C2S traffic has traversed
// the selected /32 recently. The endpoint gate never decides gameplay state; it only gates a
// new endpoint before Direct bootstrap.
constexpr std::uint64_t kRelayTrafficFreshMs = 3000;

constexpr bool should_endpoint_handover(RouteControlState state,
                                        std::uint32_t routed_host,
                                        std::uint32_t candidate_host) noexcept {
    return state == RouteControlState::RelayLocked && routed_host != 0 &&
           candidate_host != 0 && routed_host != candidate_host;
}

constexpr bool relay_route_is_current(RouteControlState state,
                                      std::uint32_t routed_host,
                                      std::uint64_t routed_idle_ms,
                                      std::uint64_t traffic_fresh_ms = kRelayTrafficFreshMs) noexcept {
    return state == RouteControlState::RelayLocked && routed_host != 0 &&
           routed_idle_ms <= traffic_fresh_ms;
}

}  // namespace glo
