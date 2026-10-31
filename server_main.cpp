/**
 * @file server_main.cpp
 * @brief Standalone server executable for LAN multiplayer
 */

#include <iostream>
#include <signal.h>
#include <chrono>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "../src/network/network.hpp"
#include "../src/network/server_config.hpp"

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
    SettingSource srcPort, srcMax, srcPerMachine;

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

    if (maxPlayers < 1) maxPlayers = 1;
    if (maxPlayersPerMachine < -1) maxPlayersPerMachine = -1;

    std::cout << "========================================" << std::endl;
    std::cout << "  " << serverName << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Port: " << port << "   (" << srcPort.origin << ")" << std::endl;
    std::cout << "Max Players: " << maxPlayers << "   (" << srcMax.origin << ")" << std::endl;
    std::cout << "Max Players Per Machine: " << perMachineText(maxPlayersPerMachine)
              << "   (" << srcPerMachine.origin << ")" << std::endl;
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
    server.onPlayerConnect = [&server](uint32_t id) {
        std::cout << "[Server] Player " << id << " connected (" << server.getCurrentPlayerCount() << "/" << server.getMaxPlayers() << ")" << std::endl;
    };

    server.onPlayerDisconnect = [&server](uint32_t id) {
        std::cout << "[Server] Player " << id << " disconnected (" << server.getCurrentPlayerCount() << "/" << server.getMaxPlayers() << ")" << std::endl;
    };
    
    server.onPlayerInput = [&server](uint32_t id, const PlayerInputPacket& input) {
        // Process player input on server (authoritative)
        ServerPlayer* player = server.getPlayer(id);
        if (!player) return;
        
        // Update player state based on input
        // Use client position from input packet (client-authoritative movement)
        player->position.x = input.posX;
        player->position.y = input.posY;
        player->position.z = input.posZ;
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
        player->position.y += player->velocity.y * 0.016f;
        
        // Ground collision
        if (player->position.y < 2.0f) {
            player->position.y = 2.0f;
            player->velocity.y = 0.0f;
        }
        
        // Handle shoot with cooldown and proper pitch/yaw
        if (input.shoot && player->shootCooldown <= 0.0f) {
            // Calculate direction using both yaw and pitch
            float yawRad = glm::radians(input.yaw);
            float pitchRad = glm::radians(input.pitch);
            
            glm::vec3 front(
                cos(pitchRad) * cos(yawRad),
                sin(pitchRad),
                cos(pitchRad) * sin(yawRad)
            );
            
            // Use client position from input packet for projectile spawning
            glm::vec3 clientPos(input.posX, input.posY, input.posZ);
            
            // Weapon-specific projectile spawning
            switch (input.weaponId) {
                case 1: // PUMP_SHOTGUN - 5 pellets with spread
                    player->shootCooldown = 0.8f;
                    for (int i = 0; i < 5; i++) {
                        glm::vec3 spread = glm::vec3(
                            (rand() % 100 - 50) * 0.002f,
                            (rand() % 100 - 50) * 0.002f,
                            0.0f
                        );
                        glm::vec3 dir = glm::normalize(front + spread);
                        server.broadcastProjectileSpawnToMap(
                            player->mapName,
                            clientPos + front * 0.5f,
                            dir * 50.0f,
                            1.0f,
                            input.weaponId,
                            id
                        );
                    }
                    break;
                    
                case 2: // GRENADE - slower, longer lifetime
                    player->shootCooldown = 1.0f;
                    server.broadcastProjectileSpawnToMap(
                        player->mapName,
                        clientPos + front * 0.5f,
                        front * 30.0f,
                        5.0f,
                        input.weaponId,
                        id
                    );
                    break;

                case 3: // FLASHBANG - slower, medium lifetime
                    player->shootCooldown = 1.0f;
                    server.broadcastProjectileSpawnToMap(
                        player->mapName,
                        clientPos + front * 0.5f,
                        front * 25.0f,
                        4.0f,
                        input.weaponId,
                        id
                    );
                    break;
                    
                case 0: // PISTOL (default)
                default:
                    player->shootCooldown = 0.3f;
                    server.broadcastProjectileSpawnToMap(
                        player->mapName,
                        clientPos + front * 0.5f,
                        front * 60.0f,
                        2.0f,
                        input.weaponId,
                        id
                    );
                    break;
            }
        }
        
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
    
    // Main server loop
    auto lastTime = std::chrono::steady_clock::now();
    
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
        
        // Small sleep to prevent 100% CPU usage
        usleep(1000); // 1ms
    }
    
    return 0;
}
