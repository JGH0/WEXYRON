/**
 * @file bot.hpp
 * @brief Portable bot AI used by the dedicated server (and, later, singleplayer)
 *
 * The bot has no dependency on OpenGL, GLFW or the network layer. It receives a
 * snapshot of the world (a small map geometry + the other bodies) and produces
 * actions (move is applied internally; shots / throwables / damage are returned
 * as Actions). The network host in src/network/server_bots.* translates those
 * actions into packets and ServerPlayer state.
 *
 * Bots are intentionally simple but complete:
 *   - they walk and collide with the authored map geometry,
 *   - they see enemies with a line-of-sight check and engage them,
 *   - they fire pistols / shotguns with an accuracy-driven hit chance,
 *   - they throw grenades and flashbangs,
 *   - a flashbang blinds them; while blind they wander randomly and cannot aim.
 *
 * Game modes: deathmatch (hunt everyone), team vs team (hunt the other team and
 * push the enemy side), bomb defusal (move to the bomb sites / hold them).
 */

#ifndef GAME_BOT_HPP
#define GAME_BOT_HPP

#include <glm/glm.hpp>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace bots {

/// One solid axis-aligned map block (world-space bounds).
struct Block {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

/// A spawn point authored in a .map file.
struct SpawnPoint {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

/// A team spawn point authored in a .map file.
struct TeamSpawnPoint {
    glm::vec3 position{0.0f};
    int team = 0;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

/// A bomb site (from a .map file, or the client's built-in fallback sites).
struct BombSite {
    glm::vec3 position{0.0f};
    char label = 'A';
};

/// Result of picking a spawn.
struct SpawnResult {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
};

/**
 * @brief Minimal map geometry used by bot navigation / line-of-sight.
 *
 * The client's Map class loads textures through OpenGL, which the headless
 * server cannot do. This parser reads the same .map text and keeps only the
 * collision boxes and spawn/bomb-site metadata the AI needs.
 */
class BotMap {
public:
    std::string name = "simple.map";
    glm::vec3 dimensions{20.0f, 10.0f, 20.0f};
    std::vector<Block> blocks;
    std::vector<SpawnPoint> spawns;
    std::vector<TeamSpawnPoint> teamSpawns;
    std::vector<BombSite> bombSites;

    /// One node of the walkable navigation grid (a safe standing position).
    struct NavNode {
        glm::vec3 feet{0.0f};
        std::vector<int> neighbors;
        int component = -1;   ///< connected walkable area id
    };

    /// Parse `path` and build the navigation grid. Returns false when the file
    /// cannot be opened.
    bool load(const std::string& path);

    /// An empty map with a floor at y = 0 (used when a map is missing so that
    /// bots still have somewhere to stand instead of falling forever).
    static BotMap fallback(const std::string& mapName);

    /// True when the axis-aligned box min..max overlaps a solid block.
    bool boxBlocked(const glm::vec3& min, const glm::vec3& max) const;

    /// True when the body standing at `feet` overlaps geometry / map bounds.
    bool bodyBlocked(const glm::vec3& feet) const;

    /// True when a wall blocks the straight segment a -> b.
    bool lineBlocked(const glm::vec3& a, const glm::vec3& b) const;

    /// Highest block top below the feet/radius footprint within maxDrop.
    bool groundBelow(const glm::vec3& feet, float radius, float maxDrop, float& height) const;

    /// Highest floor at (x, z) that is below `fromY` (or false when none).
    bool topUnder(float x, float z, float fromY, float& y) const;

    /// True when a body can stand at `feet` (not inside geometry, with ground).
    bool standable(const glm::vec3& feet) const;

    /// Pick a safe spawn. Uses authored spawns when possible, then navigation
    /// nodes (so a bot never spawns over a hole or inside geometry).
    SpawnResult findSpawn(bool teamMode, int team, std::mt19937& rng) const;

    /// A* path over the walkable grid. `path` is a list of feet positions from
    /// just after `from` through `to` (empty when no node/path is found).
    bool findPath(const glm::vec3& from, const glm::vec3& to,
                  std::vector<glm::vec3>& path) const;

    /// Random walkable nav node (used for wandering / fallback spawns).
    bool randomNavPoint(std::mt19937& rng, glm::vec3& point) const;

    size_t navNodeCount() const { return navNodes_.size(); }
    const std::vector<NavNode>& navigationNodes() const { return navNodes_; }

private:
    std::vector<NavNode> navNodes_;
    bool navReady_ = false;
    int mainComponent_ = -1;

    /// Build the walkable grid + neighbour links. Called once after load().
    void buildNavigation();

    /// Closest nav node to `feet` (index or -1). `maxDistance` is horizontal
    /// distance; the vertical difference is limited too.
    int nearestNode(const glm::vec3& feet, float maxDistance = 4.0f) const;

    /// True when a body moving along a -> b would pass through geometry.
    bool moveBlocked(const glm::vec3& a, const glm::vec3& b) const;
};

/// Another player / bot a bot can see and fight.
struct Target {
    uint32_t id = 0;
    glm::vec3 feet{0.0f};
    uint8_t team = 0;
    bool alive = true;
    bool isBot = false;
};

/// Snapshot passed to every bot for one AI tick.
struct World {
    const BotMap* map = nullptr;
    std::vector<Target> targets;
    int gameMode = 0;          ///< 0 = deathmatch, 1 = team vs team, 2 = bomb
    bool friendlyFire = false; ///< team modes: allow damaging teammates
};

/// Something a bot wants the host to do (damage is applied by the host).
struct Action {
    enum Type {
        FireBullet,     ///< broadcast a bullet/shotgun projectile (+ damage)
        ThrowGrenade,   ///< broadcast a grenade projectile
        ThrowFlashbang  ///< broadcast a flashbang projectile
    };

    Type type = FireBullet;
    glm::vec3 origin{0.0f};
    glm::vec3 velocity{0.0f};
    float lifetime = 1.0f;
    uint8_t projectileType = 0; ///< 0 pistol, 3 shotgun, 1 grenade, 2 flashbang
    uint32_t victimId = 0;      ///< 0 when the shot misses
    uint16_t damage = 0;
    uint8_t weaponId = 0;
};

/// One bot body + its AI state.
class Bot {
public:
    Bot(uint32_t id, uint8_t team, float accuracy, uint32_t randomSeed);

    uint32_t id() const { return id_; }
    uint8_t team() const { return team_; }
    float accuracy() const { return accuracy_; }

    const glm::vec3& position() const { return position_; }
    const glm::vec3& velocity() const { return velocity_; }
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    bool alive() const { return alive_; }
    float health() const { return health_; }
    float respawnTimer() const { return respawnTimer_; }
    bool blinded() const { return blindTimer_ > 0.0f; }
    void setGameMode(int mode) { gameMode_ = mode; }

    /// Put the body somewhere (map switch / server teleport). Does not change
    /// health or the alive flag.
    void teleport(const glm::vec3& feet, float yaw, float pitch, const BotMap& map);

    /// Mark the bot dead and start its respawn countdown.
    void markDead(float delay = 3.0f);

    /// Bring the bot back at `spawn`; resets health, ammo and AI timers.
    void respawn(const glm::vec3& spawn, float yaw, const BotMap& map);

    /// A flashbang detonated at `origin`: blind the bot when it has line of
    /// sight and is close enough (same rules as the client flash effect).
    void applyFlash(const glm::vec3& origin, const BotMap& map);

    /// Stop all AI activity (no movement, aiming, shooting or throwables) and
    /// settle vertically onto the floor. Used while no human is on the server.
    void idle(float dt, const BotMap* map);

    /// Run one AI tick. `dt` is clamped internally. Emits shots/throws.
    void update(float dt, const World& world, std::vector<Action>& actions);

private:
    uint32_t id_;
    uint8_t team_;
    float accuracy_;
    std::mt19937 rng_;

    glm::vec3 position_{0.0f};
    glm::vec3 velocity_{0.0f};
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    bool alive_ = true;
    float health_ = 100.0f;
    float respawnTimer_ = 0.0f;
    bool grounded_ = false;
    int gameMode_ = 0;

    // Perception / combat timers.
    float blindTimer_ = 0.0f;
    float reactionTimer_ = 0.0f;
    float fireTimer_ = 0.0f;
    float grenadeTimer_ = 0.0f;
    float flashTimer_ = 0.0f;
    int grenadeAmmo_ = 2;
    int flashAmmo_ = 2;
    float lastSeenTimer_ = 0.0f;
    glm::vec3 lastSeenPos_{0.0f};
    uint32_t lastTargetId_ = 0;

    // Movement / wander state.
    glm::vec3 objective_{0.0f};
    bool hasObjective_ = false;
    float objectiveTimer_ = 0.0f;
    float strafeTimer_ = 0.0f;
    int strafeDir_ = 1;
    glm::vec3 wanderTarget_{0.0f};
    float wanderTimer_ = 0.0f;
    float stuckTimer_ = 0.0f;
    glm::vec3 stuckOrigin_{0.0f};

    // Navigation path (feet positions from the map's walkable grid).
    std::vector<glm::vec3> path_;
    size_t pathIndex_ = 0;
    glm::vec3 pathGoal_{0.0f};
    float pathTimer_ = 0.0f;

    /// Current visible enemy, or nullptr.
    const Target* selectTarget(const World& world) const;
    /// Choose a mode-aware point to walk to when no enemy is visible.
    glm::vec3 chooseObjective(const World& world);
    /// Aim at `point` with a turn-rate limit.
    void aimAt(const glm::vec3& point, float dt);
    /// True when the current view direction is close to `point`.
    bool aimedAt(const glm::vec3& point) const;
    /// Fire at `target` and append the projectile/damage actions.
    void fireAt(const Target& target, float distance, std::vector<Action>& actions);
    /// Occasionally throw a grenade or flashbang at a visible target.
    void tryThrowable(const Target& target, float distance, std::vector<Action>& actions);

    /// Move the body horizontally with collision + simple step-up.
    void moveHorizontal(float dt, const BotMap* map, const glm::vec3& dir, float speed);
    /// Apply gravity / snap to the floor.
    void updateVertical(float dt, const BotMap* map);
    /// True when moving would leave the authored map bounds.
    bool outsideBounds(const glm::vec3& feet, const BotMap* map) const;
    /// Random point around `origin` that has floor and is not blocked.
    glm::vec3 randomReachablePoint(const BotMap* map, const glm::vec3& origin);

    /// Path following: returns the direction to the next waypoint toward
    /// `goal`, recomputing the A* path when the goal moves or the path expires.
    glm::vec3 pathDirection(const BotMap* map, const glm::vec3& goal, float dt);
    void clearPath();

    float random01();
    float randomRange(float lo, float hi);
};

/// Distance from `point` to the upright body box standing at `feet`.
float distanceToBody(const glm::vec3& point, const glm::vec3& feet);

} // namespace bots

#endif // GAME_BOT_HPP
