#include "glo/preflight_policy.hpp"
#include "glo/route_scope.hpp"

#include <cassert>
#include <iostream>

int main() {
    using namespace glo;

    PreflightEndpoint good{ipv4_host(128, 116, 54, 33), 54513, 53721, 17};
    assert(preflight_endpoint_allowed(good));

    auto bad_proto = good;
    bad_proto.protocol = 6;
    assert(!preflight_endpoint_allowed(bad_proto));

    auto bad_remote_port = good;
    bad_remote_port.remote_port = 0;
    assert(!preflight_endpoint_allowed(bad_remote_port));

    auto stun_like = good;
    stun_like.remote_port = 3478;
    assert(!preflight_endpoint_allowed(stun_like));

    auto below_game_range = good;
    below_game_range.remote_port = 49151;
    assert(!preflight_endpoint_allowed(below_game_range));

    auto game_range_min = good;
    game_range_min.remote_port = 49152;
    assert(preflight_endpoint_allowed(game_range_min));

    auto bad_local_port = good;
    bad_local_port.local_port = 0;
    assert(!preflight_endpoint_allowed(bad_local_port));

    auto outside = good;
    outside.remote_ipv4_host = ipv4_host(1, 1, 1, 1);
    assert(!preflight_endpoint_allowed(outside));

    std::cout << "GLO v0.3.15 preflight policy tests PASS\n";
    return 0;
}
