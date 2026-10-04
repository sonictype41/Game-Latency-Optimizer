#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace glo {

enum class GameId : std::uint8_t {
    Roblox = 0,
    Minecraft = 1,
    PUBG = 2,
    LeagueOfLegends = 3,
};

struct GameProfile {
    GameId id{};
    std::string_view key;
    std::string_view display_name;
    bool supported{};
};

inline constexpr std::array<GameProfile, 4> kGameProfiles{{
    {GameId::Roblox, "roblox", "Roblox", true},
    {GameId::Minecraft, "minecraft", "Minecraft", false},
    {GameId::PUBG, "pubg", "PUBG", false},
    {GameId::LeagueOfLegends, "league-of-legends", "League of Legends", false},
}};

constexpr const GameProfile& game_profile(GameId id) noexcept {
    for (const auto& profile : kGameProfiles) {
        if (profile.id == id) return profile;
    }
    return kGameProfiles.front();
}

constexpr bool game_supported(GameId id) noexcept {
    return game_profile(id).supported;
}

constexpr std::string_view game_name(GameId id) noexcept {
    return game_profile(id).display_name;
}

constexpr GameId default_game() noexcept { return GameId::Roblox; }

}  // namespace glo
