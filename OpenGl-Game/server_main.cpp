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
        // This is where server-side physics would run
        
        float moveSpeed = 5.0f;
        player->velocity.x = input.moveX * moveSpeed;
        player->velocity.z = input.moveZ * moveSpeed;
        player->yaw = input.yaw;
        player->pitch = input.pitch;
        
        // Simple movement
        player->position.x += player->velocity.x * 0.016f;
        player->position.z += player->velocity.z * 0.016f;
        
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
            // Set cooldown (0.2 seconds between shots)
            player->shootCooldown = 0.2f;
            
            // Calculate direction using both yaw and pitch
            float yawRad = glm::radians(input.yaw);
            float pitchRad = glm::radians(input.pitch);
            
            glm::vec3 front(
                cos(pitchRad) * cos(yawRad),
                sin(pitchRad),
                cos(pitchRad) * sin(yawRad)
            );
            
            // Spawn projectile from slightly in front of player
            server.broadcastProjectileSpawn(
                player->position + front * 0.5f,
                front * 50.0f,
                2.0f,
                input.weaponId,
                id
            );
        }
        
        // Broadcast updated state to all clients
        server.broadcastPlayerState(
            id,
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
