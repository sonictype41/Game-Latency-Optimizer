#include "glo/game_session_hints.hpp"
#include "glo/route_scope.hpp"
#include <charconv>
#include <cstdint>

namespace glo {
namespace {
bool parse_uint(std::string_view value, std::uint32_t& result) noexcept {
    if (value.empty()) return false;
    const char* first = value.data();
    const char* last = first + value.size();
    auto parsed = std::from_chars(first, last, result);
    return parsed.ec == std::errc{} && parsed.ptr == last;
}
bool parse_ip(std::string_view text, std::uint32_t& host) noexcept {
    std::uint32_t octets[4]{};
    for (int i=0; i<4; ++i) {
        auto cut = text.find('.');
        if (i == 3) {
            if (cut != std::string_view::npos || !parse_uint(text, octets[i])) return false;
        } else {
            if (cut == std::string_view::npos || !parse_uint(text.substr(0,cut), octets[i])) return false;
            text.remove_prefix(cut+1);
        }
        if (octets[i] > 255) return false;
    }
    host = ipv4_host(static_cast<std::uint8_t>(octets[0]),static_cast<std::uint8_t>(octets[1]),
                     static_cast<std::uint8_t>(octets[2]),static_cast<std::uint8_t>(octets[3]));
    return true;
}
} // namespace
bool game_endpoint_hint_allowed(GameId game, const PreflightEndpoint& ep) noexcept {
    switch (game) {
        case GameId::Roblox:
            return ep.protocol == 17 && ep.remote_ipv4_host != 0 && ep.remote_port != 0 &&
                   ep.local_port == 0 && roblox_gameplay_endpoint_allowed(ep.remote_ipv4_host,ep.remote_port);
        default: return false;
    }
}
GameSessionHint parse_game_session_hint(GameId game, std::string_view line) noexcept {
    if (game != GameId::Roblox || line.size() > 8192) return {};
    if (line.find("[FLog::Output] ! Joining game '") != std::string_view::npos)
        return {GameHintKind::SessionStarted, {}};
    if (line.find("[DFLog::NetworkClient] Client:Disconnect") != std::string_view::npos ||
        line.find("[FLog::SingleSurfaceApp] leaveUGCGameInternal") != std::string_view::npos)
        return {GameHintKind::SessionEnded, {}};
    constexpr std::string_view marker = "[FLog::Network] UDMUX Address = ";
    const auto begin = line.find(marker);
    if (begin == std::string_view::npos) return {};
    auto tail = line.substr(begin + marker.size());
    constexpr std::string_view delimiter = ", Port = ";
    const auto middle = tail.find(delimiter);
    if (middle == std::string_view::npos) return {};
    const auto ip_part = tail.substr(0,middle);
    auto port_part = tail.substr(middle+delimiter.size());
    auto end = port_part.find_first_not_of("0123456789");
    if (end == 0) return {};
    if (end != std::string_view::npos) port_part = port_part.substr(0,end);
    std::uint32_t ip=0, port=0;
    if (!parse_ip(ip_part,ip) || !parse_uint(port_part,port) || port > 65535) return {};
    const PreflightEndpoint ep{ip,static_cast<std::uint16_t>(port),0,17};
    if (!game_endpoint_hint_allowed(game,ep)) return {};
    return {GameHintKind::EndpointCandidate, ep};
}
} // namespace glo
