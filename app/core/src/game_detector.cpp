#include "glo/game_detector.hpp"

namespace glo {

GameDetector::GameDetector(GameId game_id) : game_id_(game_id) {
    if (game_id_ == GameId::Roblox) roblox_ = std::make_unique<RobloxDetector>();
}

GameDetector::~GameDetector() = default;

GameState GameDetector::poll() {
    GameState out;
    out.game_id = game_id_;
    if (!roblox_) {
        out.detail = std::string(game_name(game_id_)) + " profile is not enabled in this build";
        return out;
    }
    const auto r = roblox_->poll();
    out.process_running = r.process_running;
    out.primary_pid = r.primary_pid;
    out.detail = r.detail;
    return out;
}

}  // namespace glo
