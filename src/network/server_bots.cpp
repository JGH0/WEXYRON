/**
 * @file server_bots.cpp
 * @brief Dedicated-server host for the portable bot AI (see server_bots.hpp)
 */

#include "server_bots.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>

namespace {
constexpr float kBotTick = 1.0f / 60.0f;
constexpr float kProjectileGravity = 15.0f;
constexpr float kGrenadeRadius = 4.0f;
constexpr float kGrenadeDamage = 65.0f;

float clampf(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}
} // namespace

struct ServerBotManager::Runtime {
    uint32_t id = 0;
    int slot = 0;
    uint8_t team = 0;
    std::string mapName;
    int gameMode = 0;
    bool assigned = true;   ///< in the active roster (not parked)
    uint8_t weaponId = 0;   ///< weapon the bot is currently firing/holding
    std::unique_ptr<bots::Bot> ai;
};

ServerBotManager::ServerBotManager() = default;
ServerBotManager::~ServerBotManager() = default;

bool ServerBotManager::init(NetworkServer& server, const Config& config) {
    config_ = config;
    server_ = &server;
    config_.count = std::max(0, std::min(config_.count, kMaxBots));
    config_.accuracy = clampf(config_.accuracy, 0.0f, 1.0f);

    if (config_.count <= 0) {
        std::cout << "[Bots] Disabled (bots.count = 0)" << std::endl;
        return true;
    }

    maps_.clear();
    bots_.clear();
    projectiles_.clear();
    accumulator_ = 0.0f;

    const bots::BotMap* map = getMap(config_.defaultMap);

    // Balance the initial teams using the players already connected.
    int teamCounts[2] = {0, 0};
    for (const ServerPlayer* player : server.getPlayers()) {
        if (player->active && !player->isBot) teamCounts[player->team & 1]++;
    }

    for (int i = 0; i < config_.count; ++i) {
        const uint8_t team = static_cast<uint8_t>(teamCounts[0] <= teamCounts[1] ? 0 : 1);
        teamCounts[team]++;

        const bots::SpawnResult spawn = map->findSpawn(false, team, rng_);
        const uint32_t id = server.addBot(config_.defaultMap, team, spawn.position);
        if (id == 0) continue;

        auto runtime = std::make_unique<Runtime>();
        runtime->id = id;
        runtime->slot = i;
        runtime->team = team;
        runtime->mapName = config_.defaultMap;
        runtime->gameMode = 0;

        // A little per-bot variation around the configured accuracy, so a
        // group of bots is not a firing squad of identical aim.
        std::uniform_real_distribution<float> jitter(-0.07f, 0.07f);
        const float accuracy = clampf(config_.accuracy + jitter(rng_), 0.0f, 1.0f);
        runtime->ai = std::make_unique<bots::Bot>(
            id, team, accuracy, static_cast<uint32_t>(0x9E3779B9u * (i + 1) + 0x85EBCA6Bu));
        runtime->ai->setGameMode(0);
        runtime->ai->teleport(spawn.position, spawn.yaw, 0.0f, *map);

        bots_.push_back(std::move(runtime));
    }

    assignedTeamA_ = 0;
    assignedTeamB_ = 0;
    for (const auto& runtime : bots_) {
        if (runtime->team == 0) ++assignedTeamA_;
        else ++assignedTeamB_;
    }
    currentAccuracy_ = config_.accuracy;

    std::cout << "[Bots] " << bots_.size() << " bot(s) active, accuracy "
              << config_.accuracy << ", world " << config_.defaultMap << std::endl;
    return true;
}

void ServerBotManager::applyTeamSetup(NetworkServer& server, int teamA, int teamB,
                                      float accuracy) {
    if (bots_.empty()) return;

    const int pool = static_cast<int>(bots_.size());
    teamA = std::max(0, std::min(teamA, pool));
    teamB = std::max(0, std::min(teamB, pool - teamA));
    accuracy = clampf(accuracy, 0.0f, config_.accuracy);

    assignedTeamA_ = teamA;
    assignedTeamB_ = teamB;
    currentAccuracy_ = accuracy;

    for (size_t i = 0; i < bots_.size(); ++i) {
        Runtime& runtime = *bots_[i];
        ServerPlayer* player = server.getPlayer(runtime.id);
        if (!player) continue;

        const bool assigned = static_cast<int>(i) < teamA + teamB;
        const uint8_t team = assigned
            ? (static_cast<int>(i) < teamA ? 0 : 1)
            : runtime.team;

        runtime.assigned = assigned;
        runtime.team = team;
        runtime.ai->setTeam(team);
        runtime.ai->setAccuracy(accuracy);
        player->team = team;

        if (assigned) {
            const bots::BotMap* map = getMap(runtime.mapName);
            // Team spawns are also valid DM spawns; using them here puts the
            // pre-match roster on the correct side before the mode is known.
            const bots::SpawnResult spawn =
                map->findSpawn(true, spawnSideFor(*player, runtime.gameMode,
                                                  runtime.mapName), rng_);
            player->position = spawn.position;
            player->velocity = glm::vec3(0.0f);
            player->yaw = spawn.yaw;
            player->pitch = 0.0f;
            player->health = 100.0f;
            player->isDead = false;
            runtime.ai->respawn(spawn.position, spawn.yaw, *map);
            server.broadcastPlayerStateToMap(
                runtime.id, runtime.mapName, player->position, player->velocity,
                player->yaw, player->pitch, player->health, player->isDead, true);
        } else {
            // Parked bot: away from the playable volume and marked dead so no
            // client renders it. It comes back on the next assignment.
            player->position = glm::vec3(0.0f, -100.0f, 0.0f);
            player->velocity = glm::vec3(0.0f);
            player->health = 0.0f;
            player->isDead = true;
            server.broadcastPlayerStateToMap(
                runtime.id, runtime.mapName, player->position, player->velocity,
                player->yaw, player->pitch, 0.0f, true, true);
        }
    }

    std::cout << "[Bots] Lobby setup: " << assignedTeamA_ << " vs "
              << assignedTeamB_ << " (accuracy " << accuracy << ")" << std::endl;
}

std::string ServerBotManager::resolveMapPath(const std::string& mapName) const {
    std::vector<std::string> candidates;
    if (!config_.mapDirectory.empty()) candidates.push_back(config_.mapDirectory + "/" + mapName);
    candidates.push_back("assets/maps/" + mapName);
    candidates.push_back(mapName);
    for (const std::string& path : candidates) {
        std::ifstream test(path);
        if (test.good()) return path;
    }
    return std::string();
}

const bots::BotMap* ServerBotManager::getMap(const std::string& mapName) {
    const std::string key = mapName.empty() ? config_.defaultMap : mapName;
    auto found = maps_.find(key);
    if (found != maps_.end()) return found->second.get();

    auto map = std::make_unique<bots::BotMap>();
    const std::string path = resolveMapPath(key);
    if (path.empty() || !map->load(path)) {
        std::cerr << "[Bots] Could not load map '" << key
                  << "' - using a fallback floor" << std::endl;
        *map = bots::BotMap::fallback(key);
    }
    const bots::BotMap* result = map.get();
    maps_[key] = std::move(map);
    return result;
}

void ServerBotManager::update(float deltaTime, NetworkServer& server) {
    if (bots_.empty()) return;

    // The main server loop can spin much faster than the game tick. Run the AI
    // at a fixed rate so packets and CPU stay bounded.
    accumulator_ += clampf(deltaTime, 0.0f, 0.25f);
    int guard = 0;
    while (accumulator_ >= kBotTick && guard++ < 8) {
        tick(kBotTick, server);
        accumulator_ -= kBotTick;
    }
    if (accumulator_ > 0.5f) accumulator_ = 0.0f;
}

void ServerBotManager::tick(float deltaTime, NetworkServer& server) {
    // No human on the server: bots must not move, aim, shoot, throw or fight
    // each other. They settle onto the floor and wait for a player to connect.
    if (server.getHumanPlayerCount() == 0) {
        if (!idle_) {
            idle_ = true;
            projectiles_.clear();
            std::cout << "[Bots] Idle - no players connected" << std::endl;
        }
        for (std::unique_ptr<Runtime>& runtimePtr : bots_) {
            Runtime& runtime = *runtimePtr;
            ServerPlayer* player = server.getPlayer(runtime.id);
            if (!player || player->isDead) continue;
            const bots::BotMap* map = getMap(runtime.mapName);
            runtime.ai->idle(deltaTime, map);
            player->position = runtime.ai->position();
            player->velocity = runtime.ai->velocity();
            player->yaw = runtime.ai->yaw();
            player->pitch = runtime.ai->pitch();
            player->weaponId = runtime.weaponId;
        }
        return;
    }

    if (idle_) {
        idle_ = false;
        std::cout << "[Bots] Active - player(s) connected" << std::endl;
    }

    std::vector<ServerPlayer*> humans;
    for (ServerPlayer* player : server.getPlayers()) {
        // Only players who are actually sending game input choose the bot
        // world; a client sitting on a menu must not drag bots to its map.
        if (player->active && !player->isBot && player->inputIdle <= 1.5f) {
            humans.push_back(player);
        }
    }

    // NOTE: a round-based client only sends PACKET_ROUND_START from
    // Game::startRound(). Do *not* infer a new round from a dead->alive
    // transition of a human player: a lost damage packet (or any other
    // resurrection of the server record) also flips that flag mid-round and
    // used to revive every dead bot in the middle of a round.

    for (std::unique_ptr<Runtime>& runtimePtr : bots_) {
        Runtime& runtime = *runtimePtr;
        ServerPlayer* player = server.getPlayer(runtime.id);
        if (!player) continue;
        // Parked bots (not allocated to a team yet) take no part in the game.
        if (!runtime.assigned) continue;

        // Follow the player world/game mode. Bots are spread over the human
        // list, so if players are on two maps both maps get bots.
        ServerPlayer* follow = nullptr;
        if (!humans.empty()) {
            follow = humans[static_cast<size_t>(runtime.slot) % humans.size()];
        }

        const std::string targetMap = follow
            ? (follow->mapName[0] ? std::string(follow->mapName) : config_.defaultMap)
            : runtime.mapName;
        const int targetMode = follow
            ? std::max(0, std::min(2, static_cast<int>(follow->gameMode)))
            : runtime.gameMode;

        if (targetMap != runtime.mapName) {
            switchBotWorld(server, runtime, targetMap, targetMode);
            player = server.getPlayer(runtime.id);
            if (!player) continue;
        }

        runtime.gameMode = targetMode;
        player->gameMode = static_cast<uint8_t>(targetMode);
        runtime.ai->setGameMode(targetMode);

        // Dead bots stay dead in bomb defusal (like human players in
        // spectator); they are revived only when a new round starts. In
        // deathmatch and team deathmatch they respawn after the normal delay.
        if (player->isDead) {
            if (runtime.ai->alive()) {
                runtime.ai->markDead(respawnDelayForMode(targetMode));
            }

            if (targetMode != 2) {
                static const bots::World kEmptyWorld;
                std::vector<bots::Action> noActions;
                runtime.ai->update(deltaTime, kEmptyWorld, noActions);
                if (runtime.ai->respawnTimer() <= 0.0f) {
                    respawnBot(server, runtime);
                    continue;
                }
            }

            server.broadcastPlayerStateToMap(
                runtime.id, runtime.mapName, player->position, player->velocity,
                player->yaw, player->pitch, player->health, player->isDead, false);
            continue;
        }

        // The server may have revived the record from outside (map switch).
        // In bomb defusal a dead AI must stay dead: the only revival is the
        // explicit next-round path (reviveBotsForNewRound).
        const bots::BotMap* map = getMap(runtime.mapName);
        if (!runtime.ai->alive()) {
            if (targetMode != 2) {
                runtime.ai->respawn(player->position, player->yaw, *map);
            } else {
                player->isDead = true;
                player->health = 0.0f;
                server.broadcastPlayerStateToMap(
                    runtime.id, runtime.mapName, player->position, player->velocity,
                    player->yaw, player->pitch, player->health, true, true);
                continue;
            }
        }

        const BombInfo bomb = bombInfoProvider_
                                  ? bombInfoProvider_(runtime.mapName)
                                  : BombInfo{};
        // Round ended / intermission or round-start freeze: hold the bot
        // completely. No movement, aiming, shooting or throwables until the
        // freeze / next round releases it.
        if (targetMode == 2 && bomb.valid &&
            (!bomb.roundActive || bomb.movementFrozen)) {
            runtime.ai->idle(deltaTime, map);
            player->position = runtime.ai->position();
            player->velocity = runtime.ai->velocity();
            player->yaw = runtime.ai->yaw();
            player->pitch = runtime.ai->pitch();
            player->weaponId = runtime.weaponId;
            server.broadcastPlayerStateToMap(
                runtime.id, runtime.mapName, player->position, player->velocity,
                player->yaw, player->pitch, player->health, player->isDead, false);
            continue;
        }

        bots::World world;
        world.map = map;
        world.gameMode = targetMode;
        world.friendlyFire = config_.friendlyFire;
        if (bomb.valid) {
            world.roundActive = bomb.roundActive;
            world.bombPlanted = bomb.planted && !bomb.defused && !bomb.exploded;
            world.bombDefused = bomb.defused;
            world.bombPosition = bomb.position;
            world.bombPlantingTeam = bomb.plantingTeam;
            world.attackingTeam = bomb.attackingTeam;
        }
        for (ServerPlayer* other : server.getPlayers()) {
            if (!other->active || other->isDead || other->id == player->id) continue;
            if (std::string(other->mapName) != runtime.mapName) continue;
            // Do not shoot players who are still in a menu / not sending game
            // input yet (the server otherwise sees stale spawn positions).
            if (!other->isBot && other->inputIdle > 1.5f) continue;
            world.targets.push_back({other->id, other->position,
                                     other->team, true, other->isBot});
        }

        std::vector<bots::Action> actions;
        runtime.ai->update(deltaTime, world, actions);
        applyActions(server, runtime, actions);

        if (player->health <= 0.0f && !player->isDead) {
            player->health = 0.0f;
            player->isDead = true;
            runtime.ai->markDead(respawnDelayForMode(targetMode));
            std::cout << "[Server] Bot " << runtime.id << " died" << std::endl;
        }

        // The AI owns the body; copy it into the authoritative record.
        player->position = runtime.ai->position();
        player->velocity = runtime.ai->velocity();
        player->yaw = runtime.ai->yaw();
        player->pitch = runtime.ai->pitch();
        player->weaponId = runtime.weaponId;

        server.broadcastPlayerStateToMap(
            runtime.id, runtime.mapName, player->position, player->velocity,
            player->yaw, player->pitch, player->health, player->isDead, false);
    }

    updateProjectiles(deltaTime, server);
}

int ServerBotManager::spawnSideFor(const ServerPlayer& player, int gameMode,
                                   const std::string& mapName) const {
    int side = player.team & 1;
    if (gameMode == 2 && bombInfoProvider_) {
        const BombInfo info = bombInfoProvider_(mapName);
        if (info.valid) side ^= info.attackingTeam & 1;
    }
    return side & 1;
}

void ServerBotManager::switchBotWorld(NetworkServer& server, Runtime& runtime,
                                      const std::string& mapName, int gameMode) {
    ServerPlayer* player = server.getPlayer(runtime.id);
    if (!player) return;

    const bots::BotMap* map = getMap(mapName);
    const bots::SpawnResult spawn =
        map->findSpawn(gameMode >= 1, spawnSideFor(*player, gameMode, mapName), rng_);

    server.movePlayerToMap(runtime.id, mapName, spawn.position);
    player = server.getPlayer(runtime.id);
    if (!player) return;

    player->position = spawn.position;
    player->velocity = glm::vec3(0.0f);
    player->yaw = spawn.yaw;
    player->pitch = 0.0f;
    player->health = 100.0f;
    player->isDead = false;
    player->gameMode = static_cast<uint8_t>(gameMode);

    runtime.mapName = mapName;
    runtime.gameMode = gameMode;
    runtime.ai->respawn(spawn.position, spawn.yaw, *map);

    std::cout << "[Server] Bot " << runtime.id << " follows world " << mapName
              << " (mode " << gameMode << ")" << std::endl;
    server.broadcastPlayerStateToMap(
        runtime.id, runtime.mapName, player->position, player->velocity,
        player->yaw, player->pitch, player->health, player->isDead, true);
}

void ServerBotManager::respawnBot(NetworkServer& server, Runtime& runtime) {
    ServerPlayer* player = server.getPlayer(runtime.id);
    if (!player) return;

    const bots::BotMap* map = getMap(runtime.mapName);
    const bots::SpawnResult spawn =
        map->findSpawn(runtime.gameMode >= 1,
                       spawnSideFor(*player, runtime.gameMode, runtime.mapName), rng_);

    player->position = spawn.position;
    player->velocity = glm::vec3(0.0f);
    player->yaw = spawn.yaw;
    player->pitch = 0.0f;
    player->health = 100.0f;
    player->isDead = false;
    runtime.ai->respawn(spawn.position, spawn.yaw, *map);

    std::cout << "[Server] Bot " << runtime.id << " respawned" << std::endl;
    server.broadcastPlayerStateToMap(
        runtime.id, runtime.mapName, player->position, player->velocity,
        player->yaw, player->pitch, player->health, player->isDead, true);
}

void ServerBotManager::reviveBotsForNewRound(NetworkServer& server, const std::string& mapName,
                                             int gameMode) {
    const bots::BotMap* map = getMap(mapName);
    for (std::unique_ptr<Runtime>& runtimePtr : bots_) {
        Runtime& runtime = *runtimePtr;
        if (runtime.mapName != mapName) continue;

        ServerPlayer* player = server.getPlayer(runtime.id);
        if (!player) continue;

        // Every bot starts the new round at a spawn point. Living bots used to
        // be skipped here, so a bot that survived the previous round stayed
        // frozen wherever it happened to be when the round ended.
        const bots::SpawnResult spawn =
            map->findSpawn(gameMode >= 1,
                           spawnSideFor(*player, gameMode, mapName), rng_);

        runtime.gameMode = gameMode;
        runtime.ai->setGameMode(gameMode);
        runtime.ai->respawn(spawn.position, spawn.yaw, *map);

        player->position = spawn.position;
        player->velocity = glm::vec3(0.0f);
        player->yaw = spawn.yaw;
        player->pitch = 0.0f;
        player->health = 100.0f;
        player->isDead = false;
        player->gameMode = static_cast<uint8_t>(gameMode);

        std::cout << "[Server] Bot " << runtime.id
                  << " revived for a new round" << std::endl;
        server.broadcastPlayerStateToMap(
            runtime.id, runtime.mapName, player->position, player->velocity,
            player->yaw, player->pitch, player->health, player->isDead, true);
    }
}

void ServerBotManager::startRound(const std::string& mapName, int gameMode) {
    // Only bomb defusal has rounds now; team deathmatch respawns normally.
    if (!server_ || bots_.empty() || gameMode != 2) return;
    reviveBotsForNewRound(*server_, mapName, gameMode);
}

void ServerBotManager::applyActions(NetworkServer& server, Runtime& runtime,
                                    const std::vector<bots::Action>& actions) {
    for (const bots::Action& action : actions) {
        // Remember which weapon produced this action so clients can render
        // the correct gun in the bot's hands.
        switch (action.projectileType) {
            case 3:  runtime.weaponId = 1; break;   // shotgun
            case 4:  runtime.weaponId = 2; break;   // sniper
            case 1:  runtime.weaponId = 3; break;   // grenade
            case 2:  runtime.weaponId = 4; break;   // flashbang
            default: runtime.weaponId = 0; break;   // pistol
        }

        switch (action.type) {
            case bots::Action::FireBullet:
                server.broadcastProjectileSpawnToMap(
                    runtime.mapName, action.origin, action.velocity, action.lifetime,
                    action.projectileType, runtime.id);
                break;

            case bots::Action::ThrowGrenade:
            case bots::Action::ThrowFlashbang: {
                server.broadcastProjectileSpawnToMap(
                    runtime.mapName, action.origin, action.velocity, action.lifetime,
                    action.projectileType, runtime.id);

                ServerProjectile projectile;
                projectile.ownerId = runtime.id;
                projectile.mapName = runtime.mapName;
                projectile.position = action.origin;
                projectile.velocity = action.velocity;
                projectile.lifetime = action.lifetime;
                projectile.type = action.projectileType;
                projectiles_.push_back(projectile);
                break;
            }
        }

        if (action.victimId != 0 && action.damage > 0 && onDamage) {
            onDamage(runtime.id, action.victimId, action.damage, action.weaponId);
        }
    }
}

void ServerBotManager::onProjectileSpawned(uint32_t ownerId,
                                           const ProjectileSpawnPacket& packet) {
    // Bot projectiles are already simulated (they are created locally by
    // applyActions). Only a human flashbang needs a server-side copy so bots
    // can be blinded. Grenade damage against bots is reported by the human
    // shooter through PACKET_PLAYER_DAMAGE.
    if (packet.projectileType != 2 || !server_) return;

    ServerPlayer* owner = server_->getPlayer(ownerId);
    if (!owner || owner->isBot) return;

    ServerProjectile projectile;
    projectile.ownerId = ownerId;
    projectile.mapName = owner->mapName;
    projectile.position = glm::vec3(packet.posX, packet.posY, packet.posZ);
    projectile.velocity = glm::vec3(packet.velX, packet.velY, packet.velZ);
    projectile.lifetime = packet.lifetime;
    projectile.type = packet.projectileType;
    projectiles_.push_back(projectile);
}

void ServerBotManager::updateProjectiles(float deltaTime, NetworkServer& server) {
    for (size_t i = 0; i < projectiles_.size();) {
        ServerProjectile& projectile = projectiles_[i];
        const bots::BotMap* map = getMap(projectile.mapName);

        const glm::vec3 previous = projectile.position;
        projectile.velocity.y -= kProjectileGravity * deltaTime;
        projectile.position += projectile.velocity * deltaTime;
        projectile.lifetime -= deltaTime;

        bool detonateNow = projectile.lifetime <= 0.0f;
        if (!detonateNow && map) {
            if (map->lineBlocked(previous, projectile.position)) detonateNow = true;
            const glm::vec3 half = map->dimensions * 0.5f + glm::vec3(2.0f);
            if (std::abs(projectile.position.x) > half.x ||
                std::abs(projectile.position.z) > half.z ||
                projectile.position.y < -10.0f) {
                detonateNow = true;
            }
        }

        if (detonateNow) {
            detonate(projectile, server);
            projectiles_.erase(projectiles_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void ServerBotManager::detonate(const ServerProjectile& projectile, NetworkServer& server) {
    if (projectile.type == 2) {
        // Blind every bot on this map that can see the flash.
        int blinded = 0;
        for (std::unique_ptr<Runtime>& runtime : bots_) {
            if (runtime->mapName != projectile.mapName) continue;
            if (!runtime->ai->alive()) continue;
            const bots::BotMap* map = getMap(runtime->mapName);
            runtime->ai->applyFlash(projectile.position, *map);
            if (runtime->ai->blinded()) ++blinded;
        }
        if (blinded > 0) {
            std::cout << "[Bots] Flashbang blinded " << blinded << " bot(s)" << std::endl;
        }
        return;
    }

    if (projectile.type != 1) return;

    ServerPlayer* owner = server.getPlayer(projectile.ownerId);
    if (!owner || !owner->isBot) return; // human grenades are resolved client-side

    const int gameMode = owner->gameMode;
    for (ServerPlayer* victim : server.getPlayers()) {
        if (!victim->active || victim->isDead || victim->id == owner->id) continue;
        if (std::string(victim->mapName) != projectile.mapName) continue;
        if (gameMode >= 1 && !config_.friendlyFire && victim->team == owner->team) continue;

        const float distance = bots::distanceToBody(
            projectile.position, victim->position,
            stanceBodyHeight(victim->stance));
        if (distance >= kGrenadeRadius) continue;

        // Explosions have no distance falloff.
        const uint16_t damage = static_cast<uint16_t>(std::max(1.0f, kGrenadeDamage));
        if (onDamage) onDamage(owner->id, victim->id, damage, 1);
    }
}

float ServerBotManager::respawnDelayForMode(int gameMode) {
    return gameMode == 2 ? 5.0f : 2.5f;
}
