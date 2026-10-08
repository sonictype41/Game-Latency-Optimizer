#include "glo/game_session_hints.hpp"
#include "glo/route_scope.hpp"
#include <cassert>
#include <string>
int main() {
    using namespace glo;
    auto start=parse_game_session_hint(GameId::Roblox,"2026-10-08T14:06:20Z [FLog::Output] ! Joining game 'uuid' place 123 at 10.3.2.1");
    assert(start.kind==GameHintKind::SessionStarted);
    auto hint=parse_game_session_hint(GameId::Roblox,"2026-10-08T14:06:20Z [FLog::Network] UDMUX Address = 128.116.54.33, Port = 65389 | RCC Server Address = 10.3.2.1");
    assert(hint.kind==GameHintKind::EndpointCandidate);
    assert(hint.endpoint.remote_ipv4_host==ipv4_host(128,116,54,33));
    assert(hint.endpoint.remote_port==65389 && hint.endpoint.local_port==0);
    assert(parse_game_session_hint(GameId::Roblox,"[FLog::Network] UDMUX Address = 8.8.8.8, Port = 65389").kind==GameHintKind::None);
    assert(parse_game_session_hint(GameId::Roblox,"[FLog::Network] UDMUX Address = 128.116.54.33, Port = 80").kind==GameHintKind::None);
    assert(parse_game_session_hint(GameId::Roblox,"[FLog::Network] UDMUX Address = 128.116.54.33, Port = 70000").kind==GameHintKind::None);
    assert(parse_game_session_hint(GameId::Minecraft,"[FLog::Network] UDMUX Address = 128.116.54.33, Port = 65389").kind==GameHintKind::None);
    assert(parse_game_session_hint(GameId::Roblox,"[DFLog::NetworkClient] Client:Disconnect").kind==GameHintKind::SessionEnded);
    return 0;
}
