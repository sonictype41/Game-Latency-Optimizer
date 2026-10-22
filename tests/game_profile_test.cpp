#include "glo/game_profile.hpp"
#include <cassert>
#include <string_view>

int main() {
    using namespace glo;
    static_assert(kGameProfiles.size() >= 4);
    static_assert(default_game() == GameId::Roblox);
    static_assert(game_supported(GameId::Roblox));
    static_assert(game_supported(GameId::Valorant));
    static_assert(game_supported(GameId::PUBG));
    static_assert(game_supported(GameId::TFT));
    static_assert(static_cast<int>(GameId::Valorant) == 4);
    static_assert(static_cast<int>(GameId::TFT) == 5);
    static_assert(game_profile(GameId::TFT).key == "tft");
    static_assert(game_profile(GameId::PUBG).key == "pubg");
    static_assert(game_profile(GameId::Valorant).key == "valorant");
    static_assert(!game_supported(GameId::Minecraft));
    static_assert(game_name(GameId::Roblox) == std::string_view{"Roblox"});
    assert(game_profile(GameId::LeagueOfLegends).key == "league-of-legends");
    return 0;
}
