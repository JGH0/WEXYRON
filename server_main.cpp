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
#include <cmath>
#include "../src/network/network.hpp"
#include "../src/network/server_config.hpp"
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
    std::string serverName = settingString(config, "SERVER_NAME", "server", "name", "Cube Game Server");
    int maxPlayers = settingInt(config, "MAX_PLAYERS", "server", "maxPlayers", 16, &srcMax);
    // Players allowed from one machine (same PC/IP + machine id).
    //   -1 = unlimited, 0 = nobody, N = up to N instances per PC.
    int maxPlayersPerMachine = settingInt(config, "MAX_PLAYERS_PER_MACHINE", "server",
                                          "maxPlayersPerMachine", -1, &srcPerMachine);
    std::string region = settingString(config, "", "server", "region", "US-East");
    std::string description = settingString(config, "", "server", "description", "Custom game server");
    float timeout = settingFloat(config, "TIMEOUT", "network", "timeout", 10.0f);

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
    g_server = &server;
    server.setServerName(serverName);
    server.setMaxPlayers(maxPlayers);
    server.setMaxPlayersPerMachine(maxPlayersPerMachine);
    if (timeout > 0.0f) server.setTimeoutSeconds(timeout);
    
    // Set up callbacks
    // The server has no map geometry, so it never invents spawn positions:
    // the client (which knows the map) spawns, and its first input tells the
    // server where that is.
    server.onPlayerConnect = [&server](uint32_t id) {
        if (ServerPlayer* player = server.getPlayer(id)) {
            for (ServerPlayer* other : server.getPlayers()) {
                if (other->id != id) {
                    player->position = other->position;
                    break;
                }
            }
        }
        std::cout << "[Server] Player " << id << " connected (" << server.getCurrentPlayerCount() << "/" << server.getMaxPlayers() << ")" << std::endl;
    };

    server.onPlayerDisconnect = [&server](uint32_t id) {
        std::cout << "[Server] " << (server.isBot(id) ? "Bot " : "Player ")
                  << id << " disconnected (" << server.getCurrentPlayerCount() << "/"
                  << server.getMaxPlayers() << ")" << std::endl;
    };
    
    server.onPlayerInput = [&server](uint32_t id, const PlayerInputPacket& input) {
        // Process player input on server (authoritative)
        ServerPlayer* player = server.getPlayer(id);
        if (!player) return;

        // Remember the mode the client is in: round-based games keep a dead
        // player in spectator mode until that client starts the next round.
        player->gameMode = input.gameMode;

        // A client that reports itself dead/spectating does not move its body.
        // If the server did not know yet (a lost damage packet, a fall, a
        // local death), adopt the client's death state so every other client
        // hides the body immediately. The respawn/round logic then brings it
        // back through the normal path.
        if (!input.alive) {
            if (!player->isDead) {
                player->isDead = true;
                player->health = 0.0f;
                player->velocity = glm::vec3(0.0f);
                server.broadcastPlayerStateToMap(
                    player->id, player->mapName, player->position, player->velocity,
                    player->yaw, player->pitch, player->health, player->isDead, true);
            }
            return;
        }

        // The client came back to life: either the server respawned it, or a
        // round-based client started a new round locally. Make the server
        // match, then let it move again from the next input.
        if (player->isDead) {
            player->isDead = false;
            player->health = 100.0f;
            player->velocity = glm::vec3(0.0f);
            server.broadcastPlayerStateToMap(
                player->id, player->mapName, player->position, player->velocity,
                player->yaw, player->pitch, player->health, player->isDead, true);
        }
        
        // Update player state based on input
        // Use client position from input packet (client-authoritative movement).
        // The client reports its position AT EYE LEVEL (camera height); the
        // server keeps feet positions (spawns and broadcasts use them), or it
        // would spawn projectiles 1.6 units above every victim's head.
        constexpr float kEyeHeight = 1.6f;
        float feetY = input.posY - kEyeHeight;
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

    // ── Damage ───────────────────────────────────────────────────────────────
    // Hits from clients are resolved by the shooter against the victim's body
    // box, so they arrive already validated. Bot damage (bullets resolved by
    // the AI, grenade blasts resolved server-side) goes through the same
    // function, so death/broadcast behaviour is identical for both.
    //
    // Damage is clamped so a bad packet cannot deal more than a hit is worth;
    // grenade / flashbang blasts stay lethal but are not stacked.
    auto applyDamage = [&server](uint32_t shooterId, uint32_t victimId,
                                 float damage, uint8_t weaponId) {
        ServerPlayer* victim = server.getPlayer(victimId);
        if (!victim || victim->isDead) return;

        damage = weaponId >= 1 ? std::min(damage, 75.0f) : std::min(damage, 100.0f);
        if (damage <= 0.0f) return;

        victim->health -= damage;
        victim->velocity = glm::vec3(0.0f);
        if (victim->health < 0.0f) victim->health = 0.0f;
        if (victim->health <= 0.0f) victim->isDead = true;

        // Face the shooter: the victim's camera yaw follows `yaw` on its
        // client, so this is the "you were hit from there" turn.
        if (const ServerPlayer* shooter = server.getPlayer(shooterId)) {
            const float dx = shooter->position.x - victim->position.x;
            const float dz = shooter->position.z - victim->position.z;
            if (dx * dx + dz * dz > 0.0001f) {
                victim->yaw = glm::degrees(std::atan2(dz, dx));
            }
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

    // ── Server bots ──────────────────────────────────────────────────────────
    ServerBotManager botManager;
    botManager.onDamage = [applyDamage](uint32_t shooterId, uint32_t victimId,
                                        uint16_t damage, uint8_t weaponId) {
        applyDamage(shooterId, victimId, static_cast<float>(damage), weaponId);
    };
    server.onProjectileSpawned = [&botManager](uint32_t ownerId,
                                               const ProjectileSpawnPacket& packet) {
        botManager.onProjectileSpawned(ownerId, packet);
    };
    server.onRoundStarted = [&botManager](const std::string& mapName, int gameMode) {
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

    // Main server loop
    auto lastTime = std::chrono::steady_clock::now();
    std::map<uint32_t, std::chrono::steady_clock::time_point> deadSince;
    
    while (server.isRunning()) {
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        
        // Update all player cooldowns
        for (auto* player : server.getPlayers()) {
            if (player->shootCooldown > 0.0f) {
                player->shootCooldown -= deltaTime;
            }
        }
        
        server.update(deltaTime);

        // Bots run their own AI / movement / respawn bookkeeping.
        botManager.update(deltaTime, server);

        // Respawn dead players after a short delay in deathmatch. Round-based
        // games (team vs team / bomb defusal) keep the body dead and invisible
        // until the client starts the next round and reports itself alive.
        // Bots are excluded: the bot manager respawns them on every mode.
        for (ServerPlayer* player : server.getPlayers()) {
            if (player->isBot) continue;
            if (!player->isDead) {
                deadSince.erase(player->id);
                continue;
            }

            const bool roundBased = player->gameMode == 1 || player->gameMode == 2;
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
            deadSince.erase(player->id);

            std::cout << "[Server] Player " << player->id << " respawned" << std::endl;
            server.broadcastPlayerStateToMap(
                player->id, player->mapName, player->position, player->velocity,
                player->yaw, player->pitch, player->health, player->isDead, true);
        }
        
        // Small sleep to prevent 100% CPU usage
        // std::this_thread::sleep_for works on Linux and Windows (usleep /
        // <unistd.h> do not exist under MSVC); the server links Threads.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    return 0;
}
