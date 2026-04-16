/**
 * @file network.cpp
 * @brief Network layer implementation for LAN multiplayer
 */

#include "network.hpp"
#include <iostream>
#include <chrono>
#include <functional>

// ========== NETWORK CLIENT ==========

NetworkClient::NetworkClient() : socketFd(SOCKET_INVALID), connected(false), connecting(false), clientId(0), sequence(0) {
    memset(&serverAddr, 0, sizeof(serverAddr));
}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connect(const std::string& serverAddress, uint16_t port) {
#ifdef _WIN32
    static bool wsaInitialized = false;
    if (!wsaInitialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "Failed to initialize Winsock" << std::endl;
            return false;
        }
        wsaInitialized = true;
    }
#endif
    socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd == SOCKET_INVALID) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }
    
    // Set non-blocking
#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(socketFd, FIONBIO, &nonBlocking);
#else
    int flags = fcntl(socketFd, F_GETFL, 0);
    fcntl(socketFd, F_SETFL, flags | O_NONBLOCK);
#endif

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    int ok = inet_pton(AF_INET, serverAddress.c_str(), &serverAddr.sin_addr);
    if (ok != 1) {
        std::cerr << "Invalid server IP address: " << serverAddress << std::endl;
        CLOSE_SOCKET(socketFd);
        socketFd = SOCKET_INVALID;
        return false;
    }
    
    // Send connect packet
    PacketHeader header;
    header.magic = PACKET_MAGIC;
    header.type = PACKET_CONNECT;
    header.sequence = sequence++;
    header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    header.clientId = 0;
    
    if (!sendPacket(&header, sizeof(header))) {
        std::cerr << "Failed to send connect packet" << std::endl;
        CLOSE_SOCKET(socketFd);
        socketFd = SOCKET_INVALID;
        return false;
    }
    
    std::cout << "Connecting to server at " << serverAddress << ":" << port << "..." << std::endl;
    
    // Wait for connect ack (non-blocking, handled in update)
    connecting = true;
    connected = false;
    return true;
}

void NetworkClient::disconnect() {
    if ((connected || connecting) && socketFd >= 0) {
        PacketHeader header;
        header.magic = PACKET_MAGIC;
        header.type = PACKET_DISCONNECT;
        header.sequence = sequence++;
        header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        header.clientId = clientId;
        
        sendPacket(&header, sizeof(header));
    }
    
    if (socketFd != SOCKET_INVALID) {
        CLOSE_SOCKET(socketFd);
        socketFd = SOCKET_INVALID;
    }
    connected = false;
    connecting = false;
    clientId = 0;
}

void NetworkClient::sendPlayerInput(float moveX, float moveZ, float yaw, float pitch,
                                     bool jump, bool shoot, uint8_t weaponId, float posX, float posY, float posZ) {
    if (!connected) return;

    PlayerInputPacket packet;
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_PLAYER_INPUT;
    packet.header.sequence = sequence++;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = clientId;

    packet.moveX = moveX;
    packet.moveZ = moveZ;
    packet.yaw = yaw;
    packet.pitch = pitch;
    packet.jump = jump;
    packet.shoot = shoot;
    packet.weaponId = weaponId;
    packet.posX = posX;
    packet.posY = posY;
    packet.posZ = posZ;

    sendPacket(&packet, sizeof(packet));
}

void NetworkClient::sendMapChange(const std::string& mapName) {
    if (!connected) return;

    MapChangePacket packet;
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_MAP_CHANGE;
    packet.header.sequence = sequence++;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = clientId;

    strncpy(packet.mapName, mapName.c_str(), sizeof(packet.mapName) - 1);
    packet.mapName[sizeof(packet.mapName) - 1] = '\0';

    sendPacket(&packet, sizeof(packet));
    std::cout << "Sent map change request: " << mapName << std::endl;
}

void NetworkClient::update() {
    if ((!connected && !connecting) || socketFd == SOCKET_INVALID) return;
    
    char buffer[MAX_PACKET_SIZE];
    size_t received;
    
    // Process all available packets
    while (receivePacket(buffer, sizeof(buffer), received)) {
        if (received < sizeof(PacketHeader)) continue;
        
        PacketHeader* header = (PacketHeader*)buffer;
        if (header->magic != PACKET_MAGIC) continue;
        
        switch (header->type) {
            case PACKET_CONNECT_ACK:
                clientId = header->clientId;
                connecting = false;
                connected = true;
                std::cout << "Connected to server! Client ID: " << clientId << std::endl;
                break;
                
            case PACKET_PLAYER_STATE:
                if (received >= sizeof(PlayerStatePacket) && onPlayerState) {
                    onPlayerState(*(PlayerStatePacket*)buffer);
                }
                break;
                
            case PACKET_PLAYER_JOIN:
                if (received >= sizeof(PlayerJoinPacket) && onPlayerJoin) {
                    onPlayerJoin(*(PlayerJoinPacket*)buffer);
                }
                break;
                
            case PACKET_PLAYER_LEAVE:
                if (onPlayerLeave) {
                    onPlayerLeave(header->clientId);
                }
                break;
                
            case PACKET_PROJECTILE_SPAWN:
                if (received >= sizeof(ProjectileSpawnPacket) && onProjectileSpawn) {
                    onProjectileSpawn(*(ProjectileSpawnPacket*)buffer);
                }
                break;

            case PACKET_MAP_PLAYER_COUNT:
                if (received >= sizeof(MapPlayerCountPacket) && onMapPlayerCount) {
                    onMapPlayerCount(*(MapPlayerCountPacket*)buffer);
                }
                break;

            case PACKET_PING:
                // Send pong
                PacketHeader pong;
                pong.magic = PACKET_MAGIC;
                pong.type = PACKET_PONG;
                pong.sequence = sequence++;
                pong.timestamp = header->timestamp;
                pong.clientId = clientId;
                sendPacket(&pong, sizeof(pong));
                break;
        }
    }
}

bool NetworkClient::sendPacket(const void* data, size_t size) {
    if (socketFd == SOCKET_INVALID) return false;
#ifdef _WIN32
    int sent = sendto(socketFd, (const char*)data, (int)size, 0,
                      (sockaddr*)&serverAddr, sizeof(serverAddr));
    return sent == (int)size;
#else
    ssize_t sent = sendto(socketFd, data, size, 0,
                          (sockaddr*)&serverAddr, sizeof(serverAddr));
    return sent == (ssize_t)size;
#endif
}

bool NetworkClient::receivePacket(void* buffer, size_t maxSize, size_t& received) {
    if (socketFd == SOCKET_INVALID) return false;

    sockaddr_in from;
    socklen_t fromLen = sizeof(from);
#ifdef _WIN32
    int bytes = recvfrom(socketFd, (char*)buffer, (int)maxSize, 0,
                         (sockaddr*)&from, &fromLen);
#else
    ssize_t bytes = recvfrom(socketFd, buffer, maxSize, 0,
                            (sockaddr*)&from, &fromLen);
#endif
    if (bytes > 0) {
        received = bytes;
        return true;
    }
    return false;
}

// ========== NETWORK SERVER ==========

NetworkServer::NetworkServer() : socketFd(SOCKET_INVALID), running(false), nextClientId(1) {
#ifdef _WIN32
    static bool wsaInitialized = false;
    if (!wsaInitialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "Failed to initialize Winsock" << std::endl;
        }
        wsaInitialized = true;
    }
#endif
    memset(&serverAddr, 0, sizeof(serverAddr));
}

NetworkServer::~NetworkServer() {
    stop();
}

bool NetworkServer::start(uint16_t port) {
    if (!createSocket(port)) {
        return false;
    }
    
    running = true;
    std::cout << "Server started on port " << port << std::endl;
    return true;
}

void NetworkServer::stop() {
    running = false;
    
    // Disconnect all players
    for (auto& pair : players) {
        if (onPlayerDisconnect) {
            onPlayerDisconnect(pair.first);
        }
    }
    players.clear();
    
    if (socketFd != SOCKET_INVALID) {
        CLOSE_SOCKET(socketFd);
        socketFd = SOCKET_INVALID;
    }
}

bool NetworkServer::createSocket(uint16_t port) {
    socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd == SOCKET_INVALID) {
        std::cerr << "Failed to create server socket" << std::endl;
        return false;
    }

    // Allow reuse
    int opt = 1;
    setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(socketFd, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR_CODE) {
        std::cerr << "Failed to bind server socket" << std::endl;
        CLOSE_SOCKET(socketFd);
        socketFd = SOCKET_INVALID;
        return false;
    }

    // Set non-blocking
#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(socketFd, FIONBIO, &nonBlocking);
#else
    int flags = fcntl(socketFd, F_GETFL, 0);
    fcntl(socketFd, F_SETFL, flags | O_NONBLOCK);
#endif

    return true;
}

void NetworkServer::update(float deltaTime) {
    if (!running || socketFd == SOCKET_INVALID) return;

    char buffer[MAX_PACKET_SIZE];
    sockaddr_in from;
    socklen_t fromLen = sizeof(from);

    // Process all available packets
    while (true) {
#ifdef _WIN32
        int bytes = recvfrom(socketFd, (char*)buffer, sizeof(buffer), 0,
                             (sockaddr*)&from, &fromLen);
#else
        ssize_t bytes = recvfrom(socketFd, buffer, sizeof(buffer), 0,
                                (sockaddr*)&from, &fromLen);
#endif
        if (bytes <= 0) break;

        processPacket(buffer, bytes, from);
    }
    
    // Check for timed out players
    auto now = std::chrono::steady_clock::now();
    std::vector<uint32_t> toRemove;
    
    for (auto& pair : players) {
        // Check if player hasn't sent anything in 10 seconds
        // (simplified timeout check)
        // In production, track last packet time per player
    }
}

void NetworkServer::processPacket(const void* data, size_t size, const sockaddr_in& from) {
    if (size < sizeof(PacketHeader)) return;
    
    PacketHeader* header = (PacketHeader*)data;
    if (header->magic != PACKET_MAGIC) return;
    
    switch (header->type) {
        case PACKET_DISCOVER: {
            // Reply with a simple server info packet
            DiscoverResponsePacket resp;
            resp.header.magic = PACKET_MAGIC;
            resp.header.type = PACKET_DISCOVER_RESPONSE;
            resp.header.sequence = 0;
            resp.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            resp.header.clientId = 0;
            resp.port = ntohs(serverAddr.sin_port);
            std::memset(resp.name, 0, sizeof(resp.name));
            std::strncpy(resp.name, "Cube Game Server", sizeof(resp.name) - 1);
            sendPacket(&resp, sizeof(resp), from);
            break;
        }

        case PACKET_CONNECT: {
            // New player connecting
            uint32_t newId = nextClientId++;
            ServerPlayer& player = players[newId];
            player.id = newId;
            player.address = from;
            player.position = glm::vec3(0.0f, 2.0f, 0.0f);
            player.velocity = glm::vec3(0.0f);
            player.yaw = 0.0f;
            player.pitch = 0.0f;
            player.health = 100.0f;
            player.isDead = false;
            player.active = true;
            player.lastInputSequence = 0;
            player.shootCooldown = 0.0f;
            strcpy(player.mapName, "simple.map"); // Default map
            
            // Send connect ack
            PacketHeader ack;
            ack.magic = PACKET_MAGIC;
            ack.type = PACKET_CONNECT_ACK;
            ack.sequence = 0;
            ack.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            ack.clientId = newId;
            sendPacket(&ack, sizeof(ack), from);
            
            // Notify other players
            PlayerJoinPacket join;
            join.header.magic = PACKET_MAGIC;
            join.header.type = PACKET_PLAYER_JOIN;
            join.header.sequence = 0;
            join.header.timestamp = ack.timestamp;
            join.header.clientId = newId;
            join.playerId = newId;
            join.spawnX = player.position.x;
            join.spawnY = player.position.y;
            join.spawnZ = player.position.z;
            broadcastPacket(&join, sizeof(join), newId);
            
            // Notify callback
            if (onPlayerConnect) {
                onPlayerConnect(newId);
            }
            
            std::cout << "Player " << newId << " connected from " 
                     << inet_ntoa(from.sin_addr) << std::endl;
            break;
        }
        
        case PACKET_DISCONNECT: {
            uint32_t id = header->clientId;
            if (players.find(id) != players.end()) {
                removePlayer(id);
            }
            break;
        }
        
        case PACKET_PLAYER_INPUT: {
            if (size < sizeof(PlayerInputPacket)) return;
            
            PlayerInputPacket* input = (PlayerInputPacket*)data;
            uint32_t id = input->header.clientId;
            
            if (players.find(id) != players.end()) {
                ServerPlayer& player = players[id];
                player.lastInputSequence = input->header.sequence;
                
                // Notify game logic
                if (onPlayerInput) {
                    onPlayerInput(id, *input);
                }
            }
            break;
        }
        
        case PACKET_PONG: {
            // Update player ping/latency if needed
            break;
        }

        case PACKET_MAP_CHANGE: {
            if (size < sizeof(MapChangePacket)) return;

            MapChangePacket* mapChange = (MapChangePacket*)data;
            uint32_t id = mapChange->header.clientId;

            if (players.find(id) != players.end()) {
                std::string newMap(mapChange->mapName);
                std::string oldMap(players[id].mapName);

                // Update player's map
                strncpy(players[id].mapName, newMap.c_str(), sizeof(players[id].mapName) - 1);
                players[id].mapName[sizeof(players[id].mapName) - 1] = '\0';

                std::cout << "Player " << id << " changed map from " << oldMap << " to " << newMap << std::endl;

                // Notify other players on the old map that this player left
                PlayerLeavePacket leave;
                leave.header.magic = PACKET_MAGIC;
                leave.header.type = PACKET_PLAYER_LEAVE;
                leave.header.sequence = 0;
                leave.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                leave.header.clientId = id;
                broadcastToMap(oldMap, &leave, sizeof(leave), id);

                // Broadcast updated player counts to all clients
                broadcastMapPlayerCounts();
            }
            break;
        }
    }
}

void NetworkServer::broadcastPlayerState(uint32_t playerId, const glm::vec3& pos,
                                          const glm::vec3& vel, float yaw, float pitch,
                                          float health, bool isDead) {
    PlayerStatePacket packet;
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_PLAYER_STATE;
    packet.header.sequence = 0;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = playerId;

    packet.playerId = playerId;
    packet.posX = pos.x;
    packet.posY = pos.y;
    packet.posZ = pos.z;
    packet.velX = vel.x;
    packet.velY = vel.y;
    packet.velZ = vel.z;
    packet.yaw = yaw;
    packet.pitch = pitch;
    packet.health = health;
    packet.isDead = isDead;

    // Only broadcast to players on the same map
    if (players.find(playerId) != players.end()) {
        broadcastToMap(players[playerId].mapName, &packet, sizeof(packet), playerId);
    } else {
        broadcastPacket(&packet, sizeof(packet), playerId);
    }
}

void NetworkServer::broadcastProjectileSpawn(const glm::vec3& pos, const glm::vec3& vel,
                                              float lifetime, uint8_t type, uint32_t ownerId) {
    ProjectileSpawnPacket packet;
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_PROJECTILE_SPAWN;
    packet.header.sequence = 0;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = ownerId;
    
    packet.posX = pos.x;
    packet.posY = pos.y;
    packet.posZ = pos.z;
    packet.velX = vel.x;
    packet.velY = vel.y;
    packet.velZ = vel.z;
    packet.lifetime = lifetime;
    packet.projectileType = type;
    packet.ownerId = ownerId;
    
    broadcastPacket(&packet, sizeof(packet));
}

std::vector<ServerPlayer*> NetworkServer::getPlayers() {
    std::vector<ServerPlayer*> result;
    for (auto& pair : players) {
        if (pair.second.active) {
            result.push_back(&pair.second);
        }
    }
    return result;
}

ServerPlayer* NetworkServer::getPlayer(uint32_t id) {
    auto it = players.find(id);
    if (it != players.end() && it->second.active) {
        return &it->second;
    }
    return nullptr;
}

void NetworkServer::sendPacket(const void* data, size_t size, const sockaddr_in& to) {
    if (socketFd == SOCKET_INVALID) return;
#ifdef _WIN32
    sendto(socketFd, (const char*)data, (int)size, 0, (sockaddr*)&to, sizeof(to));
#else
    sendto(socketFd, data, size, 0, (sockaddr*)&to, sizeof(to));
#endif
}

void NetworkServer::broadcastPacket(const void* data, size_t size, uint32_t excludeId) {
    for (auto& pair : players) {
        if (pair.first != excludeId && pair.second.active) {
            sendPacket(data, size, pair.second.address);
        }
    }
}

void NetworkServer::broadcastToMap(const std::string& mapName, const void* data, size_t size, uint32_t excludeId) {
    for (auto& pair : players) {
        if (pair.first != excludeId && pair.second.active) {
            if (mapName == pair.second.mapName) {
                sendPacket(data, size, pair.second.address);
            }
        }
    }
}

void NetworkServer::broadcastPlayerStateToMap(uint32_t playerId, const std::string& mapName,
                                               const glm::vec3& pos, const glm::vec3& vel,
                                               float yaw, float pitch, float health, bool isDead) {
    PlayerStatePacket packet;
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_PLAYER_STATE;
    packet.header.sequence = 0;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = playerId;

    packet.playerId = playerId;
    packet.posX = pos.x;
    packet.posY = pos.y;
    packet.posZ = pos.z;
    packet.velX = vel.x;
    packet.velY = vel.y;
    packet.velZ = vel.z;
    packet.yaw = yaw;
    packet.pitch = pitch;
    packet.health = health;
    packet.isDead = isDead;

    broadcastToMap(mapName, &packet, sizeof(packet), playerId);
}

void NetworkServer::broadcastProjectileSpawnToMap(const std::string& mapName, const glm::vec3& pos,
                                                 const glm::vec3& vel, float lifetime,
                                                 uint8_t type, uint32_t ownerId) {
    ProjectileSpawnPacket packet;
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_PROJECTILE_SPAWN;
    packet.header.sequence = 0;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = ownerId;

    packet.posX = pos.x;
    packet.posY = pos.y;
    packet.posZ = pos.z;
    packet.velX = vel.x;
    packet.velY = vel.y;
    packet.velZ = vel.z;
    packet.lifetime = lifetime;
    packet.projectileType = type;
    packet.ownerId = ownerId;

    broadcastToMap(mapName, &packet, sizeof(packet), ownerId);
}

void NetworkServer::broadcastMapPlayerCounts() {
    MapPlayerCountPacket packet;
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_MAP_PLAYER_COUNT;
    packet.header.sequence = 0;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = 0;

    // Count players per map
    std::map<std::string, int> mapCounts;
    for (auto& pair : players) {
        if (pair.second.active) {
            std::string mapName(pair.second.mapName);
            if (mapName.empty()) {
                mapName = "simple.map"; // Default map
            }
            mapCounts[mapName]++;
        }
    }

    // Fill packet
    packet.mapCount = 0;
    for (auto& pair : mapCounts) {
        if (packet.mapCount >= 8) break;
        strncpy(packet.maps[packet.mapCount].mapName, pair.first.c_str(), 31);
        packet.maps[packet.mapCount].mapName[31] = '\0';
        packet.maps[packet.mapCount].playerCount = pair.second;
        packet.mapCount++;
    }

    broadcastPacket(&packet, sizeof(packet));
}

void NetworkServer::setPlayerMap(uint32_t playerId, const std::string& mapName) {
    auto it = players.find(playerId);
    if (it != players.end()) {
        strncpy(it->second.mapName, mapName.c_str(), sizeof(it->second.mapName) - 1);
        it->second.mapName[sizeof(it->second.mapName) - 1] = '\0';
    }
}

int NetworkServer::getPlayerCountOnMap(const std::string& mapName) {
    int count = 0;
    for (auto& pair : players) {
        if (pair.second.active && mapName == pair.second.mapName) {
            count++;
        }
    }
    return count;
}

void NetworkServer::removePlayer(uint32_t id) {
    auto it = players.find(id);
    if (it != players.end()) {
        // Notify other players
        PacketHeader leave;
        leave.magic = PACKET_MAGIC;
        leave.type = PACKET_PLAYER_LEAVE;
        leave.sequence = 0;
        leave.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        leave.clientId = id;
        broadcastPacket(&leave, sizeof(leave), id);
        
        // Notify callback
        if (onPlayerDisconnect) {
            onPlayerDisconnect(id);
        }
        
        players.erase(it);
        std::cout << "Player " << id << " disconnected" << std::endl;
    }
}

ServerPlayer* NetworkServer::findPlayerByAddress(const sockaddr_in& addr) {
    for (auto& pair : players) {
        if (pair.second.address.sin_addr.s_addr == addr.sin_addr.s_addr &&
            pair.second.address.sin_port == addr.sin_port) {
            return &pair.second;
        }
    }
    return nullptr;
}
