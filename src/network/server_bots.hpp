/**
 * @file server_bots.hpp
 * @brief Glue between the portable bot AI and the dedicated server
 *
 * The bot AI (src/game/bot.*) is deliberately network-agnostic. This host
 * creates a ServerPlayer per bot, feeds the AI a world snapshot, turns the AI
 * actions into packets/damage, and keeps bot physics / respawns in sync with
 * the authoritative ServerPlayer records. The same AI can later be driven by
 * singleplayer code without the network layer.
 */

#ifndef SERVER_BOTS_HPP
#define SERVER_BOTS_HPP

#include "network.hpp"
#include "../game/bot.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

class ServerBotManager {
public:
    struct Config {
        int count = 0;
        float accuracy = 0.65f;
        std::string defaultMap = "simple.map";
        std::string mapDirectory = "assets/maps";
        bool friendlyFire = false;
        float respawnDelay = 3.0f;
    };

    ServerBotManager();
    ~ServerBotManager();

    /// Create the configured bots on `server`. No-op for count <= 0.
    bool init(NetworkServer& server, const Config& config);

    /// Advance the AI at a fixed 60 Hz and broadcast bot state. Call every
    /// server frame (the manager throttles itself).
    void update(float deltaTime, NetworkServer& server);

    /// A projectile from a human client. We only need to simulate flashbangs
    /// server-side; bot projectiles are added directly by applyActions().
    void onProjectileSpawned(uint32_t ownerId, const ProjectileSpawnPacket& packet);

    /// A round-based client started a new round on `mapName`. Dead bots on
    /// that map are revived, matching the human clients.
    void startRound(const std::string& mapName, int gameMode);

    /// Bomb state the bot AI needs for one map. Kept as a plain value so this
    /// host stays independent of the concrete server bomb implementation.
    struct BombInfo {
        bool valid = false;
        bool roundActive = true;
        bool planted = false;
        bool defused = false;
        bool exploded = false;
        glm::vec3 position{0.0f};
        int plantingTeam = -1;
        int attackingTeam = 0;
    };
    using BombInfoProvider = std::function<BombInfo(const std::string&)>;

    /// Bots read the authoritative bomb to choose between sites, the planted
    /// bomb, and guard positions. Unset in tests (they keep the neutral state).
    void setBombInfoProvider(BombInfoProvider provider) {
        bombInfoProvider_ = std::move(provider);
    }

    /// Damage produced by a bot (bullets, or a grenade blast the server
    /// resolved). The server applies it like PACKET_PLAYER_DAMAGE.
    std::function<void(uint32_t shooterId, uint32_t victimId,
                       uint16_t damage, uint8_t weaponId)> onDamage;

    int botCount() const { return static_cast<int>(bots_.size()); }
    bool enabled() const { return !bots_.empty(); }
    /// Highest number of bots the client can display (MAX_REMOTE_PLAYERS).
    static constexpr int kMaxBots = 8;

private:
    struct Runtime;
    struct ServerProjectile {
        uint32_t ownerId = 0;
        std::string mapName;
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float lifetime = 0.0f;
        uint8_t type = 0;
    };

    Config config_;
    NetworkServer* server_ = nullptr;
    BombInfoProvider bombInfoProvider_;
    std::map<std::string, std::unique_ptr<bots::BotMap>> maps_;
    std::vector<std::unique_ptr<Runtime>> bots_;
    std::vector<ServerProjectile> projectiles_;
    std::mt19937 rng_{0xC0FFEEu};
    float accumulator_ = 0.0f;
    bool idle_ = false;  ///< no human connected: freeze all bot activity

    /// Load (and cache) a map. Falls back to an empty floor map when missing.
    const bots::BotMap* getMap(const std::string& mapName);
    /// Resolve "assets/maps/name", "name", etc. and return the first file.
    std::string resolveMapPath(const std::string& mapName) const;

    /// Run one fixed AI tick.
    void tick(float deltaTime, NetworkServer& server);

    /// Move a bot to the world / game mode its followed player is in.
    void switchBotWorld(NetworkServer& server, Runtime& runtime,
                        const std::string& mapName, int gameMode);

    /// Respawn a dead bot on its current map.
    void respawnBot(NetworkServer& server, Runtime& runtime);

    /// A new round started on `mapName`: bring every dead bot on that map
    /// back, using the same round-start rule as human clients.
    void reviveBotsForNewRound(NetworkServer& server, const std::string& mapName,
                               int gameMode);

    /// Turn AI actions into packets and damage.
    void applyActions(NetworkServer& server, Runtime& runtime,
                      const std::vector<bots::Action>& actions);

    /// Advance server-simulated grenades/flashbangs (collision + detonation).
    void updateProjectiles(float deltaTime, NetworkServer& server);

    /// Blind bots and/or damage players inside a resolved blast.
    void detonate(const ServerProjectile& projectile, NetworkServer& server);

    static float respawnDelayForMode(int gameMode);
};

#endif // SERVER_BOTS_HPP
