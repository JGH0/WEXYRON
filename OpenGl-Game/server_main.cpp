/**
 * @file server_main.cpp
 * @brief Standalone server executable for LAN multiplayer
 */

#include <iostream>
#include <signal.h>
#include <chrono>
#include "../src/network/network.hpp"

NetworkServer* g_server = nullptr;

void signalHandler(int sig) {
    std::cout << "\nShutting down server..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    uint16_t port = 7777;
    
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "  Cube Game Server" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================" << std::endl;
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    NetworkServer server;
    g_server = &server;
    
    // Set up callbacks
    server.onPlayerConnect = [](uint32_t id) {
        std::cout << "[Server] Player " << id << " connected" << std::endl;
    };
    
    server.onPlayerDisconnect = [](uint32_t id) {
        std::cout << "[Server] Player " << id << " disconnected" << std::endl;
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
