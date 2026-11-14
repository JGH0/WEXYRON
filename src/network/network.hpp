/**
 * @file network.hpp
 * @brief Network layer for LAN multiplayer
 */

#ifndef NETWORK_HPP
#define NETWORK_HPP

#include <glm/glm.hpp>
#include <string>
#include <cstring>
#include <vector>
#include <map>
#include <functional>

// Platform-specific socket headers
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    typedef int socklen_t;
    typedef SOCKET socket_t;
    #define SOCKET_INVALID INVALID_SOCKET
    #define SOCKET_ERROR_CODE SOCKET_ERROR
    #define CLOSE_SOCKET closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    typedef int socket_t;
    #define SOCKET_INVALID -1
    #define SOCKET_ERROR_CODE -1
    #define CLOSE_SOCKET close
#endif

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
    PACKET_DISCOVER_RESPONSE = 14,
    PACKET_MAP_CHANGE = 15,        // Client requesting map change
    PACKET_MAP_PLAYER_COUNT = 16,  // Server sending player counts per map
    PACKET_CONNECT_REJECT = 17,    // Server refusing a connection (reason follows)
    PACKET_PLAYER_DAMAGE = 18,     // Client reporting a hit on another player
    PACKET_ROUND_START = 19        // Round-based client starting the next round
};

/// Why a connection attempt was refused (PACKET_CONNECT_REJECT.reason).
enum RejectReason : uint8_t {
    REJECT_SERVER_FULL = 1,        ///< maxPlayers reached
    REJECT_MACHINE_LIMIT = 2       ///< maxPlayersPerMachine reached for this machine
};

/// Size of the machine GUID sent with the handshake (hex, no NUL needed).
const size_t MACHINE_ID_LENGTH = 32;

#pragma pack(push, 1)

// Header for all packets
struct PacketHeader {
    uint32_t magic;        // 'CUBE' = 0x43554245
    uint8_t type;
    uint32_t sequence;
    uint32_t timestamp;
    uint32_t clientId;
};

/// First packet of a session. `machineId` is the stable id of the machine the
/// client runs on (all instances on one PC share it), used for the server's
/// per-machine player limit. All zeros is accepted (treated as one per IP).
struct ConnectPacket {
    PacketHeader header;
    char machineId[MACHINE_ID_LENGTH];
};

/// Refusal of a CONNECT: the client shows `reason` instead of waiting.
struct ConnectRejectPacket {
    PacketHeader header;           ///< clientId == 0
    uint8_t reason;                ///< RejectReason
    uint8_t limit;                 ///< the limit that was hit (0 when n/a)
};

/// Acceptance of a CONNECT. The team is assigned by the server (players are
/// spread evenly over the two teams), so both clients spawn on opposite sides.
struct ConnectAckPacket {
    PacketHeader header;           ///< clientId = the new player's id
    uint8_t team;                  ///< 0 = Team A, 1 = Team B
};

// Player input from client
struct PlayerInputPacket {
    PacketHeader header;
    float moveX, moveZ;
    float yaw, pitch;
    bool jump;
    bool shoot;
    uint8_t weaponId;
    float posX, posY, posZ; // Client position for projectile spawning
    /// Client is alive and playing (not dead / spectating). A round-based
    /// client keeps sending input while it spectates, so the server can tell
    /// the difference between "dead" and "new round started".
    bool alive;
    /// GameMode (0 = deathmatch, 1 = team vs team, 2 = bomb defusal). The
    /// server needs it to stop auto-respawning players during a round.
    uint8_t gameMode;
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
    uint8_t team;                  ///< 0 = Team A, 1 = Team B
};

/// A hit one client scored on another player (client -> server -> victim and
/// back to the shooter as an authoritative PlayerState). The shooter checked
/// the collision against the victim's body, so the server applies it as-is.
struct PlayerDamagePacket {
    PacketHeader header;       ///< clientId = victim, sent by the shooter
    uint16_t damage;
    uint8_t  weaponId;         ///< projectile type that hit (0 bullet, 1 grenade, ...)
};

/// A round-based client started the next round. The server uses this to bring
/// dead bots back at the same time as the human players.
struct RoundStartPacket {
    PacketHeader header;
    uint8_t gameMode;          ///< 1 = teams, 2 = bomb defusal
};

// New player joined
struct PlayerJoinPacket {
    PacketHeader header;
    uint32_t playerId;
    float spawnX, spawnY, spawnZ;
    uint8_t team;                  ///< 0 = Team A, 1 = Team B
};

// Player left
struct PlayerLeavePacket {
    PacketHeader header;
    uint32_t playerId;
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

// Map change request (client -> server)
struct MapChangePacket {
    PacketHeader header;
    char mapName[32];
};

// Map player count (server -> client)
struct MapPlayerCountPacket {
    PacketHeader header;
    // Array of map names and player counts
    struct MapInfo {
        char mapName[32];
        uint8_t playerCount;
    };
    MapInfo maps[8];  // Support up to 8 maps
    uint8_t mapCount;
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
    
    /// Start (or retry) the handshake with `serverAddress`.
    ///
    /// The session is only established once the server answers with a
    /// PACKET_CONNECT_ACK, so a single client can only ever have one live
    /// session: calling connect() again while this client is already
    /// connected/connecting just re-sends the CONNECT for the *same*
    /// address (handshake retry). The server recognises the re-send by the
    /// client id carried in the packet and answers the existing player
    /// instead of registering a new one.
    bool connect(const std::string& serverAddress, uint16_t port = DEFAULT_PORT);
    void disconnect();
    bool isConnected() const { return connected; }
    bool isConnecting() const { return connecting; }
    /// Last failure reason (shown on the connect screen), empty when none.
    const std::string& getLastError() const { return lastError; }
    /// Address/port of the current (or last attempted) session.
    const std::string& getServerAddress() const { return serverAddressText; }
    uint16_t getServerPort() const { return remotePort; }
    /// Stable id of this machine, sent with every handshake (all instances on
    /// one PC share it; "" when it could not be determined).
    const std::string& getMachineId() const { return machineId_; }
    /// Override the machine id (tests); pass "" to fall back to detection.
    void setMachineId(const std::string& id) { machineId_ = id; }
    /// Refusal reason when the last handshake was rejected (see RejectReason).
    int getRejectReason() const { return rejectReason_; }
    /// Seconds since the last packet arrived (server-liveness check).
    double secondsSinceLastReceive() const;
    
    // Send player input to server
    void sendPlayerInput(float moveX, float moveZ, float yaw, float pitch, bool jump, bool shoot, uint8_t weaponId, float posX, float posY, float posZ, bool alive = true, uint8_t gameMode = 0);
    
    // Receive packets from server - call every frame
    void update();
    
    // Getters for received state
    uint32_t getClientId() const { return clientId; }
    /// Team the server assigned in the handshake (0 = Team A, 1 = Team B).
    uint8_t getTeam() const { return assignedTeam; }
    
    // Callbacks (set these to handle incoming data)
    std::function<void(const PlayerStatePacket&)> onPlayerState;
    std::function<void(const PlayerJoinPacket&)> onPlayerJoin;
    std::function<void(uint32_t)> onPlayerLeave;
    std::function<void(const ProjectileSpawnPacket&)> onProjectileSpawn;
    std::function<void(const MapPlayerCountPacket&)> onMapPlayerCount;

    // Send map change request to server
    void sendMapChange(const std::string& mapName);

    /// Tell the server a new round started (round-based modes). Bots are
    /// revived together with the human players.
    void sendRoundStart(uint8_t gameMode);

    // Send a keepalive PING (update() does this on its own every second)
    void sendPing();

    /// Relay one projectile the local player just fired. The server forwards
    /// it to the other players on the map, so they see exactly what was fired
    /// (weapon type, pellet count, ammo use and start position/speed).
    void sendProjectileSpawn(const glm::vec3& pos, const glm::vec3& vel,
                             float lifetime, uint8_t type);

    // Report a hit on player `victimId` (resolved against its body below)
    void sendPlayerDamage(uint32_t victimId, uint16_t damage, uint8_t weaponId);

private:
    socket_t socketFd;
    sockaddr_in serverAddr;
    bool connected;
    bool connecting;
    uint32_t clientId;
    uint8_t assignedTeam = 0;
    uint32_t sequence;

    // Session bookkeeping
    std::string serverAddressText; ///< address of the current target
    uint16_t remotePort = 0;       ///< port of the current target
    std::string lastError;         ///< last failure, empty when none
    double lastHandshakeSend = 0.0;///< time of the last CONNECT transmission
    double handshakeStart = 0.0;   ///< time the current attempt started
    double lastReceive = 0.0;      ///< time of the last packet from the server
    std::string machineId_;        ///< resolved machine id (see getMachineId)
    int rejectReason_ = 0;         ///< RejectReason of the last refusal
    /// Retransmission interval for an unanswered CONNECT (seconds). The
    /// handshake must survive a lost UDP packet, but must not flood.
    static constexpr double kHandshakeRetryInterval = 0.5;
    /// Give up on an unanswered handshake after this long (seconds), so the
    /// screen can report "no answer" instead of staying on CONNECTING forever.
    static constexpr double kHandshakeTimeout = 8.0;
    /// Interval between keepalive PINGs while connected (seconds). They prove
    /// to the server that an idle client is still alive, and its PONG proves
    /// the other way round (see secondsSinceLastReceive).
    static constexpr double kKeepaliveInterval = 1.0;
    double lastPingSend = 0.0;     ///< time of the last keepalive PING

    /// Resolve `address` into serverAddr; false when it is not a valid IPv4.
    bool resolveTarget(const std::string& address, uint16_t port);
    /// Send the CONNECT handshake (fresh id 0, or the current id on a retry).
    bool sendConnectPacket();
    /// Return to the "not connected" state and close the socket.
    void closeSocket();

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
    float lastPacketTime = 0.0f; ///< seconds since the last packet (timeout)
    float inputIdle = 999.0f;    ///< seconds since the last PlayerInput (menu)
    float shootCooldown;  // Shooting cooldown timer
    char mapName[32];     // Current map the player is on
    char machineId[MACHINE_ID_LENGTH] = {}; ///< stable id of the player's PC
    uint8_t team = 0;     ///< 0 = Team A, 1 = Team B (assigned on connect)
    uint8_t gameMode = 0; ///< last game mode the client reported
    bool isBot = false;   ///< server-controlled player (no socket / timeout)
};

class NetworkServer {
public:
    NetworkServer();
    ~NetworkServer();
    
    bool start(uint16_t port = DEFAULT_PORT);
    void stop();
    bool isRunning() const { return running; }
    /// Seconds without a packet before a player is dropped as timed out.
    void setTimeoutSeconds(float seconds) { timeoutSeconds = seconds; }
    
    // Process network events - call every frame
    void update(float deltaTime);
    
    // Send game state to all clients
    void broadcastPlayerState(uint32_t playerId, const glm::vec3& pos, const glm::vec3& vel,
                               float yaw, float pitch, float health, bool isDead);
    void broadcastProjectileSpawn(const glm::vec3& pos, const glm::vec3& vel, float lifetime,
                                   uint8_t type, uint32_t ownerId);

    // Map-based broadcasting - only sends to players on the same map
    void broadcastToMap(const std::string& mapName, const void* data, size_t size, uint32_t excludeId);
    void broadcastPlayerStateToMap(uint32_t playerId, const std::string& mapName,
                                    const glm::vec3& pos, const glm::vec3& vel,
                                    float yaw, float pitch, float health, bool isDead,
                                    bool includePlayer = false);
    void broadcastProjectileSpawnToMap(const std::string& mapName, const glm::vec3& pos,
                                        const glm::vec3& vel, float lifetime,
                                        uint8_t type, uint32_t ownerId);

    // Send map player counts to all clients
    void broadcastMapPlayerCounts();

    // Change player's map
    void setPlayerMap(uint32_t playerId, const std::string& mapName);

    /// Move a player (bot or human) to another map at `position` and send the
    /// leave/join packets the clients on both maps need.
    void movePlayerToMap(uint32_t playerId, const std::string& mapName,
                         const glm::vec3& position);

    /// Create a server-controlled player. Bots have no address, never time out
    /// and are skipped by every broadcast. Returns the new id (0 on failure).
    uint32_t addBot(const std::string& mapName, uint8_t team, const glm::vec3& position);
    /// Remove a bot (broadcasts its leave).
    void removeBot(uint32_t id);
    bool isBot(uint32_t id) const;

    // Get connected players
    std::vector<ServerPlayer*> getPlayers();
    ServerPlayer* getPlayer(uint32_t id);

    // Get player count for a specific map
    int getPlayerCountOnMap(const std::string& mapName);
    /// Connected humans (excludes server-controlled bots).
    int getHumanPlayerCount() const;

    // Set server name for discovery
    void setServerName(const std::string& name) { serverName = name; }
    std::string getServerName() const { return serverName; }

    // Set max players
    void setMaxPlayers(int max) { maxPlayers = max; }
    int getMaxPlayers() const { return maxPlayers; }
    /// Max players from one machine (same IP + machine id). -1 = unlimited,
    /// 0 = nobody may join, 1 = one instance per PC, ...
    void setMaxPlayersPerMachine(int max) { maxPlayersPerMachine = max; }
    int getMaxPlayersPerMachine() const { return maxPlayersPerMachine; }
    int getCurrentPlayerCount() const { return players.size(); }
    /// Players currently connected from the machine `machineId` seen from `addr`.
    int getPlayerCountForMachine(const sockaddr_in& addr, const char* machineId) const;

    // Callbacks
    std::function<void(uint32_t, const PlayerInputPacket&)> onPlayerInput;
    /// Client-reported hit (shooter id, packet). Apply it to the victim found
    /// in packet.header.clientId.
    std::function<void(uint32_t, const PlayerDamagePacket&)> onPlayerDamage;
    std::function<void(uint32_t)> onPlayerConnect;
    std::function<void(uint32_t)> onPlayerDisconnect;
    /// A validated projectile spawn from a client (owner id, packet). The bot
    /// host uses this to simulate flashbang detonations server-side.
    std::function<void(uint32_t, const ProjectileSpawnPacket&)> onProjectileSpawned;
    /// A client started the next round: map name and game mode.
    std::function<void(const std::string&, int)> onRoundStarted;
    
private:
    socket_t socketFd;
    sockaddr_in serverAddr;
    bool running;
    uint32_t nextClientId;
    uint32_t nextBotId = 100000; ///< bots never collide with human ids
    std::map<uint32_t, ServerPlayer> players;
    std::string serverName;
    int maxPlayers;
    int maxPlayersPerMachine = -1; ///< -1 = unlimited
    float timeoutSeconds = 10.0f; ///< inactive players are dropped after this
    float lastDeltaTime = 1.0f / 60.0f;
    
    bool createSocket(uint16_t port);
    void processPacket(const void* data, size_t size, const sockaddr_in& from);
    void sendPacket(const void* data, size_t size, const sockaddr_in& to);
    void broadcastPacket(const void* data, size_t size, uint32_t excludeId = 0);
    /// Tell a client why its CONNECT was refused (clientId == 0).
    void sendReject(const sockaddr_in& to, uint8_t reason, uint8_t limit);
    /// True when `addr` (with `machineId`) is on the same PC as `player`.
    /// An empty id degrades to "same IPv4 address".
    static bool sameMachine(const ServerPlayer& player, const sockaddr_in& addr,
                            const char* machineId);
    void removePlayer(uint32_t id);
    /// Drop every player that has not sent a packet for timeoutSeconds.
    void removeTimedOutPlayers();
    ServerPlayer* findPlayerByAddress(const sockaddr_in& addr);
};

#endif // NETWORK_HPP
