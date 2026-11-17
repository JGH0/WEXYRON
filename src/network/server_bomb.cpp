/**
 * @file server_bomb.cpp
 * @brief Authoritative bomb state for the dedicated server (see server_bomb.hpp)
 */

#include "server_bomb.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>

namespace {
constexpr float kStateBroadcastInterval = 0.1f; // 10 Hz, plus every change
constexpr uint8_t kNoTeam = 255;

float horizontalDistance(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

bool sameMap(const ServerPlayer& player, const std::string& mapName) {
    return mapName == player.mapName;
}
} // namespace

ServerBombManager::ServerBombManager() = default;
ServerBombManager::~ServerBombManager() = default;

bool ServerBombManager::init(NetworkServer& server, const Config& config) {
    config_ = config;
    server_ = &server;
    config_.plantTime = std::max(0.1f, config_.plantTime);
    config_.defuseTime = std::max(0.1f, config_.defuseTime);
    config_.explodeTime = std::max(1.0f, config_.explodeTime);
    config_.roundTime = std::max(5.0f, config_.roundTime);
    config_.useRange = std::max(0.5f, config_.useRange);

    states_.clear();
    maps_.clear();
    interact_.clear();
    interactAge_.clear();
    std::cout << "[Bomb] Server-side bomb enabled (round " << config_.roundTime
              << "s, plant " << config_.plantTime << "s, defuse "
              << config_.defuseTime << "s, fuse " << config_.explodeTime << "s)"
              << std::endl;
    return true;
}

std::string ServerBombManager::resolveMapPath(const std::string& mapName) const {
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

const bots::BotMap* ServerBombManager::getMap(const std::string& mapName) {
    const std::string key = mapName.empty() ? std::string("simple.map") : mapName;
    auto found = maps_.find(key);
    if (found != maps_.end()) return found->second.get();

    auto map = std::make_unique<bots::BotMap>();
    const std::string path = resolveMapPath(key);
    if (path.empty() || !map->load(path)) {
        std::cerr << "[Bomb] Could not load map '" << key
                  << "' - using a fallback floor" << std::endl;
        *map = bots::BotMap::fallback(key);
    }
    const bots::BotMap* result = map.get();
    maps_[key] = std::move(map);
    return result;
}

ServerBombManager::MapState& ServerBombManager::stateFor(const std::string& mapName) {
    return states_[mapName.empty() ? std::string("simple.map") : mapName];
}

void ServerBombManager::resetBomb(MapState& state) {
    state.planted = false;
    state.defused = false;
    state.exploded = false;
    state.position = glm::vec3(0.0f);
    state.siteIndex = -1;
    state.siteLabel = 'A';
    state.plantingTeam = kNoTeam;
    state.plantingPlayer = 0;
    state.plantedTime = 0.0f;
    state.plantProgress = 0.0f;
    state.defuseProgress = 0.0f;
    state.plantActor = 0;
    state.defuseActor = 0;
    state.dirty = true;
}

void ServerBombManager::resetRound(MapState& state, int gameMode, int attackingTeam) {
    state.gameMode = gameMode;
    state.roundActive = true;
    state.attackingTeam = static_cast<uint8_t>(attackingTeam & 1);
    state.roundTimeLeft = config_.roundTime;
    state.roundEnded = false;
    state.roundWinner = 255;
    state.roundEndReason = ROUND_END_NONE;
    state.broadcastTimer = 0.0f;
    resetBomb(state);
    interact_.clear();
    interactAge_.clear();
}

void ServerBombManager::observePlayers(NetworkServer& server) {
    // A map that became empty starts from scratch when someone joins again
    // (otherwise a finished round would keep roundActive = false forever).
    for (auto& entry : states_) {
        bool anyHuman = false;
        for (ServerPlayer* player : server.getPlayers()) {
            if (player->active && !player->isBot &&
                entry.first == player->mapName) {
                anyHuman = true;
                break;
            }
        }
        MapState& state = entry.second;
        if (!anyHuman && state.hasHumans) {
            state.hasHumans = false;
            state.gameMode = 0;
            state.roundActive = false;
            resetBomb(state);
            state.broadcastTimer = 0.0f;
        }
    }

    // The first client entering a team/bomb map does not send a round-start
    // packet (that only happens after the first round ends). Treat the first
    // report of a round-based mode as the start of the initial round.
    for (ServerPlayer* player : server.getPlayers()) {
        if (!player->active || player->isBot) continue;
        if (player->inputIdle > 2.0f) continue; // still in a menu
        const int mode = static_cast<int>(player->gameMode);

        MapState& state = stateFor(player->mapName);
        state.hasHumans = true;

        // Switching to deathmatch abandons any running round immediately.
        // A round-based client reporting a different round mode for a frame
        // must not restart a live round, though.
        if (state.gameMode != mode && (mode < 1 || !state.roundActive)) {
            if (mode < 1) {
                state.gameMode = 0;
                state.roundActive = false;
                state.roundEnded = false;
                state.roundWinner = 255;
                state.roundEndReason = ROUND_END_NONE;
                state.roundTimeLeft = 0.0f;
                resetBomb(state);
                interact_.clear();
                interactAge_.clear();
            } else {
                resetRound(state, mode, state.attackingTeam);
            }
            state.broadcastTimer = 0.0f;
        }
    }
}

void ServerBombManager::onRoundStart(const std::string& mapName, int gameMode,
                                     int attackingTeam) {
    if (!server_ || gameMode < 1) return;
    MapState& state = stateFor(mapName);
    resetRound(state, gameMode, attackingTeam);
    broadcastState(mapName, state, *server_);
}

void ServerBombManager::onBombInteract(uint32_t playerId, uint8_t action) {
    if (!server_) return;
    ServerPlayer* player = server_->getPlayer(playerId);
    if (!player || !player->active || player->isDead) return;
    // Only humans send requests; bots are handled automatically by chooseActor.
    if (player->isBot) return;
    if (player->gameMode < 1) return;
    if (action != BOMB_INTERACT_PLANT && action != BOMB_INTERACT_DEFUSE) return;

    MapState& state = stateFor(player->mapName);
    if (state.gameMode < 1) resetRound(state, player->gameMode, state.attackingTeam);
    if (!state.roundActive) return;

    interact_[playerId] = action;
    interactAge_[playerId] = 0.0f;
}

void ServerBombManager::onPlayerDisconnected(uint32_t playerId) {
    interact_.erase(playerId);
    interactAge_.erase(playerId);
}

int ServerBombManager::nearbySite(const bots::BotMap* map, const glm::vec3& position) const {
    if (!map) return -1;

    int best = -1;
    float bestDistance = config_.useRange;
    for (size_t i = 0; i < map->bombSites.size(); ++i) {
        const glm::vec3& site = map->bombSites[i].position;
        const float horizontal = horizontalDistance(position, site);
        const float vertical = std::abs(position.y - site.y);
        if (horizontal > config_.useRange || vertical > 2.5f) continue;
        if (horizontal < bestDistance) {
            bestDistance = horizontal;
            best = static_cast<int>(i);
        }
    }
    return best;
}

ServerPlayer* ServerBombManager::chooseActor(const MapState& state, NetworkServer& server,
                                             const std::string& mapName, bool defusing,
                                             int& siteIndex) {
    ServerPlayer* best = nullptr;
    float bestDistance = std::numeric_limits<float>::max();
    siteIndex = -1;

    auto consider = [&](ServerPlayer* candidate, bool requested) {
        if (!candidate || !candidate->active || candidate->isDead) return;
        if (!sameMap(*candidate, mapName)) return;
        if (candidate->gameMode < 1) return;

        if (defusing) {
            if (!state.planted || state.defused || state.exploded) return;
            if (candidate->team == state.plantingTeam) return;
            const float horizontal = horizontalDistance(candidate->position, state.position);
            const float vertical = std::abs(candidate->position.y - state.position.y);
            if (horizontal > config_.useRange || vertical > 2.5f) return;

            // Explicit human requests win over an equally close bot.
            const float score = horizontal + (requested ? -1.0f : 0.0f);
            if (score < bestDistance) {
                bestDistance = score;
                best = candidate;
            }
        } else {
            const bots::BotMap* map = getMap(mapName);
            const int site = nearbySite(map, candidate->position);
            if (site < 0) return;
            if (candidate->team != state.attackingTeam) return;

            const float horizontal = horizontalDistance(
                candidate->position, map->bombSites[static_cast<size_t>(site)].position);
            const float score = horizontal + (requested ? -1.0f : 0.0f);
            if (score < bestDistance) {
                bestDistance = score;
                best = candidate;
                siteIndex = site;
            }
        }
    };

    // Human intents first (nearest requested actor wins over a bot).
    for (const auto& entry : interact_) {
        if ((entry.second == BOMB_INTERACT_PLANT) == defusing) continue;
        ServerPlayer* requested = server.getPlayer(entry.first);
        consider(requested, true);
    }
    if (best) return best;

    // Bots interact automatically once they reach the objective.
    for (ServerPlayer* candidate : server.getPlayers()) {
        if (!candidate->isBot) continue;
        consider(candidate, false);
    }

    return best;
}

void ServerBombManager::finishRound(MapState& state, int winner, RoundEndReason reason) {
    if (!state.roundActive) return;

    state.roundActive = false;
    state.roundEnded = true;
    state.roundWinner = (winner == 0 || winner == 1)
                            ? static_cast<uint8_t>(winner)
                            : static_cast<uint8_t>(255);
    state.roundEndReason = static_cast<uint8_t>(reason);
    state.plantProgress = 0.0f;
    state.plantActor = 0;
    state.defuseProgress = 0.0f;
    state.defuseActor = 0;
    state.dirty = true;

    // The next round swaps sides, just like Game::endRound() on the clients.
    state.attackingTeam = static_cast<uint8_t>((state.attackingTeam + 1) & 1);

    const char* reasonText = "round over";
    switch (reason) {
        case ROUND_END_BOMB_EXPLODED: reasonText = "bomb exploded"; break;
        case ROUND_END_BOMB_DEFUSED: reasonText = "bomb defused"; break;
        case ROUND_END_ATTACKERS_ELIMINATED: reasonText = "attackers eliminated"; break;
        case ROUND_END_DEFENDERS_ELIMINATED: reasonText = "defenders eliminated"; break;
        case ROUND_END_TIME_EXPIRED: reasonText = "time expired"; break;
        default: break;
    }
    if (state.roundWinner == 255) {
        std::cout << "[Round] Draw (" << reasonText << ")" << std::endl;
    } else {
        std::cout << "[Round] Team " << (state.roundWinner == 0 ? "A" : "B")
                  << " wins (" << reasonText << ")" << std::endl;
    }
}

void ServerBombManager::checkRoundOutcome(MapState& state, const std::string& mapName,
                                          NetworkServer& server) {
    if (!state.roundActive || state.roundEnded) return;

    // Count participating players (alive + dead). A team with no participants
    // at all must not instantly "lose by elimination" on a one-team server.
    int present[2] = {0, 0};
    int alive[2] = {0, 0};
    for (ServerPlayer* player : server.getPlayers()) {
        if (!player->active) continue;
        if (!sameMap(*player, mapName)) continue;
        if (!player->isBot && player->inputIdle > 2.0f) continue;
        const int team = player->team & 1;
        ++present[team];
        if (!player->isDead) ++alive[team];
    }

    const int attackers = state.attackingTeam & 1;
    const int defenders = 1 - attackers;

    // All defenders dead: attackers win, with or without the bomb. The bomb
    // itself can still be defused only while a defender is alive.
    if (present[defenders] > 0 && alive[defenders] == 0) {
        finishRound(state, attackers, ROUND_END_DEFENDERS_ELIMINATED);
        return;
    }

    // All attackers dead ends the round only when the bomb is NOT planted:
    // after a plant the defenders still have to defuse it (CS rules).
    if (!state.planted && present[attackers] > 0 && alive[attackers] == 0) {
        finishRound(state, defenders, ROUND_END_ATTACKERS_ELIMINATED);
        return;
    }

    // Plant time expired: defenders win. Once the bomb is planted the fuse
    // replaces the round clock.
    if (!state.planted && state.roundTimeLeft <= 0.0f) {
        finishRound(state, defenders, ROUND_END_TIME_EXPIRED);
    }
}

void ServerBombManager::explode(MapState& state, const std::string& mapName,
                                NetworkServer& server) {
    if (state.exploded) return;

    state.exploded = true;
    state.plantedTime = config_.explodeTime;
    state.defuseProgress = 0.0f;
    state.defuseActor = 0;
    state.dirty = true;

    // The bomb is lethal around its position. This is server-authoritative so
    // bots and human players are affected by exactly the same blast.
    if (onDamage) {
        const float radius = config_.grenadeRadius;
        for (ServerPlayer* victim : server.getPlayers()) {
            if (!victim->active || victim->isDead) continue;
            if (!sameMap(*victim, mapName)) continue;
            const float distance = bots::distanceToBody(state.position, victim->position);
            if (distance >= radius) continue;
            // No distance falloff: any body inside the blast radius takes the
            // same damage. Weapon id 0 = the unclamped (100) damage path in
            // server_main.
            onDamage(0, victim->id, 100, 0);
        }
    }

    std::cout << "[Bomb] Exploded on team " << static_cast<int>(state.plantingTeam)
              << "'s site" << std::endl;
    finishRound(state, state.plantingTeam, ROUND_END_BOMB_EXPLODED);
}

void ServerBombManager::update(float deltaTime, NetworkServer& server) {
    if (!server_) return;
    observePlayers(server);

    // Interaction intents are held only while the client keeps re-sending
    // them. A lost final packet therefore cannot leave a stale request that
    // plants the bomb minutes later.
    for (auto it = interactAge_.begin(); it != interactAge_.end(); ) {
        it->second += deltaTime;
        if (it->second > kIntentTtl) {
            interact_.erase(it->first);
            it = interactAge_.erase(it);
        } else {
            ++it;
        }
    }

    // No human online: bots idle and the round clock should not keep running.
    const bool humansOnline = server.getHumanPlayerCount() > 0;

    for (auto& entry : states_) {
        const std::string& mapName = entry.first;
        MapState& state = entry.second;
        if (state.gameMode < 1) continue;

        state.broadcastTimer -= deltaTime;

        if (humansOnline && state.gameMode >= 1) {
            // Round clock runs only until the bomb is planted; after that the
            // fuse is the only timer that matters.
            if (state.roundActive && !state.planted) {
                state.roundTimeLeft = std::max(0.0f, state.roundTimeLeft - deltaTime);
            }

            // The fuse only runs while the round is still live. Eliminating
            // the last defender after a plant ends the round immediately and
            // the bomb must not detonate afterwards.
            if (state.roundActive && state.planted && !state.defused && !state.exploded) {
                state.plantedTime += deltaTime;
                if (state.plantedTime >= config_.explodeTime) {
                    explode(state, mapName, server);
                }
            }

            if (state.roundActive && !state.exploded) {
                const bots::BotMap* map = getMap(mapName);
                if (!state.planted && !state.defused) {
                    int site = -1;
                    ServerPlayer* actor = chooseActor(state, server, mapName, false, site);
                    if (!actor || site < 0 || !map) {
                        if (state.plantProgress > 0.0f || state.plantActor != 0) {
                            state.plantProgress = 0.0f;
                            state.plantActor = 0;
                            state.dirty = true;
                        }
                    } else {
                        if (state.siteIndex != site) {
                            state.siteIndex = site;
                            state.siteLabel = map->bombSites[static_cast<size_t>(site)].label;
                            state.plantProgress = 0.0f;
                            state.dirty = true;
                        }
                        state.plantActor = actor->id;
                        state.plantProgress += deltaTime;
                        if (state.plantProgress >= config_.plantTime) {
                            state.planted = true;
                            state.defused = false;
                            state.exploded = false;
                            state.position = map->bombSites[static_cast<size_t>(site)].position;
                            state.plantingTeam = actor->team;
                            state.plantingPlayer = actor->id;
                            state.plantedTime = 0.0f;
                            state.plantProgress = 0.0f;
                            state.plantActor = 0;
                            state.defuseProgress = 0.0f;
                            state.defuseActor = 0;
                            state.dirty = true;
                            std::cout << "[Bomb] Planted by "
                                      << (actor->isBot ? "bot " : "player ") << actor->id
                                      << " (team " << static_cast<int>(actor->team)
                                      << ") on " << mapName << std::endl;
                        }
                    }
                } else if (state.planted && !state.defused) {
                    int site = -1;
                    ServerPlayer* actor = chooseActor(state, server, mapName, true, site);
                    if (!actor) {
                        if (state.defuseProgress > 0.0f || state.defuseActor != 0) {
                            state.defuseProgress = 0.0f;
                            state.defuseActor = 0;
                            state.dirty = true;
                        }
                    } else {
                        state.defuseActor = actor->id;
                        state.defuseProgress += deltaTime;
                        if (state.defuseProgress >= config_.defuseTime) {
                            state.defused = true;
                            state.defuseProgress = 0.0f;
                            state.defuseActor = 0;
                            state.dirty = true;
                            std::cout << "[Bomb] Defused by "
                                      << (actor->isBot ? "bot " : "player ") << actor->id
                                      << " (team " << static_cast<int>(actor->team)
                                      << ") on " << mapName << std::endl;
                            finishRound(state, 1 - state.plantingTeam,
                                        ROUND_END_BOMB_DEFUSED);
                        }
                    }
                }
            }

            // Elimination / round-time win conditions (CS-style).
            checkRoundOutcome(state, mapName, server);
        }

        if (state.dirty || state.broadcastTimer <= 0.0f) {
            broadcastState(mapName, state, server);
            state.dirty = false;
            state.broadcastTimer = kStateBroadcastInterval;
        }
    }
}

void ServerBombManager::broadcastState(const std::string& mapName, MapState& state,
                                       NetworkServer& server) {
    BombStatePacket packet{};
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_BOMB_STATE;
    packet.header.sequence = 0;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = 0;

    packet.posX = state.position.x;
    packet.posY = state.position.y;
    packet.posZ = state.position.z;
    packet.timeLeft = state.planted && !state.defused
                          ? std::max(0.0f, config_.explodeTime - state.plantedTime)
                          : 0.0f;
    packet.roundTimeLeft = state.roundTimeLeft;
    packet.plantProgress = state.plantProgress;
    packet.defuseProgress = state.defuseProgress;
    packet.planted = state.planted ? 1 : 0;
    packet.defused = state.defused ? 1 : 0;
    packet.exploded = state.exploded ? 1 : 0;
    packet.roundActive = state.roundActive ? 1 : 0;
    packet.roundEnded = state.roundEnded ? 1 : 0;
    packet.roundWinner = state.roundWinner;
    packet.roundEndReason = state.roundEndReason;
    packet.attackingTeam = state.attackingTeam;
    packet.plantingTeam = state.planted ? state.plantingTeam : kNoTeam;
    packet.siteIndex = state.siteIndex >= 0 && state.siteIndex <= 254
                           ? static_cast<uint8_t>(state.siteIndex)
                           : static_cast<uint8_t>(255);
    packet.siteLabel = static_cast<uint8_t>(state.siteLabel);

    server.broadcastToMap(mapName, &packet, sizeof(packet), 0);
}

ServerBombManager::Snapshot ServerBombManager::snapshot(const std::string& mapName) const {
    Snapshot result;
    auto found = states_.find(mapName.empty() ? std::string("simple.map") : mapName);
    if (found == states_.end()) return result;

    const MapState& state = found->second;
    result.valid = true;
    result.roundActive = state.roundActive;
    result.planted = state.planted;
    result.defused = state.defused;
    result.exploded = state.exploded;
    result.roundEnded = state.roundEnded;
    result.gameMode = state.gameMode;
    result.attackingTeam = state.attackingTeam;
    result.plantingTeam = state.planted ? static_cast<int>(state.plantingTeam) : -1;
    result.roundWinner = state.roundEnded ? static_cast<int>(state.roundWinner) : -1;
    result.roundEndReason = state.roundEndReason;
    result.position = state.position;
    result.siteIndex = state.siteIndex;
    result.siteLabel = state.siteLabel;
    result.timeLeft = state.planted && !state.defused
                          ? std::max(0.0f, config_.explodeTime - state.plantedTime)
                          : 0.0f;
    result.roundTimeLeft = state.roundTimeLeft;
    result.plantProgress = state.plantProgress;
    result.defuseProgress = state.defuseProgress;
    return result;
}

bool ServerBombManager::roundActive(const std::string& mapName) const {
    auto found = states_.find(mapName.empty() ? std::string("simple.map") : mapName);
    return found != states_.end() && found->second.roundActive;
}
