#include "glo/game_profile.hpp"
#include <cassert>
#include <string_view>

int main() {
    using namespace glo;
    static_assert(kGameProfiles.size() >= 4);
    static_assert(default_game() == GameId::Roblox);
    static_assert(game_supported(GameId::Roblox));
    static_assert(!game_supported(GameId::Minecraft));
    static_assert(game_name(GameId::Roblox) == std::string_view{"Roblox"});
    assert(game_profile(GameId::LeagueOfLegends).key == "league-of-legends");
    return 0;
}
