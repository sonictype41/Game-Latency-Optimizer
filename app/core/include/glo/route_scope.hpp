#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace glo {

struct Ipv4Cidr {
    std::uint32_t network_host{};
    std::uint8_t prefix_length{};

    bool operator==(const Ipv4Cidr& other) const noexcept {
        return network_host == other.network_host && prefix_length == other.prefix_length;
    }
};

constexpr std::uint32_t ipv4_host(std::uint8_t a,
                                  std::uint8_t b,
                                  std::uint8_t c,
                                  std::uint8_t d) noexcept {
    return (static_cast<std::uint32_t>(a) << 24u) |
           (static_cast<std::uint32_t>(b) << 16u) |
           (static_cast<std::uint32_t>(c) << 8u) |
           static_cast<std::uint32_t>(d);
}

std::uint32_t cidr_mask(std::uint8_t prefix_length) noexcept;
bool cidr_contains(const Ipv4Cidr& cidr, std::uint32_t ip_host) noexcept;
// Return a minimal sibling-CIDR cover for `cidr` with exactly one /32 host
// removed. If the host is outside the CIDR, the original CIDR is returned.
std::vector<Ipv4Cidr> cidr_excluding_host(const Ipv4Cidr& cidr, std::uint32_t excluded_host);
std::string ipv4_text(std::uint32_t ip_host);
std::string cidr_text(const Ipv4Cidr& cidr);

// Validation allow-list only. v0.3.15 never installs these broad prefixes into
// the Windows route table. A relay route must always be a learned /32 host route.
const std::vector<Ipv4Cidr>& roblox_game_allowlist();
bool roblox_game_ip_allowed(std::uint32_t ip_host) noexcept;

// Roblox documents experience traffic on UDP destination ports 49152-65535.
// Keep the range in the game profile layer so Wintun packet classification all
// share one definition instead of inventing independent heuristics.
inline constexpr std::uint16_t kRobloxGameplayUdpPortMin = 49152;
inline constexpr std::uint16_t kRobloxGameplayUdpPortMax = 65535;
bool roblox_gameplay_udp_port_allowed(std::uint16_t port) noexcept;
bool roblox_gameplay_endpoint_allowed(std::uint32_t ip_host, std::uint16_t port) noexcept;

Ipv4Cidr host_route(std::uint32_t ip_host) noexcept;

}  // namespace glo
