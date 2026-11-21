#include "local_bots.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
constexpr float kBotTick = 1.0f / 60.0f;
constexpr float kRespawnDelay = 2.5f;

float clampf(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}
} // namespace

struct LocalBotHost::Runtime {
    uint32_t id = 0;
    uint8_t team = 0;
    std::unique_ptr<bots::Bot> ai;
    float health = 100.0f;
    bool dead = false;
    bool shooting = false;
};

LocalBotHost::LocalBotHost() = default;
LocalBotHost::~LocalBotHost() = default;

bool LocalBotHost::init(const Config& config) {
    stop();
    config_ = config;
    config_.botsTeamA = std::max(0, config_.botsTeamA);
    config_.botsTeamB = std::max(0, config_.botsTeamB);
    config_.accuracy = clampf(config_.accuracy, 0.0f, 1.0f);
    attackingTeam_ = config_.attackingTeam & 1;

    map_ = std::make_unique<bots::BotMap>();
    if (!map_->load(config_.mapPath)) {
        std::cerr << "[LocalBots] Could not load '" << config_.mapPath
                  << "' - using a fallback floor" << std::endl;
        *map_ = bots::BotMap::fallback(config_.mapName);
    }

    const int total = config_.botsTeamA + config_.botsTeamB;
    const bool teamMode = config_.gameMode >= 1;
    for (int i = 0; i < total; ++i) {
        const uint8_t team = static_cast<uint8_t>(i < config_.botsTeamA ? 0 : 1);
        const bots::SpawnResult spawn = map_->findSpawn(teamMode, spawnSide(team), rng_);

        auto runtime = std::make_unique<Runtime>();
        runtime->id = kFirstBotId + static_cast<uint32_t>(i);
        runtime->team = team;
        runtime->ai = std::make_unique<bots::Bot>(
            runtime->id, team, config_.accuracy,
            static_cast<uint32_t>(0x9E3779B9u * (i + 1) + 0x85EBCA6Bu));
        runtime->ai->setGameMode(config_.gameMode);
        runtime->ai->teleport(spawn.position, spawn.yaw, 0.0f, *map_);
        bots_.push_back(std::move(runtime));
    }

    std::cout << "[LocalBots] " << total << " bot(s): " << config_.botsTeamA
              << " vs " << config_.botsTeamB << ", accuracy " << config_.accuracy
              << std::endl;
    return true;
}

void LocalBotHost::stop() {
    bots_.clear();
    map_.reset();
    playerKills_ = 0;
    teamKills_[0] = 0;
    teamKills_[1] = 0;
    attackingTeam_ = 0;
    accumulator_ = 0.0f;
}

uint8_t LocalBotHost::spawnSide(uint8_t team) const {
    // In Bomb Defusal the physical side follows the current role, so both
    // teams move to the other side of the map after the half-time switch.
    if (config_.gameMode == 2) return static_cast<uint8_t>((team ^ attackingTeam_) & 1);
    return team & 1;
}

void LocalBotHost::respawnBot(Runtime& bot) {
    if (!map_) return;
    const bots::SpawnResult spawn =
        map_->findSpawn(config_.gameMode >= 1, spawnSide(bot.team), rng_);
    bot.health = 100.0f;
    bot.dead = false;
    bot.shooting = false;
    bot.ai->respawn(spawn.position, spawn.yaw, *map_);
}

void LocalBotHost::startNewRound(int attackingTeam) {
    if (!map_ || config_.gameMode != 2) return;
    attackingTeam_ = attackingTeam & 1;
    for (const auto& runtime : bots_) {
        respawnBot(*runtime);
    }
}

LocalBotHost::Runtime* LocalBotHost::findBot(uint32_t id) {
    for (const auto& runtime : bots_) {
        if (runtime->id == id) return runtime.get();
    }
    return nullptr;
}

void LocalBotHost::registerKill(int team) {
    const int index = team & 1;
    ++teamKills_[index];
}

void LocalBotHost::update(float deltaTime, const glm::vec3& playerFeet, int playerTeam,
                          bool playerAlive, const PlayerDamageFn& damagePlayer,
                          const ShotFn& onShot) {
    if (!map_ || bots_.empty()) return;

    // Keep the AI on a fixed clock, like the dedicated-server host.
    accumulator_ += clampf(deltaTime, 0.0f, 0.25f);
    int guard = 0;
    while (accumulator_ >= kBotTick && guard++ < 8) {
        tick(kBotTick, playerFeet, playerTeam, playerAlive, damagePlayer, onShot);
        accumulator_ -= kBotTick;
    }
    if (accumulator_ > 0.5f) accumulator_ = 0.0f;
}

void LocalBotHost::tick(float deltaTime, const glm::vec3& playerFeet, int playerTeam,
                        bool playerAlive, const PlayerDamageFn& damagePlayer,
                        const ShotFn& onShot) {
    for (const auto& runtime : bots_) runtime->shooting = false;

    for (const auto& actorPtr : bots_) {
        Runtime& actor = *actorPtr;

        if (actor.dead) {
            // Bomb defusal is round-based: a dead bot waits for the next
            // explicit round start. Deathmatch modes use the AI timer.
            if (config_.gameMode == 2) continue;

            // The AI owns its respawn timer; an empty world only advances it.
            static const bots::World kEmptyWorld;
            std::vector<bots::Action> none;
            actor.ai->update(deltaTime, kEmptyWorld, none);
            if (actor.ai->respawnTimer() <= 0.0f) respawnBot(actor);
            continue;
        }

        bots::World world;
        world.map = map_.get();
        world.gameMode = config_.gameMode;
        world.friendlyFire = config_.friendlyFire;

        // The local player is just another target body.
        if (playerAlive) {
            world.targets.push_back({kLocalPlayerId, playerFeet,
                                     static_cast<uint8_t>(playerTeam & 1), true, false});
        }
        for (const auto& otherPtr : bots_) {
            const Runtime& other = *otherPtr;
            if (other.id == actor.id || other.dead) continue;
            world.targets.push_back({other.id, other.ai->position(), other.team, true, true});
        }

        std::vector<bots::Action> actions;
        actor.ai->update(deltaTime, world, actions);

        for (const bots::Action& action : actions) {
            if (action.type != bots::Action::FireBullet) {
                // Throwables are intentionally not simulated offline yet; the
                // AI still fights with its firearms.
                continue;
            }

            actor.shooting = true;
            if (onShot) {
                onShot(action.origin, action.velocity, action.projectileType);
            }
            if (action.damage == 0 || action.victimId == 0) continue;

            if (action.victimId == kLocalPlayerId) {
                if (damagePlayer) damagePlayer(static_cast<float>(action.damage));
                continue;
            }

            Runtime* victim = findBot(action.victimId);
            if (!victim || victim->dead) continue;
            victim->health -= static_cast<float>(action.damage);
            if (victim->health > 0.0f) continue;

            victim->health = 0.0f;
            victim->dead = true;
            victim->ai->markDead(kRespawnDelay);
            // Team kills are a Team Deathmatch scoreboard only.
            if (config_.gameMode == 1 && victim->team != actor.team) {
                registerKill(actor.team);
            }
        }
    }
}

bool LocalBotHost::damageBot(uint32_t id, float damage, int playerTeam) {
    Runtime* bot = findBot(id);
    if (!bot || bot->dead || damage <= 0.0f) return false;

    bot->health -= damage;
    if (bot->health > 0.0f) return true;

    bot->health = 0.0f;
    bot->dead = true;
    bot->ai->markDead(kRespawnDelay);

    const bool enemy = config_.gameMode == 0 ||
                       bot->team != static_cast<uint8_t>(playerTeam & 1);
    if (enemy) {
        ++playerKills_;
        // Only Team Deathmatch keeps a team kill scoreboard.
        if (config_.gameMode == 1) registerKill(playerTeam);
    }
    return true;
}

void LocalBotHost::explode(const glm::vec3& position, float radius, float damage,
                           int playerTeam) {
    if (!map_ || radius <= 0.0f) return;

    for (const auto& runtimePtr : bots_) {
        Runtime& bot = *runtimePtr;
        if (bot.dead) continue;
        const float distance = bots::distanceToBody(position, bot.ai->position());
        if (distance >= radius) continue;

        // Reuse damageBot so kills/credits are identical to a gunshot.
        damageBot(bot.id, damage, playerTeam);
    }
}

void LocalBotHost::recordPlayerDeath(int playerTeam) {
    if (config_.gameMode == 1) {
        registerKill(1 - (playerTeam & 1));
    }
}

std::vector<LocalBotHost::View> LocalBotHost::views() const {
    std::vector<View> result;
    result.reserve(bots_.size());
    for (const auto& runtime : bots_) {
        View view;
        view.id = runtime->id;
        view.team = runtime->team;
        view.position = runtime->ai->position();
        view.velocity = runtime->ai->velocity();
        view.yaw = runtime->ai->yaw();
        view.pitch = runtime->ai->pitch();
        view.health = runtime->health;
        view.isDead = runtime->dead;
        view.shooting = runtime->shooting;
        result.push_back(view);
    }
    return result;
}
