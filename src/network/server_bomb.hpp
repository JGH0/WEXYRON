/**
 * @file server_bomb.hpp
 * @brief Authoritative bomb-defusal + round state for the dedicated server
 *
 * Planting used to happen only inside the client's Game::updateBomb(). That
 * meant every client had its own bomb: other players could not see it, bots
 * had no idea it existed, and a client could plant/defuse simply by holding E.
 *
 * This manager owns one bomb/round state per map. Clients only send an
 * *intent* (PACKET_BOMB_INTERACT); the server checks team, distance and round
 * state, advances planting/defusing/explosion, decides the round winner, and
 * broadcasts PACKET_BOMB_STATE. Bots are first-class actors here too: a bot
 * that walks onto a site plants, and a defending bot that reaches a planted
 * bomb defuses it.
 */

#ifndef SERVER_BOMB_HPP
#define SERVER_BOMB_HPP

#include "network.hpp"
#include "../game/bot.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

class ServerBombManager {
public:
    struct Config {
        std::string mapDirectory = "assets/maps";
        float plantTime = 3.0f;
        float defuseTime = 4.0f;
        float explodeTime = 20.0f;
        /// Seconds to plant before the defenders win on time (CS-style).
        float roundTime = 115.0f;
        /// Horizontal/vertical distance from the site/bomb that counts as
        /// "at" the objective. A little generous so bots standing on a nearby
        /// navigation node can use it.
        float useRange = 2.5f;
        float grenadeRadius = 12.0f;
    };

    /// Public snapshot used by the bot AI. `valid` is false when no round has
    /// ever run on this map.
    struct Snapshot {
        bool valid = false;
        bool roundActive = false;
        bool planted = false;
        bool defused = false;
        bool exploded = false;
        bool roundEnded = false;
        int gameMode = 0;
        int attackingTeam = 0;
        int plantingTeam = -1;
        int roundWinner = -1;
        int roundEndReason = 0;
        glm::vec3 position{0.0f};
        int siteIndex = -1;
        char siteLabel = 'A';
        float timeLeft = 0.0f;
        float roundTimeLeft = 0.0f;
        float plantProgress = 0.0f;
        float defuseProgress = 0.0f;
    };

    ServerBombManager();
    ~ServerBombManager();

    bool init(NetworkServer& server, const Config& config);

    /// Advance bombs, auto-interactions by bots, and periodic broadcasts.
    void update(float deltaTime, NetworkServer& server);

    /// A client pressed E on a site / near a planted bomb.
    void onBombInteract(uint32_t playerId, uint8_t action);

    /// A round-based client started the next round on `mapName`.
    void onRoundStart(const std::string& mapName, int gameMode, int attackingTeam);

    /// Drop interaction requests owned by a player who left.
    void onPlayerDisconnected(uint32_t playerId);

    /// Bomb state for the bot AI / tests.
    Snapshot snapshot(const std::string& mapName) const;

    bool enabled() const { return server_ != nullptr; }
    bool roundActive(const std::string& mapName) const;

    /// Damage produced by the bomb explosion. Implemented by the server so
    /// players and bots die exactly like they would from any other blast.
    std::function<void(uint32_t shooterId, uint32_t victimId,
                       uint16_t damage, uint8_t weaponId)> onDamage;

private:
    struct MapState {
        int gameMode = 0;
        bool roundActive = false;
        bool hasHumans = false;        ///< a human is currently on this map
        uint8_t attackingTeam = 0;
        float roundTimeLeft = 0.0f;
        bool roundEnded = false;
        uint8_t roundWinner = 255;     ///< 255 = draw
        uint8_t roundEndReason = ROUND_END_NONE;
        bool planted = false;
        bool defused = false;
        bool exploded = false;
        glm::vec3 position{0.0f};
        int siteIndex = -1;
        char siteLabel = 'A';
        uint8_t plantingTeam = 255;
        uint32_t plantingPlayer = 0;
        float plantedTime = 0.0f;      ///< seconds since the bomb was planted
        float plantProgress = 0.0f;
        float defuseProgress = 0.0f;
        uint32_t plantActor = 0;
        uint32_t defuseActor = 0;
        float broadcastTimer = 0.0f;
        bool dirty = false;
    };

    Config config_;
    NetworkServer* server_ = nullptr;
    std::map<std::string, MapState> states_;
    std::map<std::string, std::unique_ptr<bots::BotMap>> maps_;
    /// Last requested action per player (valid until the next round / death).
    std::map<uint32_t, uint8_t> interact_;

    const bots::BotMap* getMap(const std::string& mapName);
    std::string resolveMapPath(const std::string& mapName) const;

    MapState& stateFor(const std::string& mapName);
    void resetRound(MapState& state, int gameMode, int attackingTeam);
    void resetBomb(MapState& state);

    /// Create/refresh map states from the players' last reported game mode.
    void observePlayers(NetworkServer& server);
    /// Pick the site nearest to a player, within the use range (-1 when none).
    int nearbySite(const bots::BotMap* map, const glm::vec3& position) const;

    /// Find the actor that may currently plant / defuse. Explicit human
    /// requests win; otherwise a nearby bot is selected automatically.
    ServerPlayer* chooseActor(const MapState& state, NetworkServer& server,
                              const std::string& mapName, bool defusing,
                              int& siteIndex);

    void broadcastState(const std::string& mapName, MapState& state,
                        NetworkServer& server);
    void explode(MapState& state, const std::string& mapName, NetworkServer& server);

    /// Finalise a round once (swap sides for the next round, mark dirty).
    void finishRound(MapState& state, int winner, RoundEndReason reason);
    /// Check elimination / time win conditions for one map.
    void checkRoundOutcome(MapState& state, const std::string& mapName,
                           NetworkServer& server);
};

#endif // SERVER_BOMB_HPP
