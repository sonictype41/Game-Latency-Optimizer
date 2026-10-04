#include "glo/route_handover.hpp"
#include <cassert>
#include <cstdint>

int main() {
    using namespace glo;
    assert(should_endpoint_handover(RouteControlState::RelayLocked, 0x01020304u, 0x01020305u));
    assert(!should_endpoint_handover(RouteControlState::RelayLocked, 0x01020304u, 0x01020304u));
    assert(!should_endpoint_handover(RouteControlState::WaitingForGame, 0x01020304u, 0x01020305u));

    assert(relay_route_is_current(RouteControlState::RelayLocked, 0x01020304u, 0));
    assert(relay_route_is_current(RouteControlState::RelayLocked, 0x01020304u, kRelayTrafficFreshMs));
    assert(!relay_route_is_current(RouteControlState::RelayLocked, 0x01020304u, kRelayTrafficFreshMs + 1));
    assert(!relay_route_is_current(RouteControlState::DirectLocked, 0x01020304u, 0));
    return 0;
}
