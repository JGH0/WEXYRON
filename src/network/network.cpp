/**
 * @file network.cpp
 * @brief Network layer implementation for LAN multiplayer
 */

#include "network.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <uuid/uuid.h>
  #include <unistd.h>
#endif

namespace {
/// Seconds on a monotonic clock (handshake retransmission bookkeeping).
double nowSeconds() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

// ========== MACHINE ID ==========
//
// All instances on one PC must present the same id, so it is derived from the
// OS machine id rather than generated per process:
//   Linux   /etc/machine-id, then /var/lib/dbus/machine-id
//   Windows MachineGuid (crypto\\machine, registry)
//   macOS   gethostuuid()
// The raw id is hashed (so the registry/file value is not exposed) and cached
// in assets/machine_id next to the client. Without an OS id (containers,
// minimal images) a random id is generated once; it is stable per install but
// cannot tell two PCs apart, so the server then groups players per IP.

std::string machineIdFromOS() {
#if defined(_WIN32)
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        char buffer[256] = {};
        DWORD size = sizeof(buffer);
        DWORD type = 0;
        const LONG rc = RegQueryValueExA(key, "MachineGuid", nullptr, &type,
                                         reinterpret_cast<LPBYTE>(buffer), &size);
        RegCloseKey(key);
        if (rc == ERROR_SUCCESS && type == REG_SZ) return std::string(buffer);
    }
#elif defined(__APPLE__)
    unsigned char uuid[16];
    uuid_t raw;
    if (gethostuuid(raw, nullptr) == 0) {
        std::memcpy(uuid, raw, sizeof(uuid));
        std::string text;
        for (unsigned char byte : uuid) {
            const char hex[] = "0123456789abcdef";
            text += hex[byte >> 4];
            text += hex[byte & 0x0f];
        }
        return text;
    }
#else
    for (const char* path : {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
        std::ifstream in(path);
        std::string line;
        if (in && std::getline(in, line) && !line.empty()) return line;
    }
#endif
    return std::string();
}

/// Hex SHA-256 of `text` (32 chars), so only a derived id leaves the machine.
std::string hashId(const std::string& text) {
    // Self-contained SHA-256: no OpenSSL dependency on any platform.
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    std::vector<uint8_t> message(text.begin(), text.end());
    const uint64_t bitLength = static_cast<uint64_t>(message.size()) * 8;
    message.push_back(0x80);
    while (message.size() % 64 != 56) message.push_back(0);
    for (int i = 7; i >= 0; --i) message.push_back(static_cast<uint8_t>(bitLength >> (i * 8)));

    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };

    for (size_t chunk = 0; chunk < message.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const uint8_t* p = &message[chunk + i * 4];
            w[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                   (uint32_t(p[2]) << 8) | uint32_t(p[3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    static const char hex[] = "0123456789abcdef";
    std::string out;
    for (uint32_t word : h) {
        for (int shift = 28; shift >= 0; shift -= 4) out += hex[(word >> shift) & 0x0f];
    }
    return out;
}

std::string readMachineIdFile(const std::string& path) {
    std::ifstream in(path);
    std::string id;
    if (!(in >> id)) return std::string();
    // 32 hex chars expected; anything else means a corrupt cache.
    if (MACHINE_ID_LENGTH != id.size()) return std::string();
    for (char c : id) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return std::string();
    }
    return id;
}

void writeMachineIdFile(const std::string& path, const std::string& id) {
    std::ofstream out(path, std::ios::trunc);
    if (out) out << id << '\n';
}

std::string resolveMachineId() {
    // 1. Cache next to the client: stable even when the OS id is unavailable,
    //    and cheap to read.
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::exists("assets", ec)
                                    ? std::filesystem::path("assets")
                                    : std::filesystem::current_path(ec);
    const std::string cachePath = (dir / "machine_id").string();

    const std::string cached = readMachineIdFile(cachePath);
    if (!cached.empty()) return cached;

    // 2. Derive from the OS id (shared by every process on this PC).
    std::string id;
    const std::string osId = machineIdFromOS();
    if (!osId.empty()) {
        id = hashId(osId).substr(0, MACHINE_ID_LENGTH);
    } else {
        // 3. No OS id: random once per install.
        std::random_device rd;
        std::mt19937_64 rng((static_cast<uint64_t>(rd()) << 32) ^ rd() ^
                            static_cast<uint64_t>(std::chrono::system_clock::now()
                                .time_since_epoch().count()));
        std::ostringstream text;
        for (int i = 0; i < 4; ++i) text << std::hex << std::setw(8) << std::setfill('0')
                                         << static_cast<uint32_t>(rng());
        id = text.str();
        std::cout << "No OS machine id found - using a random one (" << id << ")" << std::endl;
    }

    writeMachineIdFile(cachePath, id);
    return id;
}

// ========== NETWORK CLIENT ==========

NetworkClient::NetworkClient() : socketFd(SOCKET_INVALID), connected(false), connecting(false), clientId(0), sequence(0) {
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;   // families make the address "unset"
    machineId_ = resolveMachineId();   // shared by every instance on this PC
}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::resolveTarget(const std::string& address, uint16_t port) {
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &serverAddr.sin_addr) != 1) {
        lastError = "Invalid server address: " + address;
        std::cerr << lastError << std::endl;
        return false;
    }
    serverAddressText = address;
    remotePort        = port;
    return true;
}

bool NetworkClient::connect(const std::string& serverAddress, uint16_t port) {
#ifdef _WIN32
    static bool wsaInitialized = false;
    if (!wsaInitialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            lastError = "Failed to initialize Winsock";
            std::cerr << lastError << std::endl;
            return false;
        }
        wsaInitialized = true;
    }
#endif

    // A client owns exactly one session. Asking for the server we are already
    // talking to (even while the handshake is still in flight) must not open a
    // second session: the server would register a new player for every click.
    if ((connected || connecting) && socketFd != SOCKET_INVALID) {
        const bool sameAddress = serverAddressText == serverAddress && remotePort == port;
        if (!sameAddress) {
            lastError = "Already connected to " + serverAddressText + ":" +
                        std::to_string(remotePort) + " - disconnect first";
            std::cerr << lastError << std::endl;
            return false;
        }
        // Same target: this is a handshake retry, not a new connection.
        lastError.clear();
        return sendConnectPacket();
    }

    // A fresh session always starts from scratch: an earlier (failed) target
    // must not inherit its port/address, or the handshake goes elsewhere.
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    lastError.clear();
    if (!resolveTarget(serverAddress, port)) {
        return false;
    }

    socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd == SOCKET_INVALID) {
        lastError = "Failed to create socket";
        std::cerr << lastError << std::endl;
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

    clientId  = 0;
    connected = false;
    connecting = true;
    handshakeStart = nowSeconds();
    return sendConnectPacket();
}

bool NetworkClient::sendConnectPacket() {
    if (socketFd == SOCKET_INVALID) return false;

    // Send connect packet. A retry carries the id the server handed out in a
    // first ACK that we missed, so the server answers the same player instead
    // of registering a duplicate. The machine id lets the server apply its
    // "players per machine" limit (several instances on one PC share it).
    ConnectPacket packet{};
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_CONNECT;
    packet.header.sequence = sequence++;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = clientId;
    if (!machineId_.empty()) {
        std::strncpy(packet.machineId, machineId_.c_str(), MACHINE_ID_LENGTH - 1);
    }
    
    if (!sendPacket(&packet, sizeof(packet))) {
        lastError = "Failed to send connect packet";
        std::cerr << lastError << std::endl;
        closeSocket();
        return false;
    }

    // Transmission time, after the (cheap, but not instant) send.
    lastHandshakeSend = nowSeconds();
    
    std::cout << "Connecting to server at " << serverAddressText << ":" << remotePort << "..." << std::endl;
    return true;
}

void NetworkClient::closeSocket() {
    if (socketFd != SOCKET_INVALID) {
        CLOSE_SOCKET(socketFd);
        socketFd = SOCKET_INVALID;
    }
    connected  = false;
    connecting = false;
    clientId   = 0;
    assignedTeam = 0;
    lastOwnStateSequence_ = 0;
}

double NetworkClient::secondsSinceLastReceive() const {
    if (lastReceive <= 0.0) return 0.0;
    return nowSeconds() - lastReceive;
}

void NetworkClient::disconnect() {
    if ((connected || connecting) && socketFd != SOCKET_INVALID) {
        PacketHeader header;
        header.magic = PACKET_MAGIC;
        header.type = PACKET_DISCONNECT;
        header.sequence = sequence++;
        header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        header.clientId = clientId;
        
        sendPacket(&header, sizeof(header));
    }
    
    closeSocket();
}

void NetworkClient::sendPlayerInput(float moveX, float moveZ, float yaw, float pitch,
                                     bool jump, bool shoot, uint8_t weaponId, float posX, float posY, float posZ,
                                     bool alive, uint8_t gameMode, uint8_t matchFormat,
                                     uint8_t stance, bool inMenu) {
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
    packet.alive = alive;
    packet.gameMode = gameMode;
    packet.matchFormat = matchFormat;
    packet.stance = stance;
    packet.inMenu = inMenu ? 1 : 0;

    sendPacket(&packet, sizeof(packet));
}

void NetworkClient::sendPing() {
    if (!connected) return;

    PacketHeader ping;
    ping.magic = PACKET_MAGIC;
    ping.type = PACKET_PING;
    ping.sequence = sequence++;
    ping.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    ping.clientId = clientId;

    // Even a failed send updates the clock: the next attempt comes next tick.
    sendPacket(&ping, sizeof(ping));
    lastPingSend = nowSeconds();
}

void NetworkClient::sendProjectileSpawn(const glm::vec3& pos, const glm::vec3& vel,
                                        float lifetime, uint8_t type) {
    if (!connected) return;

    ProjectileSpawnPacket packet;
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_PROJECTILE_SPAWN;
    packet.header.sequence = sequence++;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = clientId;

    packet.posX = pos.x;
    packet.posY = pos.y;
    packet.posZ = pos.z;
    packet.velX = vel.x;
    packet.velY = vel.y;
    packet.velZ = vel.z;
    packet.lifetime = lifetime;
    packet.projectileType = type;
    packet.ownerId = clientId;

    sendPacket(&packet, sizeof(packet));
}

void NetworkClient::sendPlayerDamage(uint32_t victimId, uint16_t damage, uint8_t weaponId) {
    if (!connected || victimId == 0) return;

    PlayerDamagePacket packet;
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_PLAYER_DAMAGE;
    packet.header.sequence = sequence++;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = victimId;      // who was hit
    packet.damage = damage;
    packet.weaponId = weaponId;

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

void NetworkClient::sendRoundStart(uint8_t gameMode, uint8_t attackingTeam,
                                   uint8_t matchFormat, bool freshMatch) {
    if (!connected) return;

    RoundStartPacket packet{};
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_ROUND_START;
    packet.header.sequence = sequence++;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = clientId;
    packet.gameMode = gameMode;
    packet.attackingTeam = attackingTeam & 1u;
    packet.matchFormat = matchFormat;
    packet.freshMatch = freshMatch ? 1 : 0;
    sendPacket(&packet, sizeof(packet));
}

void NetworkClient::sendLobbySetup(uint8_t team, uint8_t botsTeamA, uint8_t botsTeamB,
                                   uint8_t botAccuracy, uint8_t matchFormat,
                                   bool freshMatch, const std::string& mapName,
                                   uint8_t gameMode, bool ready, bool inLobby) {
    if (!connected) return;

    LobbySetupPacket packet{};
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_LOBBY_SETUP;
    packet.header.sequence = sequence++;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = clientId;
    packet.team = team & 1u;
    packet.botsTeamA = botsTeamA;
    packet.botsTeamB = botsTeamB;
    packet.botAccuracy = botAccuracy;
    packet.matchFormat = matchFormat;
    packet.gameMode = gameMode;
    packet.ready = ready ? 1 : 0;
    packet.freshMatch = freshMatch ? 1 : 0;
    packet.inLobby = inLobby ? 1 : 0;
    std::strncpy(packet.mapName, mapName.c_str(), sizeof(packet.mapName) - 1);
    packet.mapName[sizeof(packet.mapName) - 1] = '\0';
    std::strncpy(packet.playerName, playerName_.c_str(), sizeof(packet.playerName) - 1);
    packet.playerName[sizeof(packet.playerName) - 1] = '\0';
    sendPacket(&packet, sizeof(packet));
}

void NetworkClient::sendBombInteract(uint8_t action) {
    if (!connected) return;

    BombInteractPacket packet{};
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_BOMB_INTERACT;
    packet.header.sequence = sequence++;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = clientId;
    packet.action = action;
    sendPacket(&packet, sizeof(packet));
}

void NetworkClient::sendBuyAction(uint8_t item) {
    if (!connected) return;

    BuyActionPacket packet{};
    packet.header.magic = PACKET_MAGIC;
    packet.header.type = PACKET_BUY_ACTION;
    packet.header.sequence = sequence++;
    packet.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.header.clientId = clientId;
    packet.item = item;
    sendPacket(&packet, sizeof(packet));
}

void NetworkClient::update() {
    if ((!connected && !connecting) || socketFd == SOCKET_INVALID) return;

    // Retransmit an unanswered handshake: UDP may drop the CONNECT, and the
    // player would otherwise sit on "CONNECTING..." until the menu is reopened.
    // The timeout is measured from the start of the attempt, not from the last
    // retransmission (which moves the clock forward every retry).
    if (connecting) {
        const double now = nowSeconds();
        if (now - handshakeStart >= kHandshakeTimeout) {
            lastError = "No answer from " + serverAddressText + ":" +
                        std::to_string(remotePort);
            std::cerr << lastError << std::endl;
            closeSocket();
            return;
        }
        if (now - lastHandshakeSend >= kHandshakeRetryInterval) {
            sendConnectPacket();
        }
    } else if (nowSeconds() - lastPingSend >= kKeepaliveInterval) {
        // Keepalive: a client sitting in a menu sends nothing else, so the
        // server would drop it as timed out. The PONG (below) also keeps our
        // own server-liveness timer (secondsSinceLastReceive) happy, since a
        // lone player on a map receives no broadcasts at all.
        sendPing();
    }
    
    char buffer[MAX_PACKET_SIZE];
    size_t received;
    
    // Process all available packets
    while (receivePacket(buffer, sizeof(buffer), received)) {
        if (received < sizeof(PacketHeader)) continue;
        
        PacketHeader* header = (PacketHeader*)buffer;
        if (header->magic != PACKET_MAGIC) continue;
        lastReceive = nowSeconds();
        
        switch (header->type) {
            case PACKET_CONNECT_ACK:
                if (header->clientId == 0) {
                    // Old-style refusal without a reason (pre-REJECT servers).
                    lastError = "Server rejected the connection";
                    std::cerr << lastError << std::endl;
                    closeSocket();
                    break;
                }
                if (!connected) {
                    std::cout << "Connected to server! Client ID: " << header->clientId
                              << " Team: " << (received >= sizeof(ConnectAckPacket)
                                                   ? static_cast<int>(((const ConnectAckPacket*)buffer)->team)
                                                   : 0)
                              << std::endl;
                }
                clientId = header->clientId;
                if (received >= sizeof(ConnectAckPacket)) {
                    assignedTeam = ((const ConnectAckPacket*)buffer)->team & 1u;
                }
                connecting = false;
                connected = true;
                lastReceive = nowSeconds();
                // First keepalive one interval from now (sendPing sets the clock).
                lastPingSend = lastReceive - kKeepaliveInterval;
                rejectReason_ = 0;
                lastError.clear();
                break;

            case PACKET_CONNECT_REJECT: {
                // The server says why: show it on the connect screen.
                const ConnectRejectPacket* reject = (const ConnectRejectPacket*)buffer;
                rejectReason_ = (received >= sizeof(ConnectRejectPacket)) ? reject->reason : 0;
                const int limit = (received >= sizeof(ConnectRejectPacket)) ? reject->limit : 0;
                if (rejectReason_ == REJECT_MACHINE_LIMIT) {
                    lastError = "Server limit: " + std::to_string(limit) +
                                " player(s) per machine already connected";
                } else if (rejectReason_ == REJECT_SERVER_FULL) {
                    lastError = "Server is full";
                } else {
                    lastError = "Server rejected the connection";
                }
                std::cerr << lastError << std::endl;
                closeSocket();
                break;
            }
                
            case PACKET_PLAYER_STATE:
                if (connected && received >= sizeof(PlayerStatePacket)) {
                    const PlayerStatePacket& state = *(PlayerStatePacket*)buffer;
                    // The server may change our team in the pre-match setup.
                    // Ignore stale state packets so a late team update cannot
                    // undo a newer one.
                    if (state.playerId == clientId && state.team <= 1 &&
                        (state.header.sequence == 0 ||
                         state.header.sequence > lastOwnStateSequence_)) {
                        lastOwnStateSequence_ = state.header.sequence;
                        assignedTeam = static_cast<uint8_t>(state.team & 1u);
                    }
                    if (onPlayerState) onPlayerState(state);
                }
                break;
                
            case PACKET_PLAYER_JOIN:
                if (connected && received >= sizeof(PlayerJoinPacket) && onPlayerJoin) {
                    onPlayerJoin(*(PlayerJoinPacket*)buffer);
                }
                break;
                
            case PACKET_PLAYER_LEAVE:
                if (connected && onPlayerLeave) {
                    onPlayerLeave(header->clientId);
                }
                break;
                
            case PACKET_PROJECTILE_SPAWN:
                if (connected && received >= sizeof(ProjectileSpawnPacket) && onProjectileSpawn) {
                    onProjectileSpawn(*(ProjectileSpawnPacket*)buffer);
                }
                break;

            case PACKET_MAP_PLAYER_COUNT:
                if (connected && received >= sizeof(MapPlayerCountPacket) && onMapPlayerCount) {
                    onMapPlayerCount(*(MapPlayerCountPacket*)buffer);
                }
                break;

            case PACKET_BOMB_STATE:
                if (connected && received >= sizeof(BombStatePacket) && onBombState) {
                    onBombState(*(BombStatePacket*)buffer);
                }
                break;

            case PACKET_LOBBY_STATE:
                if (connected && received >= sizeof(LobbyStatePacket) && onLobbyState) {
                    onLobbyState(*(LobbyStatePacket*)buffer);
                }
                break;

            case PACKET_LOBBY_START:
                if (connected && received >= sizeof(LobbyStartPacket) && onLobbyStart) {
                    onLobbyStart(*(LobbyStartPacket*)buffer);
                }
                break;

            case PACKET_MATCH_STATE:
                if (connected && received >= sizeof(MatchStatePacket) && onMatchState) {
                    onMatchState(*(MatchStatePacket*)buffer);
                }
                break;

            case PACKET_ECONOMY_STATE:
                if (connected && received >= sizeof(EconomyStatePacket) && onEconomyState) {
                    onEconomyState(*(EconomyStatePacket*)buffer);
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

NetworkServer::NetworkServer() : socketFd(SOCKET_INVALID), running(false), nextClientId(1), maxPlayers(16) {
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
    if (deltaTime > 0.0f) lastDeltaTime = deltaTime;

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
    
    // Drop clients that went silent: a client that crashed or closed the game
    // must not keep a slot forever (and the game does not send PINGs yet).
    removeTimedOutPlayers();
}

void NetworkServer::removeTimedOutPlayers() {
    std::vector<uint32_t> timedOut;
    for (auto& pair : players) {
        if (pair.second.isBot) continue;   // bots have no socket to time out
        pair.second.lastPacketTime += lastDeltaTime;
        pair.second.inputIdle += lastDeltaTime;
        if (pair.second.lastPacketTime >= timeoutSeconds) {
            timedOut.push_back(pair.first);
        }
    }
    for (uint32_t id : timedOut) {
        std::cout << "Player " << id << " timed out (no packet for "
                  << timeoutSeconds << "s)" << std::endl;
        removePlayer(id);
    }
}

void NetworkServer::processPacket(const void* data, size_t size, const sockaddr_in& from) {
    if (size < sizeof(PacketHeader)) return;
    
    PacketHeader* header = (PacketHeader*)data;
    if (header->magic != PACKET_MAGIC) return;

    // Any packet from a known player is proof the client is still alive.
    // PACKET_PLAYER_DAMAGE names the victim in clientId (the shooter is the
    // packet's sender), so its liveness is resolved from the source address.
    if (header->type == PACKET_PLAYER_DAMAGE) {
        if (ServerPlayer* sender = findPlayerByAddress(from)) sender->lastPacketTime = 0.0f;
    } else if (header->clientId != 0) {
        auto known = players.find(header->clientId);
        if (known != players.end()) {
            known->second.lastPacketTime = 0.0f;
        }
    }
    
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
            std::strncpy(resp.name, serverName.c_str(), sizeof(resp.name) - 1);
            sendPacket(&resp, sizeof(resp), from);
            break;
        }

        case PACKET_CONNECT: {
            // Machine id of the client (empty for older clients / none found).
            char machineId[MACHINE_ID_LENGTH] = {};
            if (size >= sizeof(ConnectPacket)) {
                const ConnectPacket* request = (const ConnectPacket*)data;
                for (size_t i = 0; i < MACHINE_ID_LENGTH; ++i) {
                    const char c = request->machineId[i];
                    if (c == '\0') break;
                    machineId[i] = c;
                }
            }

            // One client, one player. A client that retries its handshake
            // (or re-sends CONNECT because it missed our ACK) already carries
            // its id: answer the existing player instead of registering a new
            // one. This is what kept a single client from showing up as
            // "Player 1..9 connected" after a few clicks.
            ServerPlayer* existing = nullptr;
            if (header->clientId != 0) {
                existing = getPlayer(header->clientId);
            }
            if (!existing) {
                existing = findPlayerByAddress(from);
            }
            if (existing) {
                // Same client coming back for an ACK (retry / lost ACK):
                // re-acknowledge, update its address, register nothing.
                existing->lastPacketTime = 0.0f;
                if (machineId[0] != '\0') {
                    std::strncpy(existing->machineId, machineId, MACHINE_ID_LENGTH - 1);
                }
                if (!(existing->address.sin_addr.s_addr == from.sin_addr.s_addr &&
                      existing->address.sin_port == from.sin_port)) {
                    // Address changed (NAT rebind): keep the session, the old
                    // endpoint is unreachable anyway.
                    std::cout << "Player " << existing->id << " address updated to "
                              << inet_ntoa(from.sin_addr) << std::endl;
                    existing->address = from;
                }

                ConnectAckPacket reAck{};
                reAck.header.magic = PACKET_MAGIC;
                reAck.header.type = PACKET_CONNECT_ACK;
                reAck.header.sequence = 0;
                reAck.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                reAck.header.clientId = existing->id;
                reAck.team = existing->team;
                sendPacket(&reAck, sizeof(reAck), from);
                break;
            }

            // Check if server is full
            if (players.size() >= static_cast<size_t>(maxPlayers)) {
                std::cout << "Connection rejected: Server is full (" << players.size() << "/" << maxPlayers << ")" << std::endl;
                sendReject(from, REJECT_SERVER_FULL,
                           static_cast<uint8_t>(std::min(maxPlayers, 255)));
                break;
            }

            // Check the per-machine limit (e.g. "2 instances per PC").
            // -1 = unlimited; 0 = nobody may join.
            if (maxPlayersPerMachine >= 0) {
                const int fromMachine = getPlayerCountForMachine(from, machineId);
                if (fromMachine >= maxPlayersPerMachine) {
                    std::cout << "Connection rejected: machine limit reached ("
                              << fromMachine << "/" << maxPlayersPerMachine << ") for "
                              << inet_ntoa(from.sin_addr)
                              << (machineId[0] ? "" : " (no machine id)") << std::endl;
                    sendReject(from, REJECT_MACHINE_LIMIT,
                               static_cast<uint8_t>(std::min(maxPlayersPerMachine, 255)));
                    break;
                }
            }

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
            player.lastPacketTime = 0.0f;
            player.shootCooldown = 0.0f;
            std::strncpy(player.machineId, machineId, MACHINE_ID_LENGTH - 1);
            player.machineId[MACHINE_ID_LENGTH - 1] = '\0';
            strcpy(player.mapName, "simple.map"); // Default map
            std::snprintf(player.playerName, sizeof(player.playerName),
                          "player%u", newId);
            player.lobbyMapName[0] = '\0';
            player.inLobby = false;
            player.lobbyReady = false;

            // Balance the two teams: the new player joins the smaller one, so
            // two clients start on opposite sides of a team map. The newly
            // inserted record is already in the map (active, team 0) and must
            // not be counted as an existing player.
            int teamCounts[2] = {0, 0};
            for (const auto& pair : players) {
                if (pair.first == newId) continue;
                if (pair.second.active) teamCounts[pair.second.team & 1]++;
            }
            player.team = static_cast<uint8_t>(teamCounts[0] <= teamCounts[1] ? 0 : 1);
            player.gameMode = 0;
            
            // Send connect ack (with the assigned team)
            ConnectAckPacket ack{};
            ack.header.magic = PACKET_MAGIC;
            ack.header.type = PACKET_CONNECT_ACK;
            ack.header.sequence = 0;
            ack.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            ack.header.clientId = newId;
            ack.team = player.team;
            sendPacket(&ack, sizeof(ack), from);
            
            // Notify other players on the same map only
            PlayerJoinPacket join;
            join.header.magic = PACKET_MAGIC;
            join.header.type = PACKET_PLAYER_JOIN;
            join.header.sequence = 0;
            join.header.timestamp = ack.header.timestamp;
            join.header.clientId = newId;
            join.playerId = newId;
            join.spawnX = player.position.x;
            join.spawnY = player.position.y;
            join.spawnZ = player.position.z;
            join.team = player.team;
            broadcastToMap(player.mapName, &join, sizeof(join), newId);
            
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
            // Accept an input from a client built before the trailing inMenu
            // flag existed too: a mixed-version LAN match must not turn into
            // one-way movement (the newer server would silently drop every
            // input from the older client and freeze its body at spawn).
            constexpr size_t kLegacyInputSize = offsetof(PlayerInputPacket, inMenu);
            if (size < kLegacyInputSize) return;

            PlayerInputPacket input{};
            std::memcpy(&input, data, std::min(size, sizeof(input)));
            const uint32_t id = input.header.clientId;

            if (players.find(id) != players.end()) {
                ServerPlayer& player = players[id];
                player.lastInputSequence = input.header.sequence;
                player.inputIdle = 0.0f;

                // Notify game logic
                if (onPlayerInput) {
                    onPlayerInput(id, input);
                }
            }
            break;
        }

        case PACKET_PLAYER_DAMAGE: {
            // The shooter resolved the hit against the victim's body (the same
            // box everyone is drawn with); the server only validates it and
            // relays the outcome to both sides. header->clientId is the
            // victim, so the shooter is looked up by the packet's source
            // address (and can be the victim itself for self-damage).
            if (size < sizeof(PlayerDamagePacket)) return;
            const PlayerDamagePacket* hit = (const PlayerDamagePacket*)data;
            if (hit->header.clientId == 0 || hit->header.clientId == 0xFFFFFFFFu) return;

            auto victim = players.find(hit->header.clientId);
            if (victim == players.end() || !victim->second.active) break;

            ServerPlayer* shooter = findPlayerByAddress(from);
            if (!shooter) break;

            if (onPlayerDamage) {
                onPlayerDamage(shooter->id, *hit);
            }
            break;
        }

        case PACKET_PROJECTILE_SPAWN: {
            // A client fired a projectile. The local client is authoritative
            // about what it fired (weapon type, pellet count, ammo use and
            // start position); the server only forwards the exact projectile
            // to the other players on its map. Deriving shots from the input
            // flags used to turn a shotgun into grenades, because weapon ids
            // and projectile types are numbered differently.
            if (size < sizeof(ProjectileSpawnPacket)) return;
            ProjectileSpawnPacket* spawn = (ProjectileSpawnPacket*)data;

            ServerPlayer* owner = findPlayerByAddress(from);
            if (!owner) break;

            // Never trust an owner id inside the packet: it must be the
            // sender, or a client could spawn projectiles as someone else.
            spawn->header.clientId = owner->id;
            spawn->ownerId = owner->id;
            if (spawn->projectileType > 4) break;
            if (spawn->lifetime <= 0.0f || spawn->lifetime > 10.0f) break;

            if (onProjectileSpawned) {
                onProjectileSpawned(owner->id, *spawn);
            }

            broadcastToMap(owner->mapName, spawn, sizeof(*spawn), owner->id);
            break;
        }
        
        case PACKET_PING: {
            // Keepalive (or latency probe): answer so the client knows the
            // session is alive. lastReceive is refreshed on any packet, and
            // the game disconnects when nothing arrives for 5 seconds.
            PacketHeader pong;
            pong.magic = PACKET_MAGIC;
            pong.type = PACKET_PONG;
            pong.sequence = 0;
            pong.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            pong.clientId = header->clientId;
            sendPacket(&pong, sizeof(pong), from);
            break;
        }

        case PACKET_PONG: {
            // Update player ping/latency if needed
            break;
        }

        case PACKET_ROUND_START: {
            if (size < sizeof(RoundStartPacket)) return;
            const RoundStartPacket* start = (const RoundStartPacket*)data;
            ServerPlayer* player = findPlayerByAddress(from);
            if (!player || !player->active) break;

            player->gameMode = start->gameMode;
            player->matchFormat = start->matchFormat;
            if (start->gameMode == 2 && onRoundStarted) {
                onRoundStarted(player->mapName, static_cast<int>(start->gameMode),
                               static_cast<int>(start->attackingTeam & 1u),
                               static_cast<int>(start->matchFormat),
                               start->freshMatch != 0);
            }
            break;
        }

        case PACKET_LOBBY_SETUP: {
            if (size < sizeof(LobbySetupPacket)) return;
            const LobbySetupPacket* setup = (const LobbySetupPacket*)data;
            ServerPlayer* player = findPlayerByAddress(from);
            if (!player || !player->active || player->isBot) break;
            player->team = static_cast<uint8_t>(setup->team & 1u);
            player->matchFormat = setup->matchFormat;
            player->gameMode = setup->gameMode;
            player->inLobby = setup->inLobby != 0;
            player->lobbyReady = player->inLobby && setup->ready != 0;
            player->lobbyGameMode = setup->gameMode;
            player->lobbyMatchFormat = setup->matchFormat;
            player->lobbyBotsTeamA = setup->botsTeamA;
            player->lobbyBotsTeamB = setup->botsTeamB;
            player->lobbyBotAccuracy = setup->botAccuracy;
            if (setup->mapName[0] != '\0') {
                std::strncpy(player->lobbyMapName, setup->mapName,
                             sizeof(player->lobbyMapName) - 1);
                player->lobbyMapName[sizeof(player->lobbyMapName) - 1] = '\0';
            }
            if (setup->playerName[0] != '\0') {
                std::strncpy(player->playerName, setup->playerName,
                             sizeof(player->playerName) - 1);
                player->playerName[sizeof(player->playerName) - 1] = '\0';
            }
            if (onLobbySetup) onLobbySetup(player->id, *setup);
            break;
        }

        case PACKET_BOMB_INTERACT: {
            if (size < sizeof(BombInteractPacket)) return;
            const BombInteractPacket* request = (const BombInteractPacket*)data;
            ServerPlayer* player = findPlayerByAddress(from);
            if (!player || !player->active) break;
            if (request->action != BOMB_INTERACT_PLANT &&
                request->action != BOMB_INTERACT_DEFUSE) break;
            if (onBombInteract) onBombInteract(player->id, request->action);
            break;
        }

        case PACKET_BUY_ACTION: {
            if (size < sizeof(BuyActionPacket)) return;
            const BuyActionPacket* request = (const BuyActionPacket*)data;
            ServerPlayer* player = findPlayerByAddress(from);
            if (!player || !player->active) break;
            if (onBuyAction) onBuyAction(player->id, *request);
            break;
        }

        case PACKET_MAP_CHANGE: {
            if (size < sizeof(MapChangePacket)) return;

            MapChangePacket* mapChange = (MapChangePacket*)data;
            uint32_t id = mapChange->header.clientId;

            if (players.find(id) != players.end()) {
                std::string newMap(mapChange->mapName);
                std::string oldMap(players[id].mapName);

                // Starting the map ends the pre-match lobby membership. The
                // server's periodic lobby broadcast clears the row for others.
                players[id].inLobby = false;
                players[id].lobbyReady = false;

                // Update player's map. A new map is a new spawn: one second of
                // protection so nobody is shot while the map is still coming up.
                strncpy(players[id].mapName, newMap.c_str(), sizeof(players[id].mapName) - 1);
                players[id].mapName[sizeof(players[id].mapName) - 1] = '\0';
                players[id].spawnProtection = 1.0f;

                std::cout << "Player " << id << " changed map from " << oldMap << " to " << newMap << std::endl;

                // Notify players on old map that this player left
                PlayerLeavePacket leave;
                leave.header.magic = PACKET_MAGIC;
                leave.header.type = PACKET_PLAYER_LEAVE;
                leave.header.sequence = 0;
                leave.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                leave.header.clientId = id;
                leave.playerId = id;
                broadcastToMap(oldMap, &leave, sizeof(leave), id);

                // Notify players on new map that this player joined
                PlayerJoinPacket join;
                join.header.magic = PACKET_MAGIC;
                join.header.type = PACKET_PLAYER_JOIN;
                join.header.sequence = 0;
                join.header.timestamp = leave.header.timestamp;
                join.header.clientId = id;
                join.playerId = id;
                join.spawnX = players[id].position.x;
                join.spawnY = players[id].position.y;
                join.spawnZ = players[id].position.z;
                join.team = players[id].team;
                broadcastToMap(newMap, &join, sizeof(join), id);

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
    auto stateIt = players.find(playerId);
    packet.team = (stateIt != players.end()) ? stateIt->second.team : 0;
    packet.stance = (stateIt != players.end()) ? stateIt->second.stance : 0;
    packet.weaponId = (stateIt != players.end()) ? (stateIt->second.weaponId % 6) : 0;
    if (stateIt != players.end()) {
        packet.header.sequence = ++stateIt->second.stateSequence;
    }

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
        if (pair.first != excludeId && pair.second.active && !pair.second.isBot) {
            sendPacket(data, size, pair.second.address);
        }
    }
}

bool NetworkServer::sendToPlayer(uint32_t id, const void* data, size_t size) {
    auto it = players.find(id);
    if (it == players.end() || !it->second.active || it->second.isBot) return false;
    sendPacket(data, size, it->second.address);
    return true;
}

void NetworkServer::sendReject(const sockaddr_in& to, uint8_t reason, uint8_t limit) {
    ConnectRejectPacket reject{};
    reject.header.magic = PACKET_MAGIC;
    reject.header.type = PACKET_CONNECT_REJECT;
    reject.header.sequence = 0;
    reject.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    reject.header.clientId = 0;
    reject.reason = reason;
    reject.limit = limit;
    sendPacket(&reject, sizeof(reject), to);
}

bool NetworkServer::sameMachine(const ServerPlayer& player, const sockaddr_in& addr,
                                const char* machineId) {
    // Both sides know the machine id: same PC = same id AND same address
    // (different NATs may reuse ids, so the address must match too).
    if (machineId && machineId[0] != '\0' && player.machineId[0] != '\0') {
        return std::strncmp(player.machineId, machineId, MACHINE_ID_LENGTH) == 0 &&
               player.address.sin_addr.s_addr == addr.sin_addr.s_addr;
    }
    // Unknown id on either side: fall back to one machine per IPv4 address.
    return player.address.sin_addr.s_addr == addr.sin_addr.s_addr;
}

int NetworkServer::getPlayerCountForMachine(const sockaddr_in& addr, const char* machineId) const {
    int count = 0;
    for (const auto& pair : players) {
        if (!pair.second.active || pair.second.isBot) continue;
        if (sameMachine(pair.second, addr, machineId)) ++count;
    }
    return count;
}

void NetworkServer::broadcastToMap(const std::string& mapName, const void* data, size_t size, uint32_t excludeId) {
    for (auto& pair : players) {
        if (pair.first != excludeId && pair.second.active && !pair.second.isBot) {
            if (mapName == pair.second.mapName) {
                sendPacket(data, size, pair.second.address);
            }
        }
    }
}

void NetworkServer::broadcastPlayerStateToMap(uint32_t playerId, const std::string& mapName,
                                               const glm::vec3& pos, const glm::vec3& vel,
                                               float yaw, float pitch, float health, bool isDead,
                                               bool includePlayer) {
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
    auto stateIt = players.find(playerId);
    packet.team = (stateIt != players.end()) ? stateIt->second.team : 0;
    packet.stance = (stateIt != players.end()) ? stateIt->second.stance : 0;
    packet.weaponId = (stateIt != players.end()) ? (stateIt->second.weaponId % 6) : 0;
    if (stateIt != players.end()) {
        packet.header.sequence = ++stateIt->second.stateSequence;
    }

    // Damage and respawn outcomes must also reach the player they describe
    // (otherwise the victim never learns it died). Ordinary position updates
    // keep excluding the sender.
    broadcastToMap(mapName, &packet, sizeof(packet), includePlayer ? 0 : playerId);
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

void NetworkServer::movePlayerToMap(uint32_t playerId, const std::string& mapName,
                                    const glm::vec3& position) {
    auto it = players.find(playerId);
    if (it == players.end() || !it->second.active) return;

    ServerPlayer& player = it->second;
    const std::string oldMap(player.mapName);
    player.position = position;
    player.velocity = glm::vec3(0.0f);

    if (oldMap == mapName) return;

    strncpy(player.mapName, mapName.c_str(), sizeof(player.mapName) - 1);
    player.mapName[sizeof(player.mapName) - 1] = '\0';

    const uint32_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    PlayerLeavePacket leave;
    leave.header.magic = PACKET_MAGIC;
    leave.header.type = PACKET_PLAYER_LEAVE;
    leave.header.sequence = 0;
    leave.header.timestamp = now;
    leave.header.clientId = playerId;
    leave.playerId = playerId;
    broadcastToMap(oldMap, &leave, sizeof(leave), playerId);

    PlayerJoinPacket join;
    join.header.magic = PACKET_MAGIC;
    join.header.type = PACKET_PLAYER_JOIN;
    join.header.sequence = 0;
    join.header.timestamp = now;
    join.header.clientId = playerId;
    join.playerId = playerId;
    join.spawnX = position.x;
    join.spawnY = position.y;
    join.spawnZ = position.z;
    join.team = player.team;
    broadcastToMap(mapName, &join, sizeof(join), playerId);

    broadcastMapPlayerCounts();
}

uint32_t NetworkServer::addBot(const std::string& mapName, uint8_t team,
                               const glm::vec3& position) {
    uint32_t id = nextBotId++;
    while (players.find(id) != players.end()) id = nextBotId++;

    ServerPlayer& player = players[id];
    player = ServerPlayer{};
    player.id = id;
    memset(&player.address, 0, sizeof(player.address));
    player.position = position;
    player.velocity = glm::vec3(0.0f);
    player.yaw = 0.0f;
    player.pitch = 0.0f;
    player.health = 100.0f;
    player.isDead = false;
    player.active = true;
    player.lastInputSequence = 0;
    player.lastPingTime = 0.0f;
    player.lastPacketTime = 0.0f;
    player.shootCooldown = 0.0f;
    player.team = team;
    player.gameMode = 0;
    player.isBot = true;
    std::strncpy(player.mapName, mapName.c_str(), sizeof(player.mapName) - 1);
    player.mapName[sizeof(player.mapName) - 1] = '\0';

    PlayerJoinPacket join;
    join.header.magic = PACKET_MAGIC;
    join.header.type = PACKET_PLAYER_JOIN;
    join.header.sequence = 0;
    join.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    join.header.clientId = id;
    join.playerId = id;
    join.spawnX = position.x;
    join.spawnY = position.y;
    join.spawnZ = position.z;
    join.team = team;
    broadcastToMap(mapName, &join, sizeof(join), id);
    broadcastMapPlayerCounts();

    std::cout << "[Server] Bot " << id << " spawned on " << mapName << std::endl;
    return id;
}

void NetworkServer::removeBot(uint32_t id) {
    auto it = players.find(id);
    if (it == players.end() || !it->second.isBot) return;
    removePlayer(id);
}

bool NetworkServer::isBot(uint32_t id) const {
    auto it = players.find(id);
    return it != players.end() && it->second.isBot;
}

int NetworkServer::getHumanPlayerCount() const {
    int count = 0;
    for (const auto& pair : players) {
        if (pair.second.active && !pair.second.isBot) ++count;
    }
    return count;
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
        std::string oldMap(it->second.mapName);

        // Notify players on the same map only
        PlayerLeavePacket leave;
        leave.header.magic = PACKET_MAGIC;
        leave.header.type = PACKET_PLAYER_LEAVE;
        leave.header.sequence = 0;
        leave.header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        leave.header.clientId = id;
        leave.playerId = id;
        broadcastToMap(oldMap, &leave, sizeof(leave), id);
        
        // Notify callback
        if (onPlayerDisconnect) {
            onPlayerDisconnect(id);
        }
        
        players.erase(it);

        // Refresh map population counts after disconnect
        broadcastMapPlayerCounts();

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
