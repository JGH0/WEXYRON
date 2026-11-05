#define GLFW_INCLUDE_NONE
#include "game.hpp"
#include "network/network.hpp"
#include "../ui/ui_helpers.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
#endif

// ========== NETWORK MULTIPLAYER IMPLEMENTATION ==========

bool Game::connectToServer(const std::string& address, uint16_t port) {
    // One session per client. While connected (or still handshaking) the same
    // address is a retry, not a second connection - asking the network layer
    // for it again keeps a single player on the server.
    if (networkClient) {
        if (isConnectedToServer() || isConnectingToServer()) {
            if (networkClient->getServerAddress() == address &&
                networkClient->getServerPort() == port) {
                return networkClient->connect(address, port);   // handshake retry
            }
            serverConnectError_ =
                "Already connected to " + networkClient->getServerAddress() + ":" +
                std::to_string(networkClient->getServerPort()) + " - disconnect first";
            return false;
        }
        disconnectFromServer();
    }

    networkClient = new NetworkClient();

    // Set up callbacks
    networkClient->onPlayerState = [this](const PlayerStatePacket& packet) {
        // Only sync health/death state for local player (client controls own movement)
        if (networkClient && packet.playerId == networkClient->getClientId()) {
            // Don't override local position/velocity - client controls own movement
            // Only sync health and death state from server.
            //
            // The server is authoritative on death: when it reports health 0 /
            // isDead the player dies where they stand (after a hit), and the
            // respawned state it sends a few seconds later brings them back.
            if (packet.isDead || packet.health <= 0.0f) {
                if (playerHealth > 0.0f) playerHealth = 0.0f;
                if (!isDead) dieLocally();
            } else if (isDead) {
                // The server put us back on our feet (respawn).
                respawnLocally();
            } else {
                // Alive and well. Health shown is the server's health minus
                // shots we reported that it has not forwarded back yet.
                int pending = 0;
                for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                    if (!remotePlayers[i].active || !pvpRemotes_[i].pending) continue;
                    pending += pvpRemotes_[i].damageSent;
                }
                pending = std::min(pending, 99);
                playerHealth = std::max(0.0f, packet.health - static_cast<float>(pending));
            }
            return;
        }

        // Update remote player state (skip own player)
        if (packet.playerId != networkClient->getClientId()) {
            for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                if (!remotePlayers[i].active || remotePlayers[i].id == packet.playerId) {
                    // A (re)appearing player starts its bookkeeping clean.
                    if (!remotePlayers[i].active || remotePlayers[i].id != packet.playerId) {
                        pvpRemotes_[i] = PvpRemote{};
                    }
                    remotePlayers[i].id       = packet.playerId;
                    remotePlayers[i].position = glm::vec3(packet.posX, packet.posY, packet.posZ);
                    remotePlayers[i].velocity = glm::vec3(packet.velX, packet.velY, packet.velZ);
                    remotePlayers[i].yaw      = packet.yaw;
                    remotePlayers[i].pitch    = packet.pitch;
                    remotePlayers[i].health   = packet.health;
                    remotePlayers[i].isDead   = packet.isDead;
                    remotePlayers[i].active   = true;
                    // The server confirmed health got through: no need to
                    // re-send the damage we reported for this player.
                    pvpRemotes_[i].damageSent = 0;
                    break;
                }
            }
        }
    };

    networkClient->onPlayerJoin = [this](const PlayerJoinPacket& packet) {
        // Skip own player join event
        if (packet.playerId == networkClient->getClientId()) {
            return;
        }

        // Avoid duplicate join entries for same player id
        for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
            if (remotePlayers[i].active && remotePlayers[i].id == packet.playerId) {
                remotePlayers[i].position       = glm::vec3(packet.spawnX, packet.spawnY, packet.spawnZ);
                remotePlayers[i].renderPosition = remotePlayers[i].position;
                remotePlayers[i].velocity       = glm::vec3(0.0f);
                remotePlayers[i].yaw            = 0.0f;
                remotePlayers[i].pitch          = 0.0f;
                remotePlayers[i].health         = 100.0f;
                remotePlayers[i].isDead         = false;
                return;
            }
        }

        std::cout << "Player " << packet.playerId << " joined the game!" << std::endl;
        // Add new remote player
        for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
            if (!remotePlayers[i].active) {
                remotePlayers[i].id             = packet.playerId;
                remotePlayers[i].position       = glm::vec3(packet.spawnX, packet.spawnY, packet.spawnZ);
                remotePlayers[i].renderPosition = remotePlayers[i].position;
                remotePlayers[i].velocity       = glm::vec3(0.0f);
                remotePlayers[i].yaw            = 0.0f;
                remotePlayers[i].pitch          = 0.0f;
                remotePlayers[i].health         = 100.0f;
                remotePlayers[i].isDead         = false;
                remotePlayers[i].active         = true;
                playerCount++; // Update player count
                break;
            }
        }
    };

    networkClient->onPlayerLeave = [this](uint32_t playerId) {
        std::cout << "Player " << playerId << " left the game!" << std::endl;
        // Remove remote player
        for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
            if (remotePlayers[i].active && remotePlayers[i].id == playerId) {
                remotePlayers[i].active         = false;
                remotePlayers[i].id             = 0;
                remotePlayers[i].position       = glm::vec3(0.0f);
                remotePlayers[i].renderPosition = glm::vec3(0.0f);
                remotePlayers[i].velocity       = glm::vec3(0.0f);
                remotePlayers[i].yaw            = 0.0f;
                remotePlayers[i].pitch          = 0.0f;
                remotePlayers[i].health         = 0.0f;
                remotePlayers[i].isDead         = true;
                if (playerCount > 1) playerCount--; // keep at least self while connected
                break;
            }
        }
    };

    networkClient->onProjectileSpawn = [this](const ProjectileSpawnPacket& packet) {
        // Only create projectile if it's from another player (not ourselves)
        // We already created our own projectile locally when we shot
        if (packet.ownerId != networkClient->getClientId()) {
            createProjectile(
                glm::vec3(packet.posX, packet.posY, packet.posZ),
                glm::vec3(packet.velX, packet.velY, packet.velZ),
                packet.lifetime,
                packet.ownerId,
                packet.projectileType // Use the projectile type from the packet
            );
        }
    };

    networkClient->onMapPlayerCount = [this](const MapPlayerCountPacket& packet) {
        // Update map player counts
        const char* mapFiles[] = {"simple.map", "large.map", "arena.map", "sniper.map", "complex.map", "team_arena.map"};
        for (int i = 0; i < 6; i++) {
            mapPlayerCounts[i] = 0;
        }
        for (int i = 0; i < packet.mapCount && i < 8; i++) {
            std::string mapName(packet.maps[i].mapName);
            for (int j = 0; j < 6; j++) {
                if (mapName == mapFiles[j]) {
                    mapPlayerCounts[j] = packet.maps[i].playerCount;
                    break;
                }
            }
        }
    };

    if (!networkClient->connect(address, port)) {
        serverConnectError_ = networkClient->getLastError().empty()
                            ? "Failed to connect to " + address + ":" + std::to_string(port)
                            : networkClient->getLastError();
        std::cerr << "Failed to connect to server at " << address << ":" << port << std::endl;
        delete networkClient;
        networkClient = nullptr;
        return false;
    }

    serverConnectError_.clear();
    multiplayerMode = true;
    playerCount = 1; // Start with self (will be updated when others join)
    std::cout << "Connecting to server at " << address << ":" << port << std::endl;
    return true;
}

void Game::connectionLossDetected() {
    if (!networkClient) return;
    serverConnectError_ = "Disconnected from " + networkClient->getServerAddress() + ":" +
                          std::to_string(networkClient->getServerPort());
    disconnectFromServer();
}

bool Game::hasServerSession() const {
    return networkClient && (networkClient->isConnected() || networkClient->isConnecting());
}

std::string Game::getServerAddress() const {
    return networkClient ? networkClient->getServerAddress() : std::string();
}

uint16_t Game::getServerPort() const {
    return networkClient ? networkClient->getServerPort() : 0;
}

void Game::disconnectFromServer() {
    if (networkClient) {
        networkClient->disconnect();
        delete networkClient;
        networkClient = nullptr;
    }
    multiplayerMode = false;
    playerCount = 0;

    // Clear remote players completely
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        remotePlayers[i].active         = false;
        remotePlayers[i].id             = 0;
        remotePlayers[i].position       = glm::vec3(0.0f);
        remotePlayers[i].renderPosition = glm::vec3(0.0f);
        remotePlayers[i].velocity       = glm::vec3(0.0f);
        remotePlayers[i].yaw            = 0.0f;
        remotePlayers[i].pitch          = 0.0f;
        remotePlayers[i].health         = 0.0f;
        remotePlayers[i].isDead         = true;
    }

    // Clear map population UI counts after disconnect
    for (int i = 0; i < 6; i++) {
        mapPlayerCounts[i] = 0;
    }
}

bool Game::isConnectingToServer() const {
    return networkClient && networkClient->isConnecting();
}

bool Game::isConnectedToServer() const {
    // Belt-and-suspenders: check that networkClient looks like a valid pointer
    // before dereferencing it (prevents crashes from memory corruption)
    if (!networkClient) return false;
    // Sanity check: verify the pointer is in a reasonable address range
    uintptr_t addr = reinterpret_cast<uintptr_t>(networkClient);
    if (addr < 0x1000 || addr > 0x7fffffffffff) return false;
    return networkClient->isConnected();
}

void Game::updateNetwork() {
    // A session may be in flight (CONNECTING) or live (CONNECTED) in any game
    // state, including the connect screen: the handshake ACK, the disconnect
    // and a dropped server must be noticed there too, not only in-game.
    if (!networkClient) return;

    // The network layer closed the session (unanswered or rejected handshake,
    // server gone in-game): drop it and keep the reason for the menu.
    if (!networkClient->isConnected() && !networkClient->isConnecting()) {
        const std::string reason = networkClient->getLastError();
        if (!reason.empty()) {
            serverConnectError_ = reason;
        }
        disconnectFromServer();
        return;
    }

    networkClient->update();
    if (!networkClient->isConnected()) {
        return;   // handshake still in flight (retried by the network layer)
    }

    // A live session goes silent when the server is gone: report it instead of
    // keeping a dead connection (and the "CONNECTED" status) forever.
    if (networkClient->secondsSinceLastReceive() > kServerTimeoutSeconds) {
        std::cerr << "Server stopped responding - disconnecting" << std::endl;
        connectionLossDetected();
    }
}

// ========== LAN DISCOVERY HELPERS (file-scope statics) ==========

#ifdef _WIN32
static bool setNonBlocking(socket_t fd) {
    u_long nonBlocking = 1;
    return ioctlsocket(fd, FIONBIO, &nonBlocking) == 0;
}
#else
static bool setNonBlocking(socket_t fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

static void ensureDiscoverySocket(socket_t& sock) {
    if (sock != SOCKET_INVALID) return;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == SOCKET_INVALID) return;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,  (const char*)&opt, sizeof(opt));
    setNonBlocking(sock);
}

static void sendDiscoverPing(socket_t sock, uint16_t port) {
    if (sock == SOCKET_INVALID) return;

    DiscoverPacket p{};
    p.header.magic     = PACKET_MAGIC;
    p.header.type      = PACKET_DISCOVER;
    p.header.sequence  = 0;
    p.header.timestamp = 0;
    p.header.clientId  = 0;

    // Localhost (fixes "doesn't find server on my PC")
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port   = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &local.sin_addr);
#ifdef _WIN32
    sendto(sock, (const char*)&p, sizeof(p), 0, (sockaddr*)&local, sizeof(local));
#else
    sendto(sock, &p, sizeof(p), 0, (sockaddr*)&local, sizeof(local));
#endif

    // Broadcast
    sockaddr_in to{};
    to.sin_family      = AF_INET;
    to.sin_port        = htons(port);
    to.sin_addr.s_addr = inet_addr("255.255.255.255");
#ifdef _WIN32
    sendto(sock, (const char*)&p, sizeof(p), 0, (sockaddr*)&to, sizeof(to));
#else
    sendto(sock, &p, sizeof(p), 0, (sockaddr*)&to, sizeof(to));
#endif
}

// ---------------------------------------------------------------------------
// Text input for the connect screen (IP / port rows)
// ---------------------------------------------------------------------------
void Game::processServerConnectTextInput() {
    if (menu::isCapturing(serverMenu_) || menuJustEntered) return;

    menu::Page* page = serverMenu_.stack.current();
    if (!page) return;

    // Keep the text-field selection when hovering the IP/PORT rows.
    const int hovered = menu::hitTest(serverMenu_, mouseGUIPosX, mouseGUIPosY);
    if (hovered >= 0) {
        const menu::Row& row = page->rows[hovered];
        if (row.label.rfind("IP:", 0) == 0) serverConnectSelection = 0;
        else if (row.label.rfind("PORT:", 0) == 0) serverConnectSelection = 1;
    }

    // Digits (and '.' for the address) are typed into the selected field.
    for (int key = GLFW_KEY_0; key <= GLFW_KEY_9; ++key) {
        const int idx = key - GLFW_KEY_0;
        if (glfwGetKey(window, key) == GLFW_PRESS && !inp.digitPressed[idx]) {
            inp.digitPressed[idx] = true;
            if (serverConnectSelection == 0) {
                serverAddress += static_cast<char>('0' + idx);
            } else {
                if (serverPort < 65535) serverPort = static_cast<uint16_t>(serverPort * 10 + idx);
            }
        } else if (glfwGetKey(window, key) == GLFW_RELEASE) {
            inp.digitPressed[idx] = false;
        }
    }

    if (serverConnectSelection == 0) {
        if (glfwGetKey(window, GLFW_KEY_PERIOD) == GLFW_PRESS && !inp.periodPressed) {
            inp.periodPressed = true;
            serverAddress += '.';
        } else if (glfwGetKey(window, GLFW_KEY_PERIOD) == GLFW_RELEASE &&
                   glfwGetKey(window, GLFW_KEY_KP_DECIMAL) != GLFW_PRESS) {
            inp.periodPressed = false;
        }
        if (glfwGetKey(window, GLFW_KEY_KP_DECIMAL) == GLFW_PRESS && !inp.periodPressed) {
            inp.periodPressed = true;
            serverAddress += '.';
        }
    }

    if (glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS && !inp.backspacePressed) {
        inp.backspacePressed = true;
        if (serverConnectSelection == 0) {
            if (!serverAddress.empty()) serverAddress.pop_back();
        } else {
            serverPort = static_cast<uint16_t>(serverPort / 10);
        }
    } else if (glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_RELEASE) {
        inp.backspacePressed = false;
    }

    // R rescans the LAN (works on any row).
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !inp.rPressed) {
        inp.rPressed = true;
        const uint16_t port = serverPort == 0 ? DEFAULT_PORT : serverPort;
        sendDiscoverPing(discoverySocketFd, port);
        discoverySendCooldown = 0.75f;
    } else if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) {
        inp.rPressed = false;
    }
}
