#include "glo/route_scope.hpp"

#include <cassert>
#include <iostream>

int main() {
    using namespace glo;

    const Ipv4Cidr allowed{ipv4_host(128, 116, 0, 0), 17};
    assert(cidr_contains(allowed, ipv4_host(128, 116, 0, 1)));
    assert(cidr_contains(allowed, ipv4_host(128, 116, 54, 33)));
    assert(cidr_contains(allowed, ipv4_host(128, 116, 127, 255)));
    assert(!cidr_contains(allowed, ipv4_host(128, 116, 128, 0)));
    assert(cidr_text(allowed) == "128.116.0.0/17");

    const auto excluded_host = ipv4_host(128, 116, 54, 33);
    const auto guard_ranges = cidr_excluding_host(allowed, excluded_host);
    assert(guard_ranges.size() == 15);
    for (const auto& part : guard_ranges) {
        assert(!cidr_contains(part, excluded_host));
        assert(part.prefix_length >= 18 && part.prefix_length <= 32);
    }
    assert(cidr_excluding_host(host_route(excluded_host), excluded_host).empty());
    for (std::uint32_t offset = 0; offset < (1u << 15u); ++offset) {
        const auto ip = ipv4_host(128, 116, 0, 0) + offset;
        unsigned matches = 0;
        for (const auto& part : guard_ranges) matches += cidr_contains(part, ip) ? 1u : 0u;
        assert(matches == (ip == excluded_host ? 0u : 1u));
    }
    const auto untouched = cidr_excluding_host(allowed, ipv4_host(1, 1, 1, 1));
    assert(untouched.size() == 1 && untouched.front() == allowed);

    const auto& allowlist = roblox_game_allowlist();
    assert(allowlist.size() == 1);
    assert(allowlist.front() == allowed);
    assert(roblox_game_ip_allowed(ipv4_host(128, 116, 46, 33)));
    assert(!roblox_game_ip_allowed(ipv4_host(1, 1, 1, 1)));

    assert(!roblox_gameplay_udp_port_allowed(3478));
    assert(!roblox_gameplay_udp_port_allowed(49151));
    assert(roblox_gameplay_udp_port_allowed(49152));
    assert(roblox_gameplay_udp_port_allowed(65535));
    assert(roblox_gameplay_endpoint_allowed(ipv4_host(128, 116, 46, 33), 49769));
    assert(!roblox_gameplay_endpoint_allowed(ipv4_host(128, 116, 46, 33), 3478));

    const auto exact = host_route(ipv4_host(128, 116, 54, 33));
    assert(exact.prefix_length == 32);
    assert(exact.network_host == ipv4_host(128, 116, 54, 33));
    assert(cidr_text(exact) == "128.116.54.33/32");

    std::cout << "GLO v0.3.15 route scope tests PASS\n";
    return 0;
}
