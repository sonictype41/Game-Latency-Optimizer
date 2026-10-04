#pragma once

#include "glo/game_profile.hpp"
#include "glo/roblox_detector.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace glo {

struct GameState {
    GameId game_id{default_game()};
    bool process_running{false};
    bool gameplay_active{false};
    std::uint32_t primary_pid{0};
    std::uint32_t gameplay_host_ipv4{0};
    std::string detail;
};

// Process detector only. ClientCore may annotate gameplay_active/host from
// actual Wintun forwarding evidence after poll() returns.
class GameDetector {
public:
    explicit GameDetector(GameId game_id = default_game());
    ~GameDetector();
    GameDetector(const GameDetector&) = delete;
    GameDetector& operator=(const GameDetector&) = delete;

    GameState poll();
    GameId game_id() const noexcept { return game_id_; }

private:
    GameId game_id_{default_game()};
    std::unique_ptr<RobloxDetector> roblox_;
};

}  // namespace glo
