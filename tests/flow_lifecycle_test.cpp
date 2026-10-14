#include "glo/flow_lifecycle.hpp"

#include <cassert>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

int main() {
    using namespace glo;
    using protocol::RelayRejectReason;
    static_assert(ClientFlowIdlePolicy::kIdleMs == 60000);
    static_assert(ClientFlowIdlePolicy::kSweepMs == 5000);
    static_assert(!ClientFlowIdlePolicy::expired(59999, 1));
    static_assert(ClientFlowIdlePolicy::expired(60001, 1));
    static_assert(!ClientFlowIdlePolicy::expired(60000, 0));
    static_assert(!ClientFlowIdlePolicy::expired(20, 30));
    static_assert(classify_relay_reject(RelayRejectReason::Temporary, 420) == RelayRejectAction::KeepTunnel);
    static_assert(classify_relay_reject(RelayRejectReason::Temporary, 0) == RelayRejectAction::FailOpenAdmission);
    static_assert(classify_relay_reject(RelayRejectReason::Capacity, 420) == RelayRejectAction::FailOpenAdmission);
    static_assert(classify_relay_reject(RelayRejectReason::Maintenance, 420) == RelayRejectAction::FailOpenAdmission);
    // Monotonically increasing IDs are identifiers, *not* active-flow quotas.
    constexpr std::uint32_t id_far_above_relay_flow_cap = 1025;
    assert(classify_relay_reject(RelayRejectReason::Temporary, id_far_above_relay_flow_cap) == RelayRejectAction::KeepTunnel);
    struct TestFlow { int tuple; std::uint64_t last_activity_ms; };
    std::unordered_map<std::uint32_t, TestFlow> flows;
    std::unordered_map<int, std::uint32_t> reverse;
    std::unordered_set<std::uint32_t> notified;
    // VALORANT-scale burst: lots of short-lived probes, one live gameplay flow.
    for (std::uint32_t id = 1; id <= 150; ++id) {
        flows.emplace(id, TestFlow{static_cast<int>(id), 1000});
        reverse.emplace(static_cast<int>(id), id);
        notified.insert(id);
    }
    flows.at(1).last_activity_ms = 61000; // receiving S2C keeps this one alive
    const auto tuple_of = [](const TestFlow& f) { return f.tuple; };
    assert(evict_idle_local_flows(flows, reverse, notified, 62000, tuple_of) == 149);
    assert(flows.size() == 1 && flows.contains(1));
    assert(reverse.size() == 1 && reverse.contains(1));
    assert(notified.size() == 1 && notified.contains(1));
    assert(evict_idle_local_flows(flows, reverse, notified, 100000, tuple_of) == 0);
    assert(evict_idle_local_flows(flows, reverse, notified, 121000, tuple_of) == 1);
    assert(flows.empty() && reverse.empty() && notified.empty());
    // The same tuple may be reused after eviction with a NEW flow ID.
    flows.emplace(151, TestFlow{1, 122000});
    reverse.emplace(1, 151);
    assert(evict_idle_local_flows(flows, reverse, notified, 122500, tuple_of) == 0);
    assert(reverse.at(1) == 151);
    return 0;
}
