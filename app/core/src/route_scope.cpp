#include "glo/route_scope.hpp"

#include <algorithm>
#include <sstream>

namespace glo {

std::uint32_t cidr_mask(std::uint8_t prefix_length) noexcept {
    if (prefix_length == 0) return 0;
    if (prefix_length >= 32) return 0xffffffffu;
    return 0xffffffffu << (32u - prefix_length);
}

bool cidr_contains(const Ipv4Cidr& cidr, std::uint32_t ip_host) noexcept {
    if (cidr.prefix_length > 32) return false;
    const auto mask = cidr_mask(cidr.prefix_length);
    return (ip_host & mask) == (cidr.network_host & mask);
}

std::vector<Ipv4Cidr> cidr_excluding_host(const Ipv4Cidr& cidr, std::uint32_t excluded_host) {
    if (cidr.prefix_length > 32) return {};
    const auto normalized = cidr.network_host & cidr_mask(cidr.prefix_length);
    if (!cidr_contains(cidr, excluded_host)) return {{normalized, cidr.prefix_length}};
    if (cidr.prefix_length == 32) return {};

    std::vector<Ipv4Cidr> out;
    out.reserve(32u - cidr.prefix_length);
    std::uint32_t current = normalized;
    for (std::uint8_t prefix = cidr.prefix_length; prefix < 32; ++prefix) {
        const std::uint32_t split_bit = std::uint32_t{1} << (31u - prefix);
        const bool host_in_right = (excluded_host & split_bit) != 0;
        const std::uint32_t sibling = host_in_right ? current : (current | split_bit);
        out.push_back(Ipv4Cidr{sibling, static_cast<std::uint8_t>(prefix + 1)});
        if (host_in_right) current |= split_bit;
    }
    return out;
}

std::string ipv4_text(std::uint32_t ip_host) {
    std::ostringstream s;
    s << ((ip_host >> 24u) & 0xffu) << '.'
      << ((ip_host >> 16u) & 0xffu) << '.'
      << ((ip_host >> 8u) & 0xffu) << '.'
      << (ip_host & 0xffu);
    return s.str();
}

std::string cidr_text(const Ipv4Cidr& cidr) {
    return ipv4_text(cidr.network_host & cidr_mask(cidr.prefix_length)) + "/" +
           std::to_string(std::min<unsigned>(cidr.prefix_length, 32));
}

const std::vector<Ipv4Cidr>& roblox_game_allowlist() {
    static const std::vector<Ipv4Cidr> kAllowed = {
        {ipv4_host(128, 116, 0, 0), 17},
    };
    return kAllowed;
}

bool roblox_game_ip_allowed(std::uint32_t ip_host) noexcept {
    for (const auto& cidr : roblox_game_allowlist()) {
        if (cidr_contains(cidr, ip_host)) return true;
    }
    return false;
}

bool roblox_gameplay_udp_port_allowed(std::uint16_t port) noexcept {
    return port >= kRobloxGameplayUdpPortMin && port <= kRobloxGameplayUdpPortMax;
}

bool roblox_gameplay_endpoint_allowed(std::uint32_t ip_host, std::uint16_t port) noexcept {
    return roblox_game_ip_allowed(ip_host) && roblox_gameplay_udp_port_allowed(port);
}

Ipv4Cidr host_route(std::uint32_t ip_host) noexcept {
    return Ipv4Cidr{ip_host, 32};
}

}  // namespace glo
