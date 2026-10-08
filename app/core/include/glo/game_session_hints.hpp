#pragma once

#include "glo/game_profile.hpp"
#include "glo/preflight_policy.hpp"
#include <string_view>

namespace glo {

// A small, optional adapter-to-core contract. It never carries packet content,
// account IDs, session credentials, game-log lines or executable instructions.
enum class GameHintKind { None, SessionStarted, SessionEnded, EndpointCandidate };
struct GameSessionHint {
    GameHintKind kind{GameHintKind::None};
    PreflightEndpoint endpoint{};
};

// Public validator for game-specific hints; the generic core does not need to
// hardcode a game's allowlist when processing early route preparation.
bool game_endpoint_hint_allowed(GameId game, const PreflightEndpoint& endpoint) noexcept;

// Pure parser: game-specific syntax stays OUTSIDE the generic route controller.
// local_port=0 is deliberately permitted for early hints; Wintun learns it
// from the actual packet, and the relay never receives this hint.
GameSessionHint parse_game_session_hint(GameId game, std::string_view line) noexcept;

} // namespace glo
