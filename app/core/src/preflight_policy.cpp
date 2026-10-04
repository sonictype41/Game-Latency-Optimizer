#include "glo/preflight_policy.hpp"
#include "glo/route_scope.hpp"

namespace glo {

bool preflight_endpoint_allowed(const PreflightEndpoint& endpoint) noexcept {
    constexpr std::uint8_t kUdpProtocol = 17;
    return endpoint.protocol == kUdpProtocol &&
           endpoint.remote_ipv4_host != 0 &&
           endpoint.remote_port != 0 &&
           endpoint.local_port != 0 &&
           roblox_gameplay_endpoint_allowed(endpoint.remote_ipv4_host, endpoint.remote_port);
}

}  // namespace glo
