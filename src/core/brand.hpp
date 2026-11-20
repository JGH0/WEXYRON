/**
 * @file brand.hpp
 * @brief Product name and public identifiers used across the game
 *
 * The Discord Application ID is a public client identifier, not a secret (it
 * is the same kind of value as a Steam App ID). The bot token / OAuth client
 * secret must never be shipped with the game.
 */

#ifndef GAME_BRAND_HPP
#define GAME_BRAND_HPP

namespace brand {

inline constexpr const char* kShortName = "WEXYRON";
inline constexpr const char* kFullName =
    "WEXYRON: Warfare EXperimentation & Yield-based Reconnaissance Operations Network";
inline constexpr const char* kTagline =
    "WARFARE EXPERIMENTATION & YIELD-BASED RECONNAISSANCE OPERATIONS NETWORK";

/// Public Discord application client id (Rich Presence only).
inline constexpr const char* kDiscordApplicationId = "1573411705831161957";

/// Rich Presence asset keys uploaded in the Discord Developer Portal:
///   logo, menu, editor, deathmatch, team, bomb
inline constexpr const char* kDiscordLargeImage = "logo";

} // namespace brand

#endif // GAME_BRAND_HPP
