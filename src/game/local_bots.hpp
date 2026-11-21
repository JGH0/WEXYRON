/**
 * @file local_bots.hpp
 * @brief Single-player host for the portable bot AI
 *
 * The dedicated server has its own host in src/network/server_bots.*. This
 * class drives the same bots::Bot AI inside the client for offline matches:
 * it owns the bot bodies, feeds them a world snapshot (the local player plus
 * the other bots), applies their damage and exposes plain views the renderer
 * copies into the existing remote-player slots.
 */

#ifndef LOCAL_BOTS_HPP
#define LOCAL_BOTS_HPP

#include "bot.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

class LocalBotHost {
public:
    struct Config {
        std::string mapPath;        ///< assets/maps/name.map
        std::string mapName;        ///< name used by the bot map
        int gameMode = 0;           ///< 0 deathmatch, 1 team deathmatch, 2 bomb
        int matchFormat = 0;        ///< reserved for objective behaviour
        int attackingTeam = 0;      ///< Bomb Defusal: which team attacks now
        int botsTeamA = 0;
        int botsTeamB = 0;
        float accuracy = 0.7f;      ///< 0..1, applied to every bot
        bool friendlyFire = false;
    };

    /// One body for rendering / hit detection.
    struct View {
        uint32_t id = 0;
        uint8_t team = 0;
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;
        float health = 100.0f;
        bool isDead = false;
        bool shooting = false;      ///< fired this tick (viewmodel/anim hint)
    };

    struct Runtime;

    /// Damage the local player takes from a bot shot.
    using PlayerDamageFn = std::function<void(float)>;
    /// Bot fired: origin, velocity, projectile type (visual only; the host
    /// already applied the damage).
    using ShotFn = std::function<void(const glm::vec3&, const glm::vec3&, uint8_t)>;

    LocalBotHost();
    ~LocalBotHost();

    /// Load the map and create `botsTeamA + botsTeamB` bots.
    bool init(const Config& config);
    void stop();
    bool active() const { return map_ != nullptr; }

    /// Advance the AI. `playerFeet` is the local player's feet position.
    void update(float deltaTime, const glm::vec3& playerFeet, int playerTeam,
                bool playerAlive, const PlayerDamageFn& damagePlayer,
                const ShotFn& onShot);

    /// Resolve a player hit against a bot. Returns true when a bot was hit.
    bool damageBot(uint32_t id, float damage, int playerTeam);

    /// A new Bomb Defusal round started: bring every bot back at a spawn,
    /// including bots that died during the previous round. In deathmatch
    /// modes this is a no-op (the normal respawn timer owns them).
    void startNewRound(int attackingTeam);

    /// Apply a blast to every bot inside the radius. The local player's own
    /// blast damage is handled by the game (as before); this only handles the
    /// bots. A bot killed by the player is credited to `playerTeam`.
    void explode(const glm::vec3& position, float radius, float damage,
                 int playerTeam);

    /// Count a local-player death (team deathmatch scoring).
    void recordPlayerDeath(int playerTeam);

    int playerKills() const { return playerKills_; }
    int teamAKills() const { return teamKills_[0]; }
    int teamBKills() const { return teamKills_[1]; }
    std::vector<View> views() const;
    int botCount() const { return static_cast<int>(bots_.size()); }

    static constexpr uint32_t kLocalPlayerId = 0xFFFFFFFEu;
    static constexpr uint32_t kFirstBotId = 0x80000000u;

private:
    Config config_;
    std::unique_ptr<bots::BotMap> map_;
    std::vector<std::unique_ptr<Runtime>> bots_;
    std::mt19937 rng_{0xBADC0DEu};
    int playerKills_ = 0;
    int teamKills_[2] = {0, 0};
    int attackingTeam_ = 0;
    float accumulator_ = 0.0f;

    void tick(float deltaTime, const glm::vec3& playerFeet, int playerTeam,
              bool playerAlive, const PlayerDamageFn& damagePlayer,
              const ShotFn& onShot);
    uint8_t spawnSide(uint8_t team) const;
    void respawnBot(Runtime& bot);
    Runtime* findBot(uint32_t id);
    void registerKill(int team);
};

#endif // LOCAL_BOTS_HPP
