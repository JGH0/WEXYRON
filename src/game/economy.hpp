/**
 * @file economy.hpp
 * @brief Buy-menu items, prices and the round-money rules
 *
 * Shared by the client (buy menu, HUD) and the dedicated server (validation),
 * so both sides always agree on an item's id and price. Only the bomb-defusal
 * mode is economic; every other mode shows the same menu with free items.
 */

#ifndef ECONOMY_HPP
#define ECONOMY_HPP

#include <cstdint>

namespace economy {

/// Ids sent in BuyActionPacket. Keep the values stable (network protocol).
enum Item : uint8_t {
    ITEM_NONE = 0,
    ITEM_KEVLAR = 1,          ///< full kevlar vest (armor 100)
    ITEM_DEFUSE_KIT = 2,      ///< halves the defuse time
    ITEM_PRIMARY_SHOTGUN = 3, ///< pump shotgun in the primary slot
    ITEM_PRIMARY_SNIPER = 4,  ///< sniper rifle in the primary slot
    ITEM_HE_GRENADE = 5,      ///< one HE grenade in slot 4
    ITEM_FLASHBANG = 6        ///< one flashbang in slot 5
};

/// Starting money for a fresh match.
constexpr int kStartingMoney = 800;
/// Money is clamped to this after every reward.
constexpr int kMaxMoney = 16000;
/// Reward for a kill in an economic mode.
constexpr int kKillReward = 100;
/// Reward for winning a round.
constexpr int kRoundWinReward = 1500;
/// First loss bonus; it grows by 250 per consecutive loss, capped at five.
constexpr int kLossBonusBase = 700;
constexpr int kLossBonusStep = 250;
constexpr int kLossBonusMaxStreak = 5;
/// Extra time after the freeze period in which the buy menu stays available
/// while the player has not moved (seconds).
constexpr float kBuyGraceSeconds = 20.0f;
/// Single-player default freeze time before a round starts (seconds). The
/// dedicated server overrides this from gameplay.freezeTime.
constexpr float kSinglePlayerFreezeTime = 10.0f;

/// Cost of one buy-menu item (0 for the free modes).
inline int price(Item item) {
    switch (item) {
        case ITEM_KEVLAR:          return 650;
        case ITEM_DEFUSE_KIT:      return 400;
        case ITEM_PRIMARY_SHOTGUN: return 1500;
        case ITEM_PRIMARY_SNIPER:  return 4750;
        case ITEM_HE_GRENADE:      return 300;
        case ITEM_FLASHBANG:       return 200;
        default:                   return 0;
    }
}

/// Loss bonus for the n-th consecutive loss (1-based).
inline int lossBonus(int lossStreak) {
    if (lossStreak < 1) lossStreak = 1;
    if (lossStreak > kLossBonusMaxStreak) lossStreak = kLossBonusMaxStreak;
    return kLossBonusBase + (lossStreak - 1) * kLossBonusStep;
}

/// True for the modes that use money, the buy phase and round loadouts.
inline bool isEconomicMode(int gameMode) { return gameMode == 2; }

} // namespace economy

#endif // ECONOMY_HPP
