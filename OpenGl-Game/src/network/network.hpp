/**
 * @file network.hpp
 * @brief Network layer for LAN multiplayer
 */

#ifndef NETWORK_HPP
#define NETWORK_HPP

#include <glm/glm.hpp>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <map>

// Packet types
enum PacketType : uint8_t {
    PACKET_CONNECT = 1,
    PACKET_CONNECT_ACK = 2,
    PACKET_DISCONNECT = 3,
    PACKET_PLAYER_INPUT = 4,
    PACKET_PLAYER_STATE = 5,
    PACKET_PLAYER_JOIN = 6,
    PACKET_PLAYER_LEAVE = 7,
    PACKET_PROJECTILE_SPAWN = 8,
    PACKET_ENEMY_STATE = 9,
    PACKET_GAME_STATE = 10,
    PACKET_PING = 11,
    PACKET_PONG = 12,
    // LAN discovery (client broadcast -> server response)
    PACKET_DISCOVER = 13,
    PACKET_DISCOVER_RESPONSE = 14
};

#pragma pack(push, 1)

// Header for all packets
struct PacketHeader {
    uint32_t magic;        // 'CUBE' = 0x43554245
    uint8_t type;
    uint32_t sequence;
    uint32_t timestamp;
    uint32_t clientId;
};

// Player input from client
struct PlayerInputPacket {
    PacketHeader header;
    float moveX, moveZ;
    float yaw, pitch;
    bool jump;
    bool shoot;
    uint8_t weaponId;
};

// Player state from server
struct PlayerStatePacket {
    PacketHeader header;
    uint32_t playerId;
    float posX, posY, posZ;
    float velX, velY, velZ;
    float yaw, pitch;
    float health;
    bool isDead;
};

// New player joined
struct PlayerJoinPacket {
    PacketHeader header;
    uint32_t playerId;
    float spawnX, spawnY, spawnZ;
};

// Projectile spawn
struct ProjectileSpawnPacket {
    PacketHeader header;
    float posX, posY, posZ;
    float velX, velY, velZ;
    float lifetime;
    uint8_t projectileType;
    uint32_t ownerId;
};

// LAN discovery request (broadcast)
struct DiscoverPacket {
    PacketHeader header;
};

// LAN discovery response (unicast back to requester)
struct DiscoverResponsePacket {
    PacketHeader header;
    uint16_t port;
    char name[32];
};

#pragma pack(pop)

const uint32_t PACKET_MAGIC = 0x43554245; // 'CUBE'
const uint16_t DEFAULT_PORT = 7777;
const size_t MAX_PACKET_SIZE = 1024;

// ========== NETWORK CLIENT ==========
class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();
    
    bool connect(const std::string& serverAddress, uint16_t port = DEFAULT_PORT);
    void disconnect();
    bool isConnected() const { return connected; }
    bool isConnecting() const { return connecting; }
    
    // Send player input to server
    void sendPlayerInput(float moveX, float moveZ, float yaw, float pitch, bool jump, bool shoot, uint8_t weaponId);
    
    // Receive packets from server - call every frame
    void update();
    
    // Getters for received state
    uint32_t getClientId() const { return clientId; }
    
    // Callbacks (set these to handle incoming data)
    std::function<void(const PlayerStatePacket&)> onPlayerState;
    std::function<void(const PlayerJoinPacket&)> onPlayerJoin;
    std::function<void(uint32_t)> onPlayerLeave;
    std::function<void(const ProjectileSpawnPacket&)> onProjectileSpawn;
    
private:
    int socketFd;
    sockaddr_in serverAddr;
    bool connected;
    bool connecting;
    uint32_t clientId;
    uint32_t sequence;
    
    bool sendPacket(const void* data, size_t size);
    bool receivePacket(void* buffer, size_t maxSize, size_t& received);
};

// ========== NETWORK SERVER ==========
struct ServerPlayer {
    uint32_t id;
    sockaddr_in address;
    glm::vec3 position;
    glm::vec3 velocity;
    float yaw, pitch;
    float health;
    bool isDead;
    bool active;
    uint32_t lastInputSequence;
    float lastPingTime;
    float shootCooldown;  // Shooting cooldown timer
};

class NetworkServer {
public:
    NetworkServer();
    ~NetworkServer();
    
    bool start(uint16_t port = DEFAULT_PORT);
    void stop();
    bool isRunning() const { return running; }
    
    // Process network events - call every frame
    void update(float deltaTime);
    
    // Send game state to all clients
    void broadcastPlayerState(uint32_t playerId, const glm::vec3& pos, const glm::vec3& vel, 
                               float yaw, float pitch, float health, bool isDead);
    void broadcastProjectileSpawn(const glm::vec3& pos, const glm::vec3& vel, float lifetime, 
                                   uint8_t type, uint32_t ownerId);
    
    // Get connected players
    std::vector<ServerPlayer*> getPlayers();
    ServerPlayer* getPlayer(uint32_t id);
    
    // Callbacks
    std::function<void(uint32_t, const PlayerInputPacket&)> onPlayerInput;
    std::function<void(uint32_t)> onPlayerConnect;
    std::function<void(uint32_t)> onPlayerDisconnect;
    
private:
    int socketFd;
    sockaddr_in serverAddr;
    bool running;
    uint32_t nextClientId;
    std::map<uint32_t, ServerPlayer> players;
    
    bool createSocket(uint16_t port);
    void processPacket(const void* data, size_t size, const sockaddr_in& from);
    void sendPacket(const void* data, size_t size, const sockaddr_in& to);
    void broadcastPacket(const void* data, size_t size, uint32_t excludeId = 0);
    void removePlayer(uint32_t id);
    ServerPlayer* findPlayerByAddress(const sockaddr_in& addr);
};

#endif // NETWORK_HPP
