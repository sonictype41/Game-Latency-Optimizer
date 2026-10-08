#pragma once

#include "glo/game_session_hints.hpp"
#include <atomic>
#include <functional>

namespace glo {
// Optional Windows client-side telemetry adapter. Reads local game log files
// incrementally; it never writes them, injects into the process, or uploads data.
class GameLogHintWatcher {
public:
    using Callback = std::function<void(const GameSessionHint&)>;
    explicit GameLogHintWatcher(GameId game) noexcept : game_(game) {}
    void run(const std::atomic_bool& stop, const Callback& cb) const;
private:
    GameId game_;
};
} // namespace glo
