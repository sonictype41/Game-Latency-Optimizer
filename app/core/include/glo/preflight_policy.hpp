#pragma once

#include <cstdint>

namespace glo {

struct PreflightEndpoint {
    std::uint32_t remote_ipv4_host{};
    std::uint16_t remote_port{};
    std::uint16_t local_port{};
    std::uint8_t protocol{};
};

// Pure validation shared by the Windows endpoint gate and portable tests. The
// gate must never be allowed to widen the existing
// Roblox relay scope or accidentally promote TCP/invalid tuples.
bool preflight_endpoint_allowed(const PreflightEndpoint& endpoint) noexcept;

}  // namespace glo
