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
    PACKET_ROUND_START = 19,       // Round-based client starting the next round
    PACKET_BOMB_INTERACT = 20,     // Client asking the server to plant / defuse
    PACKET_BOMB_STATE = 21,        // Server-authoritative bomb state for one map
    PACKET_LOBBY_SETUP = 22,       // Client -> server team / bot allocation
    PACKET_LOBBY_STATE = 23,       // Server -> clients open match setup / rosters
    PACKET_MATCH_STATE = 24,       // Server -> clients team kill scores
    PACKET_LOBBY_START = 25,       // Server -> lobby players: match is starting
    PACKET_BUY_ACTION = 26,        // Client -> server buy-menu request (validated)
    PACKET_ECONOMY_STATE = 27      // Server -> client authoritative money/loadout
};

/// Client action in BombInteractPacket.
enum BombInteractAction : uint8_t {
    BOMB_INTERACT_PLANT = 1,
    BOMB_INTERACT_DEFUSE = 2
};

/// Why the server ended a round (BombStatePacket.roundEndReason).
enum RoundEndReason : uint8_t {
    ROUND_END_NONE = 0,
    ROUND_END_BOMB_EXPLODED = 1,
    ROUND_END_BOMB_DEFUSED = 2,
    ROUND_END_ATTACKERS_ELIMINATED = 3,
    ROUND_END_DEFENDERS_ELIMINATED = 4,
    ROUND_END_TIME_EXPIRED = 5
};

/// Why a connection attempt was refused (PACKET_CONNECT_REJECT.reason).
enum RejectReason : uint8_t {
    REJECT_SERVER_FULL = 1,        ///< maxPlayers reached
    REJECT_MACHINE_LIMIT = 2       ///< maxPlayersPerMachine reached for this machine
};

/// Size of the machine GUID sent with the handshake (hex, no NUL needed).
const size_t MACHINE_ID_LENGTH = 32;

/// Player / map names in lobby packets (NUL-terminated when shorter).
const size_t LOBBY_NAME_LENGTH = 32;
/// Highest number of human players described by one lobby packet.
const size_t MAX_LOBBY_PLAYERS = 16;

/// Body height for a networked stance byte (0 standing, 1 crouching, 2 prone).
inline float stanceBodyHeight(uint8_t stance) {
    switch (stance) {
        case 1: return 1.2f;
        case 2: return 0.65f;
        default: return 1.8f;
    }
}

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
    /// GameMode (0 = deathmatch, 1 = team deathmatch, 2 = bomb defusal). The
    /// server needs it to stop auto-respawning players during a bomb round.
    uint8_t gameMode;
    /// MatchFormat (0 = free play, 1 = normal, 2 = swift).
    uint8_t matchFormat;
    /// Stance (0 = standing, 1 = crouching, 2 = prone) for the remote model
    /// and the remote hit box.
    uint8_t stance;
    /// The client is on a menu page over a running match. The server keeps the
    /// player in the match (and its body on the map) but must not auto-respawn
    /// it until the client returns to the round.
    uint8_t inMenu;
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
    uint8_t stance;                ///< 0 standing, 1 crouching, 2 prone
    uint8_t weaponId;              ///< held weapon (0 pistol ... 4 flashbang, 5 knife)
};

/// A hit one client scored on another player (client -> server -> victim and
/// back to the shooter as an authoritative PlayerState). The shooter checked
/// the collision against the victim's body, so the server applies it as-is.
struct PlayerDamagePacket {
    PacketHeader header;       ///< clientId = victim, sent by the shooter
    uint16_t damage;
    uint8_t  weaponId;         ///< projectile type that hit (0 bullet, 1 grenade, ...)
};

/// A bomb defusal client started the next round. The server uses this to bring
/// dead bots back at the same time as the human players and to reset the bomb.
struct RoundStartPacket {
    PacketHeader header;
    uint8_t gameMode;          ///< 2 = bomb defusal
    uint8_t attackingTeam;     ///< which team may plant this round
    uint8_t matchFormat;       ///< 0 free play, 1 normal, 2 swift
    uint8_t freshMatch;        ///< 1 = reset the side-switch history
};

/// Pre-match team / bot setup. The client sends its desired side, how many
/// bots each team should get and the requested bot accuracy. The server clamps
/// the counts to its bot pool and the accuracy to its configured maximum.
struct LobbySetupPacket {
    PacketHeader header;
    uint8_t team;              ///< requested team (0/1)
    uint8_t botsTeamA;         ///< bots requested on Team A
    uint8_t botsTeamB;         ///< bots requested on Team B
    uint8_t botAccuracy;       ///< percent (0..100)
    uint8_t matchFormat;       ///< 0 free play, 1 normal, 2 swift
    uint8_t gameMode;          ///< 0 deathmatch, 1 team, 2 bomb
    uint8_t ready;             ///< 1 = this player is ready to start
    uint8_t freshMatch;        ///< 1 = start a fresh scoreboard on this map
    uint8_t inLobby;           ///< 1 = join/update the map's match setup
    char mapName[LOBBY_NAME_LENGTH];    ///< map selected in the setup screen
    char playerName[LOBBY_NAME_LENGTH]; ///< name shown in the lobby roster
};

/// One human player in an open match setup.
struct LobbyPlayerEntry {
    uint32_t id;
    uint8_t team;              ///< 0 = Team A, 1 = Team B
    uint8_t ready;             ///< 1 = pressed READY
    char name[LOBBY_NAME_LENGTH];
};

/// Authoritative state of one map's open match setup, sent to every client so
/// the map selection can list joinable lobbies and the setup screen can show
/// the full roster (including other humans).
struct LobbyStatePacket {
    PacketHeader header;
    char mapName[LOBBY_NAME_LENGTH];
    uint8_t gameMode;
    uint8_t matchFormat;
    uint8_t botPool;           ///< total bots available on the server
    uint8_t botsTeamA;         ///< currently assigned to Team A
    uint8_t botsTeamB;         ///< currently assigned to Team B
    uint8_t botAccuracy;       ///< current configured accuracy (percent)
    uint8_t botMaxAccuracy;    ///< configured maximum accuracy (percent)
    uint8_t playerCount;       ///< humans currently in this lobby
    uint8_t readyCount;        ///< humans that pressed READY
    LobbyPlayerEntry players[MAX_LOBBY_PLAYERS];
};

/// Sent to every member of a lobby when all of them are ready. The server has
/// already applied the bot split; each client uses the entry for its own id to
/// learn its team, then loads the map locally.
struct LobbyStartPacket {
    PacketHeader header;
    char mapName[LOBBY_NAME_LENGTH];
    uint8_t gameMode;
    uint8_t matchFormat;
    uint8_t botsTeamA;
    uint8_t botsTeamB;
    uint8_t botAccuracy;
    uint8_t playerCount;
    struct Entry {
        uint32_t id;
        uint8_t team;
    } players[MAX_LOBBY_PLAYERS];
};

/// Authoritative team scores for team deathmatch (kill counters).
struct MatchStatePacket {
    PacketHeader header;
    uint8_t gameMode;          ///< 1 = team deathmatch
    uint8_t teamAScore;
    uint8_t teamBScore;
};

/// Client asks the server to start planting / defusing. The server validates
/// the team, distance and round state before it begins the progress.
struct BombInteractPacket {
    PacketHeader header;
    uint8_t action;            ///< BombInteractAction
};

/// Client asks the server to buy one item (economy::Item). The server checks
/// money, the buy phase, duplicates and the mode before applying it.
struct BuyActionPacket {
    PacketHeader header;
    uint8_t item;              ///< economy::Item
};

/// Authoritative per-player economy state. Sent after every round/result and
/// after an accepted purchase; the client never invents money online.
struct EconomyStatePacket {
    PacketHeader header;
    uint16_t money;            ///< current balance
    uint8_t armor;             ///< 0..100
    uint8_t defuseKit;         ///< 1 = owned
    uint8_t primary;           ///< 0 = none, 1 = shotgun, 2 = sniper
    uint8_t grenades;          ///< bit 0 = HE, bit 1 = flashbang
    uint8_t buyAllowed;        ///< 1 = buy phase still open
    uint8_t economic;          ///< 1 = money/loadout is authoritative
};

/// Server-authoritative bomb state for a map. Broadcast on changes and a few
/// times per second so joins and lost packets converge on the same state.
struct BombStatePacket {
    PacketHeader header;       ///< clientId == 0 (not tied to one player)
    float posX, posY, posZ;
    float timeLeft;            ///< seconds until the bomb explodes (0 when not planted)
    float plantProgress;       ///< 0 .. plantTime, server-side planting progress
    float defuseProgress;      ///< 0 .. defuseTime, server-side defusing progress
    float roundTimeLeft;       ///< seconds left to plant (frozen after planting)
    float roundAge;            ///< seconds since this round started
    float freezeTime;          ///< round-start freeze duration (server config)
    uint8_t planted;
    uint8_t defused;
    uint8_t exploded;
    uint8_t roundActive;
    uint8_t roundEnded;        ///< round outcome is final; clients show end screen
    uint8_t roundWinner;       ///< 0 = Team A, 1 = Team B, 255 = draw
    uint8_t roundEndReason;    ///< RoundEndReason
    uint8_t attackingTeam;
    uint32_t roundId;          ///< increments on every server round start
    uint8_t plantingTeam;      ///< 255 when no bomb has been planted
    uint8_t siteIndex;         ///< index in the map's bomb-site list (255 = none)
    uint8_t siteLabel;         ///< 'A' or 'B'
    /// Player currently doing the interaction (0 = none). Only this client
    /// may enter the crouched/locked plant or defuse pose; the progress
    /// fields alone describe the whole map, not the local player.
    uint32_t plantActor;
    uint32_t defuseActor;
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
    void sendPlayerInput(float moveX, float moveZ, float yaw, float pitch, bool jump, bool shoot, uint8_t weaponId, float posX, float posY, float posZ, bool alive = true, uint8_t gameMode = 0, uint8_t matchFormat = 0, uint8_t stance = 0, bool inMenu = false);
    
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
    /// Authoritative bomb state for the map the client is on.
    std::function<void(const BombStatePacket&)> onBombState;
    /// Server's bot pool / limits and the current roster for one map lobby.
    std::function<void(const LobbyStatePacket&)> onLobbyState;
    /// The server started the match for the lobby this client is in.
    std::function<void(const LobbyStartPacket&)> onLobbyStart;
    /// Authoritative team kill scores (team deathmatch).
    std::function<void(const MatchStatePacket&)> onMatchState;
    /// Authoritative money / armor / loadout for this client (bomb defusal).
    std::function<void(const EconomyStatePacket&)> onEconomyState;

    // Send map change request to server
    void sendMapChange(const std::string& mapName);

    /// Name published in the lobby roster (usually the Discord username).
    void setPlayerName(const std::string& name) { playerName_ = name; }

    /// Send the pre-match team / bot setup to the server. `inLobby` false
    /// leaves the map's match setup (used when going back or after a start).
    void sendLobbySetup(uint8_t team, uint8_t botsTeamA, uint8_t botsTeamB,
                        uint8_t botAccuracy, uint8_t matchFormat,
                        bool freshMatch = false,
                        const std::string& mapName = std::string(),
                        uint8_t gameMode = 0, bool ready = false,
                        bool inLobby = true);

    /// Tell the server a new bomb defusal round started. Bots are revived
    /// together with the human players and the bomb is reset.
    void sendRoundStart(uint8_t gameMode, uint8_t attackingTeam,
                        uint8_t matchFormat = 0, bool freshMatch = false);

    /// Ask the server to start planting / defusing the bomb (BombInteractAction).
    void sendBombInteract(uint8_t action);

    /// Ask the server to buy one buy-menu item (economy::Item).
    void sendBuyAction(uint8_t item);

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
    uint32_t lastOwnStateSequence_ = 0; ///< ignore stale own-team updates
    uint32_t sequence;

    // Session bookkeeping
    std::string serverAddressText; ///< address of the current target
    uint16_t remotePort = 0;       ///< port of the current target
    std::string lastError;         ///< last failure, empty when none
    double lastHandshakeSend = 0.0;///< time of the last CONNECT transmission
    double handshakeStart = 0.0;   ///< time the current attempt started
    double lastReceive = 0.0;      ///< time of the last packet from the server
    std::string machineId_;        ///< resolved machine id (see getMachineId)
    std::string playerName_ = "playerX"; ///< roster name sent with the lobby
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
    uint8_t matchFormat = 0; ///< last match format the client reported
    uint8_t stance = 0;   ///< 0 standing, 1 crouching, 2 prone
    uint8_t weaponId = 0; ///< held weapon reported by the client / bot
    uint32_t stateSequence = 0; ///< monotonic PlayerState sequence
    bool isBot = false;   ///< server-controlled player (no socket / timeout)
    /// Server-authoritative economy (bomb defusal). Updated by kills, round
    /// results and validated buy requests.
    uint16_t money = 800;
    uint8_t armor = 0;
    bool defuseKit = false;
    uint8_t primary = 0;  ///< 0 = none, 1 = shotgun, 2 = sniper
    bool hasHEGrenade = false;
    bool hasFlashbang = false;
    int lossStreak = 0;
    bool economyInitialised = false; ///< round-1 loadout was applied
    bool buyWindowClosed = false;    ///< moved after the freeze: no more buys
    /// Seconds of invulnerability left after a spawn / respawn.
    float spawnProtection = 0.0f;
    /// The client is browsing a menu over a running match; keep it in the
    /// match and hold a pending respawn until it comes back.
    bool inMenu = false;

    // Pre-match lobby (separate from the map the player is currently in).
    char playerName[LOBBY_NAME_LENGTH] = {};
    char lobbyMapName[LOBBY_NAME_LENGTH] = {};
    bool inLobby = false;
    bool lobbyReady = false;
    uint8_t lobbyGameMode = 0;
    uint8_t lobbyMatchFormat = 0;
    uint8_t lobbyBotsTeamA = 0;
    uint8_t lobbyBotsTeamB = 0;
    uint8_t lobbyBotAccuracy = 0;
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

    /// Send raw packet data to every connected human (not bots).
    void broadcastPacket(const void* data, size_t size, uint32_t excludeId = 0);

    /// Send raw packet data to one human player (used by lobby start packets).
    bool sendToPlayer(uint32_t id, const void* data, size_t size);

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
    /// A client started the next round: map name, game mode, attacking team
    /// and match format.
    std::function<void(const std::string&, int, int, int, bool)> onRoundStarted;
    /// A client asked to plant / defuse (validated sender id, action).
    std::function<void(uint32_t, uint8_t)> onBombInteract;
    /// A client asked to buy an item (validated sender id, packet).
    std::function<void(uint32_t, const BuyActionPacket&)> onBuyAction;
    /// A client sent its pre-match team / bot setup.
    std::function<void(uint32_t, const LobbySetupPacket&)> onLobbySetup;
    
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
