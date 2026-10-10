#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace glo {
// Provider or bundled profile: only exact public IPv4 destinations are routable.
// No arbitrary CIDR expansion or default routes in the privileged worker.
bool parse_profile_hosts(std::string_view csv, std::vector<std::uint32_t>& hosts, std::string& error);
std::string bundled_profile_hosts(std::string_view game);
}
