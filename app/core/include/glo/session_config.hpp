#pragma once

#include "glo/game_profile.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace glo {

struct SessionConfig {
    std::string relay_host;
    std::uint16_t relay_port{43170};
    std::string relay_public_key;
    std::string relay_name;
    GameId game_id{default_game()};
    std::string profile_id;
    std::string gameplay_ipv4;
    std::uint64_t profile_revision{1};
    std::uint16_t port_min{49152}, port_max{65535};
    std::vector<std::uint8_t> grant;
    std::string timeout_message{"Session expired."};
};

// Parse the portable v0.12 config emitted by GLO Web or any compatible issuer.
// The config is data-only: filesystem paths, local key files, account tokens,
// TTL overrides and provider/service modes are deliberately not part of it.
bool parse_session_config_json(std::string_view json, SessionConfig& out, std::string& error);

// Unwrap a successful GLO HTTP API envelope (for example {"ok":true,...})
// into the portable session-config JSON consumed by parse_session_config_json().
// This keeps HTTP response metadata out of the strict portable config contract.
bool unwrap_session_config_api_response(std::string_view response, std::string& config_json, std::string& error);

}  // namespace glo
