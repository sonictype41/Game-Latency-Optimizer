#include "glo/route_events.hpp"

#include <cassert>

int main() {
    using namespace glo;
    RouteControlModel m;
    m.reset(7, RouteControlState::WaitingForGame);
    assert(!m.locked());
    assert(!m.verifying(8));
    assert(m.verifying(7));
    assert(m.state() == RouteControlState::RelayVerifying);
    assert(m.lock_relay(7));
    assert(m.locked());
    assert(m.lock_direct(7));
    assert(m.state() == RouteControlState::DirectLocked);
    assert(!m.lock_direct(7));
    assert(m.next_gameplay_cycle(7));
    assert(m.state() == RouteControlState::WaitingForGame);
    assert(m.verifying(7));
    assert(m.lock_relay(7));
    assert(m.next_gameplay_cycle(7));
    assert(m.state() == RouteControlState::WaitingForGame);
    assert(!m.next_gameplay_cycle(8));

    RouteFlowIdentity f{3, 9, 0x80743621u, 54949, 42000};
    RouteFlowIdentity same = f;
    RouteFlowIdentity other{3, 10, 0x80743621u, 54949, 42000};
    assert(f.valid());
    assert(same_route_flow(f, same));
    assert(!same_route_flow(f, other));
    PreflightEndpoint ep{0x80743621u, 54949, 42000, 17};
    assert(same_endpoint_tuple(f, ep));

    ForwardedFlowSet forwarded;
    RouteFlowIdentity first{4, 20, 0x80742e21u, 56763, 55329};
    RouteFlowIdentity candidate{4, 21, 0x80742e21u, 58800, 55331};
    assert(forwarded.remember(first));
    assert(forwarded.remember(candidate));
    assert(forwarded.size() == 2);
    assert(forwarded.contains(20));
    assert(forwarded.contains(21));
    assert(!forwarded.contains(22));
    assert(forwarded.matches(candidate));
    RouteFlowIdentity wrong_epoch = candidate;
    wrong_epoch.route_epoch++;
    assert(!forwarded.matches(wrong_epoch));
    forwarded.clear();
    assert(!forwarded.matches(candidate));
    return 0;
}
