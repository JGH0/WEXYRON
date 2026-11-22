/**
 * @file server_main.cpp
 * @brief Standalone server executable for LAN multiplayer
 */

#include <iostream>
#include <signal.h>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <set>
#include <cstring>
#include <cmath>
#include "../src/network/network.hpp"
#include "../src/network/server_config.hpp"
#include "../src/network/server_bomb.hpp"
#include "../src/network/server_bots.hpp"

NetworkServer* g_server = nullptr;

// Helper function to get environment variable with default
std::string getEnv(const std::string& key, const std::string& defaultValue = "") {
    const char* value = std::getenv(key.c_str());
    return value ? value : defaultValue;
}

// Helper function to get environment variable as int
int getEnvInt(const std::string& key, int defaultValue) {
    const char* value = std::getenv(key.c_str());
    return value ? std::atoi(value) : defaultValue;
}

// Helper function to get environment variable as float
float getEnvFloat(const std::string& key, float defaultValue) {
    const char* value = std::getenv(key.c_str());
    return value ? std::atof(value) : defaultValue;
}

// Helper function to get environment variable as bool
bool getEnvBool(const std::string& key, bool defaultValue) {
    const char* value = std::getenv(key.c_str());
    if (!value) return defaultValue;
    std::string val(value);
    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
    return val == "true" || val == "1" || val == "yes";
}

// setting(): resolve one value with the documented precedence:
//   1. command-line argument (handled by the caller, overrides the result)
//   2. environment variable (docker-compose / .env)
//   3. server_config.json
//   4. built-in default
// `jsonSection`/`jsonKey` are empty when the setting has no JSON entry.
struct SettingSource {
    std::string origin;   // "env", "server_config.json" or "default"
    bool fromEnv = false;
};

std::string settingString(const ServerConfig& cfg, const std::string& envKey,
                          const std::string& section, const std::string& key,
                          const std::string& fallback, SettingSource* src = nullptr) {
    if (!envKey.empty()) {
        const char* value = std::getenv(envKey.c_str());
        if (value) {
            if (src) { src->origin = "env " + envKey; src->fromEnv = true; }
            return value;
        }
    }
    const std::string jsonKey = section.empty() ? std::string() : section + "." + key;
    if (!jsonKey.empty() && cfg.has(jsonKey)) {
        if (src) src->origin = cfg.path();
        return cfg.getString(jsonKey, fallback);
    }
    if (src) src->origin = "default";
    return fallback;
}

int settingInt(const ServerConfig& cfg, const std::string& envKey,
               const std::string& section, const std::string& key,
               int fallback, SettingSource* src = nullptr) {
    if (!envKey.empty()) {
        const char* value = std::getenv(envKey.c_str());
        if (value) {
            if (src) { src->origin = "env " + envKey; src->fromEnv = true; }
            return std::atoi(value);
        }
    }
    const std::string jsonKey = section.empty() ? std::string() : section + "." + key;
    if (!jsonKey.empty() && cfg.has(jsonKey)) {
        if (src) src->origin = cfg.path();
        return cfg.getInt(jsonKey, fallback);
    }
    if (src) src->origin = "default";
    return fallback;
}

float settingFloat(const ServerConfig& cfg, const std::string& envKey,
                   const std::string& section, const std::string& key,
                   float fallback, SettingSource* src = nullptr) {
    if (!envKey.empty()) {
        const char* value = std::getenv(envKey.c_str());
        if (value) {
            if (src) { src->origin = "env " + envKey; src->fromEnv = true; }
            return static_cast<float>(std::atof(value));
        }
    }
    const std::string jsonKey = section.empty() ? std::string() : section + "." + key;
    if (!jsonKey.empty() && cfg.has(jsonKey)) {
        if (src) src->origin = cfg.path();
        return static_cast<float>(cfg.getDouble(jsonKey, fallback));
    }
    if (src) src->origin = "default";
    return fallback;
}

/// "unlimited" for -1, otherwise "N player(s)".
std::string perMachineText(int limit) {
    if (limit < 0) return "unlimited";
    return std::to_string(limit) + " player(s)";
}

void signalHandler(int sig) {
    std::cout << "\nShutting down server..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    // server_config.json next to the server binary (or in the working dir).
    ServerConfig config;
    const char* configPath = std::getenv("SERVER_CONFIG");
    const std::string configFile = configPath ? configPath : "server_config.json";
    const bool configLoaded = config.load(configFile);

    // Settings: CLI args take precedence over environment variables, which
    // take precedence over server_config.json, which takes precedence over
    // the built-in defaults (see SettingSource above).
    SettingSource srcPort, srcMax, srcPerMachine, srcBots, srcBotAccuracy;

    uint16_t port = static_cast<uint16_t>(settingInt(config, "SERVER_PORT", "server", "port", 7777, &srcPort));
    std::string serverName = settingString(config, "SERVER_NAME", "server", "name", "WEXYRON Server");
    int maxPlayers = settingInt(config, "MAX_PLAYERS", "server", "maxPlayers", 16, &srcMax);
    // Players allowed from one machine (same PC/IP + machine id).
    //   -1 = unlimited, 0 = nobody, N = up to N instances per PC.
    int maxPlayersPerMachine = settingInt(config, "MAX_PLAYERS_PER_MACHINE", "server",
                                          "maxPlayersPerMachine", -1, &srcPerMachine);
    std::string region = settingString(config, "", "server", "region", "US-East");
    std::string description = settingString(config, "", "server", "description", "Custom game server");
    float timeout = settingFloat(config, "TIMEOUT", "network", "timeout", 10.0f);
    // Round time to plant the bomb (defenders win when it expires unplanted).
    float roundTime = settingFloat(config, "ROUND_TIME", "gameplay", "roundTime", 115.0f);

    // Bots (server-controlled players).
    int botCount = settingInt(config, "BOTS_COUNT", "bots", "count", 0, &srcBots);
    float botAccuracy = settingFloat(config, "BOTS_ACCURACY", "bots", "accuracy", 0.65f, &srcBotAccuracy);
    std::string defaultMap = settingString(config, "DEFAULT_MAP", "gameplay", "defaultMap", "simple.map");
    const bool friendlyFire = getEnvBool("FRIENDLY_FIRE",
                                          config.getBool("gameplay.friendlyFire", false));

    // Override with command line arguments if provided (highest precedence)
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
        srcPort.origin = "command line";
    }
    if (argc > 2) {
        serverName = argv[2];
    }
    if (argc > 3) {
        maxPlayersPerMachine = std::atoi(argv[3]);
        srcPerMachine.origin = "command line";
    }
    if (argc > 4) {
        botCount = std::atoi(argv[4]);
        srcBots.origin = "command line";
    }
    if (argc > 5) {
        botAccuracy = static_cast<float>(std::atof(argv[5]));
        srcBotAccuracy.origin = "command line";
    }

    if (maxPlayers < 1) maxPlayers = 1;
    if (maxPlayersPerMachine < -1) maxPlayersPerMachine = -1;
    if (botCount < 0) botCount = 0;
    if (botCount > ServerBotManager::kMaxBots) botCount = ServerBotManager::kMaxBots;
    if (botCount > maxPlayers) botCount = maxPlayers;
    if (botAccuracy < 0.0f) botAccuracy = 0.0f;
    if (botAccuracy > 1.0f) botAccuracy = 1.0f;

    std::cout << "========================================" << std::endl;
    std::cout << "  " << serverName << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Port: " << port << "   (" << srcPort.origin << ")" << std::endl;
    std::cout << "Max Players: " << maxPlayers << "   (" << srcMax.origin << ")" << std::endl;
    std::cout << "Max Players Per Machine: " << perMachineText(maxPlayersPerMachine)
              << "   (" << srcPerMachine.origin << ")" << std::endl;
    std::cout << "Bots: " << botCount << "   (" << srcBots.origin << ")" << std::endl;
    std::cout << "Bot Accuracy: " << botAccuracy << "   (" << srcBotAccuracy.origin << ")" << std::endl;
    std::cout << "Bots World: " << defaultMap << std::endl;
    std::cout << "Round Time: " << roundTime << "s" << std::endl;
    std::cout << "Region: " << region << std::endl;
    std::cout << "Description: " << description << std::endl;
    if (configLoaded) {
        std::cout << "Config: " << config.path() << " (" << config.size() << " keys)" << std::endl;
    } else {
        std::cout << "Config: " << configFile
                  << " not found or invalid - using environment/defaults" << std::endl;
    }
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================" << std::endl;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    NetworkServer server;
    ServerBombManager bombManager;   // authoritative bomb state (per map)
    ServerBotManager botManager;     // server-controlled players (pool)
    g_server = &server;

    // A new bomb round revives every human on that map. Their clients learn
    // the new round from the BombState broadcast and reset locally; keeping
    // this state-only avoids a second "alive" packet racing that reset (which
    // looked like being respawned twice).
    auto reviveHumansOnMap = [&server](const std::string& mapName) {
        for (ServerPlayer* player : server.getPlayers()) {
            if (!player->active || player->isBot) continue;
            if (mapName != std::string(player->mapName)) continue;
            player->isDead = false;
            player->health = 100.0f;
            player->velocity = glm::vec3(0.0f);
            player->spawnProtection = 1.0f;
        }
    };
    server.setServerName(serverName);
    server.setMaxPlayers(maxPlayers);
    server.setMaxPlayersPerMachine(maxPlayersPerMachine);
    if (timeout > 0.0f) server.setTimeoutSeconds(timeout);

    // ── Pre-match lobbies ────────────────────────────────────────────────────
    // A lobby is the match setup for one map: the humans in it, their teams,
    // their ready flags and the bot allocation. Every client receives the
    // state of every open lobby, so map selection can list joinable setups.
    auto collectLobbies = [&server]() {
        std::map<std::string, std::vector<ServerPlayer*>> byMap;
        for (ServerPlayer* player : server.getPlayers()) {
            if (player->isBot || !player->inLobby) continue;
            const std::string map = player->lobbyMapName[0] != '\0'
                                        ? std::string(player->lobbyMapName)
                                        : std::string(player->mapName);
            byMap[map].push_back(player);
        }
        return byMap;
    };

    std::set<std::string> knownLobbyMaps;
    auto broadcastLobbyStates = [&server, &botManager, botAccuracy, &knownLobbyMaps,
                                 &collectLobbies]() {
        const auto byMap = collectLobbies();
        std::set<std::string> mapsToSend;
        for (const auto& entry : byMap) mapsToSend.insert(entry.first);
        // Also clear a lobby that just became empty on every client.
        for (const std::string& map : knownLobbyMaps) mapsToSend.insert(map);

        for (const std::string& map : mapsToSend) {
            LobbyStatePacket state{};
            state.header.magic = PACKET_MAGIC;
            state.header.type = PACKET_LOBBY_STATE;
            state.header.sequence = 0;
            state.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            state.header.clientId = 0;
            std::strncpy(state.mapName, map.c_str(), sizeof(state.mapName) - 1);
            state.mapName[sizeof(state.mapName) - 1] = '\0';
            state.botPool = static_cast<uint8_t>(botManager.poolSize());
            state.botsTeamA = static_cast<uint8_t>(botManager.assignedTeamA());
            state.botsTeamB = static_cast<uint8_t>(botManager.assignedTeamB());
            state.botAccuracy = static_cast<uint8_t>(std::lround(botManager.currentAccuracy() * 100.0f));
            state.botMaxAccuracy = static_cast<uint8_t>(std::lround(botAccuracy * 100.0f));

            auto it = byMap.find(map);
            if (it != byMap.end()) {
                std::vector<ServerPlayer*> members = it->second;
                std::sort(members.begin(), members.end(),
                          [](const ServerPlayer* a, const ServerPlayer* b) {
                              if ((a->team & 1u) != (b->team & 1u)) return (a->team & 1u) < (b->team & 1u);
                              return a->id < b->id;
                          });
                state.gameMode = members.front()->lobbyGameMode;
                state.matchFormat = members.front()->lobbyMatchFormat;
                state.playerCount = static_cast<uint8_t>(
                    std::min(members.size(), MAX_LOBBY_PLAYERS));
                for (size_t i = 0; i < members.size() && i < MAX_LOBBY_PLAYERS; ++i) {
                    LobbyPlayerEntry& entry = state.players[i];
                    entry.id = members[i]->id;
                    entry.team = static_cast<uint8_t>(members[i]->team & 1u);
                    entry.ready = members[i]->lobbyReady ? 1 : 0;
                    std::strncpy(entry.name, members[i]->playerName,
                                 sizeof(entry.name) - 1);
                    entry.name[sizeof(entry.name) - 1] = '\0';
                    if (entry.ready) ++state.readyCount;
                }
            }
            server.broadcastPacket(&state, sizeof(state));
        }

        knownLobbyMaps.clear();
        for (const auto& entry : byMap) knownLobbyMaps.insert(entry.first);
    };

    // Once every member of a map lobby pressed READY, tell those clients to
    // start. The packet is repeated until they leave the lobby, so a lost UDP
    // datagram cannot strand a ready lobby forever.
    std::map<std::string, std::chrono::steady_clock::time_point> lastLobbyStart;
    auto startReadyLobbies = [&server, &botManager, &collectLobbies, &lastLobbyStart]() {
        const auto byMap = collectLobbies();
        const auto now = std::chrono::steady_clock::now();

        for (const auto& entry : byMap) {
            const std::string& map = entry.first;
            const std::vector<ServerPlayer*>& members = entry.second;
            if (members.empty()) continue;

            bool allReady = true;
            for (const ServerPlayer* player : members) {
                if (!player->lobbyReady) { allReady = false; break; }
            }
            if (!allReady) {
                lastLobbyStart.erase(map);
                continue;
            }

            auto last = lastLobbyStart.find(map);
            if (last != lastLobbyStart.end() &&
                now - last->second < std::chrono::milliseconds(750)) {
                continue;
            }
            lastLobbyStart[map] = now;

            LobbyStartPacket start{};
            start.header.magic = PACKET_MAGIC;
            start.header.type = PACKET_LOBBY_START;
            start.header.sequence = 0;
            start.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            start.header.clientId = 0;
            std::strncpy(start.mapName, map.c_str(), sizeof(start.mapName) - 1);
            start.mapName[sizeof(start.mapName) - 1] = '\0';
            start.gameMode = members.front()->lobbyGameMode;
            start.matchFormat = members.front()->lobbyMatchFormat;
            start.botsTeamA = static_cast<uint8_t>(botManager.assignedTeamA());
            start.botsTeamB = static_cast<uint8_t>(botManager.assignedTeamB());
            start.botAccuracy = static_cast<uint8_t>(
                std::lround(botManager.currentAccuracy() * 100.0f));
            start.playerCount = static_cast<uint8_t>(
                std::min(members.size(), MAX_LOBBY_PLAYERS));
            for (size_t i = 0; i < members.size() && i < MAX_LOBBY_PLAYERS; ++i) {
                start.players[i].id = members[i]->id;
                start.players[i].team = static_cast<uint8_t>(members[i]->team & 1u);
            }
            for (const ServerPlayer* player : members) {
                server.sendToPlayer(player->id, &start, sizeof(start));
            }
            std::cout << "[Lobby] Starting " << map << " with " << members.size()
                      << " player(s)" << std::endl;
        }

        // Forget throttles for lobbies that closed.
        for (auto it = lastLobbyStart.begin(); it != lastLobbyStart.end();) {
            if (byMap.find(it->first) == byMap.end()) it = lastLobbyStart.erase(it);
            else ++it;
        }
    };

    // Set up callbacks
    // The server has no map geometry, so it never invents spawn positions:
    // the client (which knows the map) spawns, and its first input tells the
    // server where that is.
    server.onPlayerConnect = [&server, &broadcastLobbyStates](uint32_t id) {
        if (ServerPlayer* player = server.getPlayer(id)) {
            player->spawnProtection = 1.0f;
            for (ServerPlayer* other : server.getPlayers()) {
                if (other->id != id) {
                    player->position = other->position;
                    break;
                }
            }
        }
        broadcastLobbyStates();
        std::cout << "[Server] Player " << id << " connected (" << server.getCurrentPlayerCount() << "/" << server.getMaxPlayers() << ")" << std::endl;
    };

    server.onPlayerDisconnect = [&server, &bombManager, &broadcastLobbyStates](uint32_t id) {
        bombManager.onPlayerDisconnected(id);
        // The callback runs before the player record is erased; hide it from
        // the lobby roster we are about to broadcast.
        if (ServerPlayer* player = server.getPlayer(id)) player->inLobby = false;
        broadcastLobbyStates();
        std::cout << "[Server] " << (server.isBot(id) ? "Bot " : "Player ")
                  << id << " disconnected (" << server.getCurrentPlayerCount() << "/"
                  << server.getMaxPlayers() << ")" << std::endl;
    };
    
    server.onPlayerInput = [&server, &bombManager](uint32_t id, const PlayerInputPacket& input) {
        // Process player input on server (authoritative)
        ServerPlayer* player = server.getPlayer(id);
        if (!player) return;

        // Remember the mode the client is in: bomb defusal keeps a dead
        // player in spectator mode until that client starts the next round.
        player->gameMode = input.gameMode;
        player->matchFormat = input.matchFormat;
        player->stance = input.stance <= 2 ? input.stance : 0;
        player->weaponId = input.weaponId % 5;

        // The client is on a menu page over a running match. Keep its player
        // record (and its body on the map) exactly as it is: no movement, no
        // death adoption, and the respawn loop below holds any respawn until
        // the client is back in the round.
        const bool returnedFromMenu = player->inMenu && input.inMenu == 0;
        player->inMenu = input.inMenu != 0;
        if (player->inMenu) return;
        // The client may have died while browsing (and respawned locally just
        // now, possibly after the server already brought it back). A short
        // protection on the way out prevents an instant death on return.
        if (returnedFromMenu) {
            player->spawnProtection = std::max(player->spawnProtection, 1.0f);
        }

        // A client that reports itself dead/spectating does not move its body.
        // If the server did not know yet (a lost damage packet, a fall, a
        // local death), adopt the client's death state so every other client
        // hides the body immediately. The respawn/round logic then brings it
        // back through the normal path.
        if (!input.alive) {
            if (!player->isDead) {
                // Right after a respawn the client may still be sending the
                // death from its previous life (the respawn state is on its
                // way). Adopting that made the player die again in a loop.
                // Keep it alive and resend the respawned state so the client
                // can catch up.
                if (player->spawnProtection > 0.0f) {
                    // Bomb clients reset from the new-round BombState, so
                    // they must not also receive an alive state (that raced
                    // the reset and respawned them twice).
                    if (player->gameMode != 2) {
                        server.broadcastPlayerStateToMap(
                            player->id, player->mapName, player->position, player->velocity,
                            player->yaw, player->pitch, player->health, player->isDead, true);
                    }
                    return;
                }
                player->isDead = true;
                player->health = 0.0f;
                player->velocity = glm::vec3(0.0f);
                server.broadcastPlayerStateToMap(
                    player->id, player->mapName, player->position, player->velocity,
                    player->yaw, player->pitch, player->health, player->isDead, true);
            }
            return;
        }

        // The client came back to life. In a round-based mode a dead player
        // only comes back when the next round starts: a stale alive input in
        // the middle of a round (or after its result) must not respawn them.
        // The server revives the players itself when it starts the next round,
        // and the repeated BombState broadcast carries that reset to clients.
        if (player->isDead && player->gameMode == 2) {
            const ServerBombManager::Snapshot bomb = bombManager.snapshot(player->mapName);
            if (bomb.valid && (bomb.roundEnded || bomb.roundActive)) {
                player->velocity = glm::vec3(0.0f);
                // Self-heal a client that still thinks it is alive: send the
                // authoritative death so it enters spectator instead of
                // walking around with a body everyone else sees frozen.
                server.broadcastPlayerStateToMap(
                    player->id, player->mapName, player->position, player->velocity,
                    player->yaw, player->pitch, 0.0f, true, true);
                return;
            }
        }

        // Make the server match, then let it move again from the next input.
        // A fresh respawn grants one second of spawn protection.
        if (player->isDead) {
            player->isDead = false;
            player->health = 100.0f;
            player->velocity = glm::vec3(0.0f);
            player->spawnProtection = 1.0f;
            server.broadcastPlayerStateToMap(
                player->id, player->mapName, player->position, player->velocity,
                player->yaw, player->pitch, player->health, player->isDead, true);
        }
        
        // Update player state based on input
        // Use client position from input packet (client-authoritative movement).
        // The client reports its position AT EYE LEVEL (camera height); the
        // server keeps feet positions (spawns and broadcasts use them), or it
        // would spawn projectiles above every victim's head. The eye offset
        // follows the stance, so a crouching / prone player's feet stay on the
        // floor and the remote hit box starts at the right height.
        const float eyeHeight = input.stance == 1 ? 1.05f
                              : input.stance == 2 ? 0.35f
                                                  : 1.6f;
        float feetY = input.posY - eyeHeight;
        // Stay inside the playable volume (a stray packet must not send a
        // player under the map, where it can never be hit again).
        feetY = std::min(18.0f, std::max(-10.0f, feetY));
        player->position.x = std::min(48.0f, std::max(-48.0f, input.posX));
        player->position.y = feetY;
        player->position.z = std::min(48.0f, std::max(-48.0f, input.posZ));
        player->yaw = input.yaw;
        player->pitch = input.pitch;
        
        // Keep velocity for potential server-side physics
        float moveSpeed = 5.0f;
        player->velocity.x = input.moveX * moveSpeed;
        player->velocity.z = input.moveZ * moveSpeed;
        
        // Handle jump
        if (input.jump && player->position.y <= 2.0f) {
            player->velocity.y = 4.0f;
        }
        
        // Apply gravity
        player->velocity.y -= 9.8f * 0.016f;
        
        // Ground collision (feet on the floor)
        if (player->position.y < 0.0f) {
            player->position.y = 0.0f;
            player->velocity.y = 0.0f;
        }
        
        // Projectiles are not invented here. The client that fired reports the
        // exact ProjectileSpawnPacket (type, speed, pellet count), and the
        // server relays it; deriving shots from input.weaponId mixed up weapon
        // ids and projectile types (a shotgun pellet showed up as a grenade).
        
        // Broadcast updated state only to clients on the same map
        server.broadcastPlayerStateToMap(
            id,
            player->mapName,
            player->position,
            player->velocity,
            player->yaw,
            player->pitch,
            player->health,
            player->isDead
        );
    };
    
    if (!server.start(port)) {
        std::cerr << "Failed to start server!" << std::endl;
        return 1;
    }

    // Team deathmatch scoreboard (kill counters), per map. The server is the
    // only place where all players' and bots' kills meet, so clients use this
    // broadcast instead of their local view of the score.
    std::map<std::string, std::pair<int, int>> teamKills;
    auto broadcastMatchState = [&server](const std::string& mapName, int a, int b) {
        MatchStatePacket state{};
        state.header.magic = PACKET_MAGIC;
        state.header.type = PACKET_MATCH_STATE;
        state.header.sequence = 0;
        state.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        state.header.clientId = 0;
        state.gameMode = 1;
        state.teamAScore = static_cast<uint8_t>(std::max(0, std::min(255, a)));
        state.teamBScore = static_cast<uint8_t>(std::max(0, std::min(255, b)));
        server.broadcastToMap(mapName, &state, sizeof(state), 0);
    };

    // ── Damage ───────────────────────────────────────────────────────────────
    // Hits from clients are resolved by the shooter against the victim's body
    // box, so they arrive already validated. Bot damage (bullets resolved by
    // the AI, grenade blasts resolved server-side) goes through the same
    // function, so death/broadcast behaviour is identical for both.
    //
    // Damage is clamped so a bad packet cannot deal more than a hit is worth;
    // grenade / flashbang blasts stay lethal but are not stacked.
    auto applyDamage = [&server, &teamKills, &broadcastMatchState](
                                 uint32_t shooterId, uint32_t victimId,
                                 float damage, uint8_t weaponId) {
        ServerPlayer* victim = server.getPlayer(victimId);
        if (!victim || victim->isDead) return;
        // Spawn protection: ignore every hit during the first second of a life.
        if (victim->spawnProtection > 0.0f) return;

        // A sniper body shot (100) is lethal; its headshot (200) is capped at
        // 160. Grenade blasts stay lethal but are not stacked.
        if (weaponId == 4) {
            damage = std::min(damage, 160.0f);
        } else if (weaponId >= 1) {
            damage = std::min(damage, 75.0f);
        } else {
            damage = std::min(damage, 100.0f);
        }
        if (damage <= 0.0f) return;

        victim->health -= damage;
        victim->velocity = glm::vec3(0.0f);
        if (victim->health < 0.0f) victim->health = 0.0f;
        if (victim->health <= 0.0f) victim->isDead = true;

        ServerPlayer* shooter = server.getPlayer(shooterId);

        // Face the shooter: the victim's camera yaw follows `yaw` on its
        // client, so this is the "you were hit from there" turn.
        if (shooter) {
            const float dx = shooter->position.x - victim->position.x;
            const float dz = shooter->position.z - victim->position.z;
            if (dx * dx + dz * dz > 0.0001f) {
                victim->yaw = glm::degrees(std::atan2(dz, dx));
            }
        }

        // Team deathmatch kill score. Only enemy teams score; friendly fire
        // and self/blast deaths (shooterId 0) do not change the board.
        if (victim->isDead && victim->gameMode == 1 && shooter &&
            shooter->id != victim->id && shooter->team != victim->team) {
            auto& score = teamKills[std::string(victim->mapName)];
            if (shooter->team == 0) ++score.first;
            else                    ++score.second;
            broadcastMatchState(victim->mapName, score.first, score.second);
        }

        std::cout << "[Server] " << (victim->isBot ? "Bot " : "Player ")
                  << victim->id << " hit by " << (server.isBot(shooterId) ? "bot " : "player ")
                  << shooterId << " for " << damage
                  << " (health " << victim->health << ")"
                  << (victim->isDead ? " - DEAD" : "") << std::endl;

        server.broadcastPlayerStateToMap(
            victim->id, victim->mapName, victim->position, victim->velocity,
            victim->yaw, victim->pitch, victim->health, victim->isDead, true);
    };

    server.onPlayerDamage = [applyDamage](uint32_t shooterId, const PlayerDamagePacket& hit) {
        applyDamage(shooterId, hit.header.clientId, static_cast<float>(hit.damage), hit.weaponId);
    };

    // ── Server-authoritative bomb ────────────────────────────────────────────
    // The client only asks to plant/defuse; the server validates and advances
    // the bomb, then broadcasts its state to every player on the map. This
    // also makes the bomb visible to other clients and usable by bots.
    ServerBombManager::Config bombConfig;
    bombConfig.mapDirectory = "assets/maps";
    bombConfig.roundTime = roundTime;
    bombManager.init(server, bombConfig);
    bombManager.onDamage = [applyDamage](uint32_t shooterId, uint32_t victimId,
                                         uint16_t damage, uint8_t weaponId) {
        applyDamage(shooterId, victimId, static_cast<float>(damage), weaponId);
    };
    server.onBombInteract = [&bombManager](uint32_t playerId, uint8_t action) {
        bombManager.onBombInteract(playerId, action);
    };

    // ── Server bots ──────────────────────────────────────────────────────────
    botManager.setBombInfoProvider([&bombManager](const std::string& mapName) {
        const ServerBombManager::Snapshot bomb = bombManager.snapshot(mapName);
        ServerBotManager::BombInfo info;
        info.valid = bomb.valid;
        info.roundActive = bomb.roundActive;
        info.planted = bomb.planted;
        info.defused = bomb.defused;
        info.exploded = bomb.exploded;
        info.position = bomb.position;
        info.plantingTeam = bomb.plantingTeam;
        info.attackingTeam = bomb.attackingTeam;
        return info;
    });
    botManager.onDamage = [applyDamage](uint32_t shooterId, uint32_t victimId,
                                        uint16_t damage, uint8_t weaponId) {
        applyDamage(shooterId, victimId, static_cast<float>(damage), weaponId);
    };
    server.onProjectileSpawned = [&botManager](uint32_t ownerId,
                                               const ProjectileSpawnPacket& packet) {
        botManager.onProjectileSpawned(ownerId, packet);
    };
    server.onRoundStarted = [&botManager, &bombManager,
                             &reviveHumansOnMap](const std::string& mapName,
                                                 int gameMode, int attackingTeam,
                                                 int matchFormat,
                                                 bool freshMatch) {
        bombManager.onRoundStart(mapName, gameMode, attackingTeam, matchFormat,
                                 freshMatch);
        reviveHumansOnMap(mapName);
        botManager.startRound(mapName, gameMode);
    };

    if (botCount > 0) {
        ServerBotManager::Config botConfig;
        botConfig.count = botCount;
        botConfig.accuracy = botAccuracy;
        botConfig.defaultMap = defaultMap;
        botConfig.friendlyFire = friendlyFire;
        botManager.init(server, botConfig);
    }

    // Server-owned round transitions: revive the humans on that map and let
    // the bots start the new round too. Clients follow the BombState packet.
    bombManager.onRoundRestart = [&botManager, &reviveHumansOnMap](
                                     const std::string& mapName, int gameMode) {
        reviveHumansOnMap(mapName);
        botManager.startRound(mapName, gameMode);
    };

    // Pre-match setup: apply team / bot allocation and tell everyone the new
    // limits. Accuracy is clamped to the server's configured maximum.
    server.onLobbySetup = [&server, &botManager, &broadcastLobbyStates,
                           &startReadyLobbies, &teamKills, &broadcastMatchState,
                           botAccuracy](uint32_t playerId,
                                        const LobbySetupPacket& setup) {
        // A player merely leaving a lobby must not overwrite the bot split of
        // the setup they were looking at. Fresh-match packets still carry the
        // authoritative values used when the map starts.
        if (setup.inLobby != 0 || setup.freshMatch != 0) {
            botManager.applyTeamSetup(server, setup.botsTeamA, setup.botsTeamB,
                                      std::min(setup.botAccuracy / 100.0f, botAccuracy));
        }
        // Echo the new team back to the client (and the other players on the
        // map): PlayerState is where the client learns its authoritative team.
        if (ServerPlayer* player = server.getPlayer(playerId)) {
            if (setup.freshMatch) {
                teamKills[std::string(player->mapName)] = {0, 0};
                broadcastMatchState(player->mapName, 0, 0);
            }
            server.broadcastPlayerStateToMap(
                playerId, player->mapName, player->position, player->velocity,
                player->yaw, player->pitch, player->health, player->isDead, true);
        }
        broadcastLobbyStates();
        startReadyLobbies();
    };

    // Main server loop
    auto lastTime = std::chrono::steady_clock::now();
    auto lastLobbyBroadcast = lastTime;
    std::map<uint32_t, std::chrono::steady_clock::time_point> deadSince;
    
    while (server.isRunning()) {
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        
        // Update all player cooldowns and spawn protection
        for (auto* player : server.getPlayers()) {
            if (player->shootCooldown > 0.0f) {
                player->shootCooldown -= deltaTime;
            }
            if (player->spawnProtection > 0.0f) {
                player->spawnProtection -= deltaTime;
                if (player->spawnProtection < 0.0f) player->spawnProtection = 0.0f;
            }
        }
        
        server.update(deltaTime);

        // Lobby rosters are refreshed twice a second so clients that open map
        // selection always see the current setups (and a lost UDP update is
        // repaired). The same tick starts any lobby whose members are all ready.
        if (std::chrono::duration<float>(now - lastLobbyBroadcast).count() >= 0.5f) {
            lastLobbyBroadcast = now;
            startReadyLobbies();
            broadcastLobbyStates();
        }

        // The bomb is authoritative: advance plant/defuse timers, explode /
        // defuse, and broadcast the new state before the bots make decisions
        // from it. Bots auto-interact once they reach a site / the bomb.
        bombManager.update(deltaTime, server);

        // Bots run their own AI / movement / respawn bookkeeping. In round
        // based modes dead bots stay dead until the next explicit round start.
        botManager.update(deltaTime, server);

        // Respawn dead players after a short delay in deathmatch and team
        // deathmatch. Bomb defusal keeps the body dead and invisible until the
        // client starts the next round and reports itself alive.
        // Bots are excluded: the bot manager owns their respawns.
        for (ServerPlayer* player : server.getPlayers()) {
            if (player->isBot) continue;
            if (!player->isDead) {
                deadSince.erase(player->id);
                continue;
            }

            // The player is browsing a menu over the match: keep the body in
            // the game but do not respawn until the client is back in the
            // round (it reports inMenu=0 again).
            if (player->inMenu) continue;

            const bool roundBased = player->gameMode == 2;
            if (roundBased) continue;

            const auto it = deadSince.find(player->id);
            if (it == deadSince.end()) {
                deadSince[player->id] = now;
                continue;   // let the death state reach the victim first
            }
            if (std::chrono::duration<float>(now - it->second).count() < 2.0f) continue;

            // The client spawns on its map (clear of geometry) and moves from
            // there; the server keeps the death state and lets the client pick
            // the next spawn point.
            player->velocity = glm::vec3(0.0f);
            player->health = 100.0f;
            player->isDead = false;
            player->spawnProtection = 1.0f;
            deadSince.erase(player->id);

            std::cout << "[Server] Player " << player->id << " respawned" << std::endl;
            server.broadcastPlayerStateToMap(
                player->id, player->mapName, player->position, player->velocity,
                player->yaw, player->pitch, player->health, player->isDead, true);
        }
        
        // A map with no humans is a finished/abandoned match: next time
        // players arrive there, the team scoreboard starts from zero.
        for (auto it = teamKills.begin(); it != teamKills.end();) {
            bool anyHuman = false;
            for (ServerPlayer* player : server.getPlayers()) {
                if (player->active && !player->isBot && it->first == player->mapName) {
                    anyHuman = true;
                    break;
                }
            }
            if (!anyHuman) it = teamKills.erase(it);
            else ++it;
        }

        // Small sleep to prevent 100% CPU usage
        // std::this_thread::sleep_for works on Linux and Windows (usleep /
        // <unistd.h> do not exist under MSVC); the server links Threads.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    return 0;
}
