#pragma once
#include "glo/route_scope.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace glo {
// Provider-supplied aligned public IPv4 CIDRs, /16 through /32 (no expansion).
bool parse_profile_hosts(std::string_view csv, std::vector<Ipv4Cidr>& hosts, std::string& error);
std::string bundled_profile_hosts(std::string_view game);
}
