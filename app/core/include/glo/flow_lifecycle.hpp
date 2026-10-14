#pragma once

#include "glo/protocol.hpp"

#include <cstdint>
#include <cstddef>

namespace glo {

// Local Wintun tuple state is distinct from the relay's per-flow UDP socket.
// Keep this idle threshold comfortably above the stock relay's 15-second GC.
// A time-based sweep bounds client memory without recycling flow IDs in-session.
struct ClientFlowIdlePolicy {
    static constexpr std::uint64_t kIdleMs = 60'000;
    static constexpr std::uint64_t kSweepMs = 5'000;

    static constexpr bool expired(std::uint64_t now_ms, std::uint64_t last_seen_ms) noexcept {
        return last_seen_ms != 0 && now_ms >= last_seen_ms && now_ms - last_seen_ms >= kIdleMs;
    }
};


// Shared, portable eviction primitive. The caller owns synchronization; erasing
// a flow must also clear both its tuple index and first-forward notification.
// Reusing a tuple later must allocate a fresh, monotonic flow ID.
template<class FlowMap, class TupleMap, class NotifiedSet, class TupleFromFlow>
std::size_t evict_idle_local_flows(FlowMap& flows, TupleMap& tuples,
                                   NotifiedSet& notified, std::uint64_t now_ms,
                                   TupleFromFlow tuple_from_flow) {
    std::size_t removed = 0;
    for (auto it = flows.begin(); it != flows.end();) {
        if (!ClientFlowIdlePolicy::expired(now_ms, it->second.last_activity_ms)) {
            ++it;
            continue;
        }
        tuples.erase(tuple_from_flow(it->second));
        notified.erase(it->first);
        it = flows.erase(it);
        ++removed;
    }
    return removed;
}

enum class RelayRejectAction {
    KeepTunnel,         // Flow-level temporary admission rejection: not a transport outage.
    FailOpenAdmission,  // Session capacity/maintenance (or unscoped error).
};

// A temporary reject is safe to treat as flow-local only when tagged with a flow ID.
// The relay can admit that same ID again on a later C2S datagram when it has capacity.
constexpr RelayRejectAction classify_relay_reject(protocol::RelayRejectReason reason,
                                                   std::uint32_t flow_id) noexcept {
    return reason == protocol::RelayRejectReason::Temporary && flow_id != 0
        ? RelayRejectAction::KeepTunnel
        : RelayRejectAction::FailOpenAdmission;
}

} // namespace glo
