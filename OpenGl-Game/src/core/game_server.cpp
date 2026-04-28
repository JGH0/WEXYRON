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
    if (networkClient) {
        disconnectFromServer();
    }

    networkClient = new NetworkClient();

    // Set up callbacks
    networkClient->onPlayerState = [this](const PlayerStatePacket& packet) {
        // Only sync health/death state for local player (client controls own movement)
        if (networkClient && packet.playerId == networkClient->getClientId()) {
            // Don't override local position/velocity - client controls own movement
            // Only sync health and death state from server
            playerHealth = packet.health;
            isDead = packet.isDead;
            return;
        }

        // Update remote player state (skip own player)
        if (packet.playerId != networkClient->getClientId()) {
            for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                if (!remotePlayers[i].active || remotePlayers[i].id == packet.playerId) {
                    remotePlayers[i].id       = packet.playerId;
                    remotePlayers[i].position = glm::vec3(packet.posX, packet.posY, packet.posZ);
                    remotePlayers[i].velocity = glm::vec3(packet.velX, packet.velY, packet.velZ);
                    remotePlayers[i].yaw      = packet.yaw;
                    remotePlayers[i].pitch    = packet.pitch;
                    remotePlayers[i].health   = packet.health;
                    remotePlayers[i].isDead   = packet.isDead;
                    remotePlayers[i].active   = true;
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
        delete networkClient;
        networkClient = nullptr;
        std::cerr << "Failed to connect to server at " << address << ":" << port << std::endl;
        return false;
    }

    multiplayerMode = true;
    playerCount = 1; // Start with self (will be updated when others join)
    std::cout << "Connected to server at " << address << ":" << port << std::endl;
    return true;
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

bool Game::isConnectedToServer() const {
    return networkClient && networkClient->isConnected();
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

// ========== SERVER CONNECTION MENU ==========

void Game::renderServerConnect() {
    // Clear screen
    renderer.clear();

    UIHelpers::setProjection(renderer, window);

    // Compute virtual bounds so we can place the bottom instructions correctly
    auto _b = UIHelpers::getBounds(window);
    float left = _b.left, right = _b.right, top = _b.top, bottom = _b.bottom;
    (void)right; (void)top; // used only through _b.width() below

    // Title - no box, just text
    renderer.drawTextCentered("CONNECT TO SERVER", 400.0f, 75.0f, 1.2f);

    float menuWidth = 400.0f;
    float menuHeight = 40.0f;
    float menuX = (800.0f - menuWidth) / 2.0f;
    float startY = 150.0f;
    float spacing = 50.0f;

    // ---- Server address (no box, just text) ----
    float addressY = startY;
    {
        const char* label = "IP:";
        float labelW = renderer.getTextWidth(label, 0.8f);
        float textY  = addressY + menuHeight/2.0f - 6.0f;
        renderer.drawText(label, menuX, textY, 0.8f);

        std::string ipText = serverAddress.empty() ? std::string("192.168.1.50") : serverAddress;
        float valX = menuX + labelW + 10.0f;
        renderer.drawTextFit(ipText.c_str(), valX, textY, menuWidth - labelW - 10.0f, 0.8f);

        // Highlight when selected for input
        if (serverConnectSelection == 0 && (enteringServerAddress && !focusServerList)) {
            glm::mat4 highlight = glm::mat4(1.0f);
            highlight = glm::translate(highlight, glm::vec3(menuX + menuWidth/2.0f, addressY + menuHeight/2.0f, 0.0f));
            highlight = glm::scale(highlight, glm::vec3(menuWidth, menuHeight, 1.0f));
            renderer.drawColoredQuad(highlight, glm::vec3(0.6f, 0.8f, 0.2f));
            // Redraw text on top
            renderer.drawText(label, menuX, textY, 0.8f);
            renderer.drawTextFit(ipText.c_str(), valX, textY, menuWidth - labelW - 10.0f, 0.8f);
        }
    }

    // ---- Port (no box, just text) ----
    float portY = startY + spacing;
    {
        const char* label = "Port:";
        float labelW = renderer.getTextWidth(label, 0.8f);
        float textY  = portY + menuHeight/2.0f - 6.0f;
        renderer.drawText(label, menuX, textY, 0.8f);

        std::string portStr = std::to_string(serverPort);
        float valX = menuX + labelW + 10.0f;
        renderer.drawTextFit(portStr.c_str(), valX, textY, menuWidth - labelW - 10.0f, 0.8f);

        // Highlight when selected for input
        if (serverConnectSelection == 1) {
            glm::mat4 highlight = glm::mat4(1.0f);
            highlight = glm::translate(highlight, glm::vec3(menuX + menuWidth/2.0f, portY + menuHeight/2.0f, 0.0f));
            highlight = glm::scale(highlight, glm::vec3(menuWidth, menuHeight, 1.0f));
            renderer.drawColoredQuad(highlight, glm::vec3(0.6f, 0.8f, 0.2f));
            // Redraw text on top
            renderer.drawText(label, menuX, textY, 0.8f);
            renderer.drawTextFit(portStr.c_str(), valX, textY, menuWidth - labelW - 10.0f, 0.8f);
        }
    }

    // ---- Discovered servers list ----
    float listY = startY + 2.5f * spacing;
    renderer.drawTextFit("FOUND SERVERS (LAN):", menuX + 25.0f, listY - 22.0f, menuWidth - 40.0f, 0.8f);

    if (discoveredServers.empty()) {
        renderer.drawTextFit("No servers found. Press R to rescan.", menuX + 25.0f, listY + 40.0f, menuWidth - 40.0f, 0.8f);
    } else {
        int maxRows = std::min(3, (int)discoveredServers.size());
        float serverBoxHeight = 60.0f; // Taller boxes for two-line text
        float serverSpacing = 70.0f; // Increased spacing for taller boxes
        for (int i = 0; i < maxRows; i++) {
            const auto& s = discoveredServers[i];
            float serverY = listY + i * serverSpacing;

            // Server item box - taller for two lines
            glm::mat4 serverBox = glm::mat4(1.0f);
            serverBox = glm::translate(serverBox, glm::vec3(menuX + menuWidth/2.0f, serverY + serverBoxHeight/2.0f, 0.0f));
            serverBox = glm::scale(serverBox, glm::vec3(menuWidth, serverBoxHeight, 1.0f));

            // Green if selected in list, blue otherwise
            glm::vec3 serverColor = (serverConnectSelection == 2 + i)
                ? glm::vec3(0.6f, 0.8f, 0.2f)
                : glm::vec3(0.3f, 0.5f, 0.7f);
            renderer.drawColoredQuad(serverBox, serverColor);

            // Server name on first line
            float nameTextScale = 0.85f;
            float nameTextY = serverY + 12.0f;
            renderer.drawTextFit(s.name.c_str(), menuX + 12.0f, nameTextY, menuWidth - 24.0f, nameTextScale);

            // IP and port on second line
            std::string serverAddr = s.ip + ":" + std::to_string(s.port);
            float addrTextScale = 0.75f;
            float addrTextY = serverY + 38.0f;
            renderer.drawTextFit(serverAddr.c_str(), menuX + 12.0f, addrTextY, menuWidth - 24.0f, addrTextScale);

            // Selection indicator
            if (serverConnectSelection == 2 + i) {
                glm::mat4 indicatorModel = glm::mat4(1.0f);
                indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 4.0f, serverY + serverBoxHeight/2.0f, 0.0f));
                indicatorModel = glm::scale(indicatorModel, glm::vec3(6.0f, 6.0f, 1.0f));
                renderer.drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
            }
        }
    }

    // ---- Connection status display (not a button) ----
    float serverSpacing = 70.0f;
    int numServers = std::min(3, (int)discoveredServers.size());
    int backRow = discoveredServers.empty() ? 2 : 2 + numServers;
    float statusY = startY + (discoveredServers.empty() ? 2.5f * spacing : 2.5f * spacing + numServers * serverSpacing);

    const char* statusText = "Not Connected";
    glm::vec3 statusColor = glm::vec3(0.5f, 0.5f, 0.5f); // Gray
    if (isConnectedToServer()) {
        statusText = "Connected";
        statusColor = glm::vec3(0.2f, 0.8f, 0.2f); // Green
    } else if (networkClient && networkClient->isConnecting()) {
        statusText = "Connecting...";
        statusColor = glm::vec3(0.8f, 0.8f, 0.2f); // Yellow
    }

    // Draw status text (no box, just text)
    float statusTextScale = 0.8f;
    renderer.drawTextFit(statusText, menuX, statusY + menuHeight/2.0f - 6.0f, menuWidth, statusTextScale);

    // ---- Back button ----
    float backY = statusY + spacing;
    glm::mat4 backBox = glm::mat4(1.0f);
    backBox = glm::translate(backBox, glm::vec3(menuX + menuWidth/2.0f, backY + menuHeight/2.0f, 0.0f));
    backBox = glm::scale(backBox, glm::vec3(menuWidth, menuHeight, 1.0f));

    // Green if selected, blue otherwise
    glm::vec3 backColor = (serverConnectSelection == backRow)
        ? glm::vec3(0.6f, 0.8f, 0.2f)
        : glm::vec3(0.3f, 0.5f, 0.7f);
    renderer.drawColoredQuad(backBox, backColor);

    // Draw text centered in box
    float rowTextScale = std::max(0.65f, std::min(0.82f, 1.0f));
    float backTextH = 5.0f * 8.0f * rowTextScale;
    float backTextY = backY + (menuHeight - backTextH) * 0.5f;
    renderer.drawTextFit("BACK", menuX + 12.0f, backTextY, menuWidth - 24.0f, rowTextScale);

    // Selection indicator for back button
    if (serverConnectSelection == backRow) {
        glm::mat4 indicatorModel = glm::mat4(1.0f);
        indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 4.0f, backY + menuHeight/2.0f, 0.0f));
        indicatorModel = glm::scale(indicatorModel, glm::vec3(6.0f, 6.0f, 1.0f));
        renderer.drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
    }

    // Instructions at bottom — drawTextFit ensures it never overflows on narrow screens
    renderer.drawTextFit("UP/DOWN=navigate, ENTER=select, TAB=input, R=rescan, ESC=back",
                         left + 20.0f, bottom - 18.0f, _b.width() - 40.0f, 0.9f);

    // Only print to console when selection actually changes (to reduce spam)
    static int lastServerConnectSelection = -1;
    if (serverConnectSelection != lastServerConnectSelection) {
        std::cout << "\n=== SERVER CONNECTION ===" << std::endl;
        std::cout << (serverConnectSelection == 0 ? "> " : "  ")
                  << "IP: " << (serverAddress.empty() ? "192.168.1.50" : serverAddress) << std::endl;
        std::cout << (serverConnectSelection == 1 ? "> " : "  ")
                  << "Port: " << serverPort << std::endl;

        if (!discoveredServers.empty()) {
            int maxRows = std::min(3, (int)discoveredServers.size());
            for (int i = 0; i < maxRows; i++) {
                const auto& s = discoveredServers[i];
                std::cout << (serverConnectSelection == 2 + i ? "> " : "  ")
                          << s.name << " - " << s.ip << ":" << s.port << std::endl;
            }
        } else {
            std::cout << "  No servers found. Press R to rescan." << std::endl;
        }

        // Connection status (not selectable)
        if (isConnectedToServer()) {
            std::cout << "  Connected" << std::endl;
        } else if (networkClient && networkClient->isConnecting()) {
            std::cout << "  Connecting..." << std::endl;
        } else {
            std::cout << "  Not Connected" << std::endl;
        }

        int backRow = discoveredServers.empty() ? 2 : 2 + std::min(3, (int)discoveredServers.size());
        std::cout << (serverConnectSelection == backRow ? "> " : "  ") << "BACK" << std::endl;
        std::cout << "Use UP/DOWN=navigate, ENTER=select, TAB=input, R=rescan, ESC=back" << std::endl;
        lastServerConnectSelection = serverConnectSelection;
    }
}

void Game::processServerConnectInput() {
    // Note: ALL static bool key-state variables have been moved to inp (InputState member).
    // Using inp.tabPressed, inp.enterPressed, inp.escapePressed, inp.backspacePressed,
    // inp.periodPressed, inp.rPressed, inp.upPressed, inp.downPressed, inp.digitPressed[].

    // Keep LAN discovery socket alive and poll for responses
    discoveryTimeSeconds += 0.016f; // best-effort (menu doesn't have deltaTime)
    if (discoverySendCooldown > 0.0f) discoverySendCooldown -= 0.016f;
    ensureDiscoverySocket(discoverySocketFd);

    // Poll discovery responses (non-blocking)
    if (discoverySocketFd != SOCKET_INVALID) {
        char buffer[MAX_PACKET_SIZE];
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        while (true) {
#ifdef _WIN32
            int bytes = recvfrom(discoverySocketFd, (char*)buffer, sizeof(buffer), 0, (sockaddr*)&from, &fromLen);
#else
            ssize_t bytes = recvfrom(discoverySocketFd, buffer, sizeof(buffer), 0, (sockaddr*)&from, &fromLen);
#endif
            if (bytes <= 0) break;
            if ((size_t)bytes < sizeof(PacketHeader)) continue;
            auto* header = (PacketHeader*)buffer;
            if (header->magic != PACKET_MAGIC) continue;
            if (header->type != PACKET_DISCOVER_RESPONSE) continue;
            if ((size_t)bytes < sizeof(DiscoverResponsePacket)) continue;
            auto* resp = (DiscoverResponsePacket*)buffer;

            std::string ip   = inet_ntoa(from.sin_addr);
            uint16_t port    = resp->port;
            std::string name(resp->name);
            if (name.empty()) name = "Server";
            std::string key = ip + ":" + std::to_string(port);

            auto it = discoveredIndexByKey.find(key);
            if (it == discoveredIndexByKey.end()) {
                DiscoveredServer s;
                s.ip              = ip;
                s.port            = port;
                s.name            = name;
                s.lastSeenSeconds = discoveryTimeSeconds;
                discoveredIndexByKey[key] = discoveredServers.size();
                discoveredServers.push_back(s);
            } else {
                auto& s           = discoveredServers[it->second];
                s.lastSeenSeconds = discoveryTimeSeconds;
                s.name            = name;
            }
        }
    }

    // Guard: skip all input on the first frame after entering this screen so a
    // still-held ENTER from the main menu doesn't fire immediately.
    if (menuJustEntered) {
        inp.resetNavKeys();
        menuJustEntered = false;
        return;
    }

    // Calculate max row based on discovered servers
    int numServerRows = discoveredServers.empty() ? 0 : std::min(3, (int)discoveredServers.size());
    int backRow       = discoveredServers.empty() ? 2 : 2 + numServerRows;

    // Rows 0 (IP) and 1 (Port) are text-input fields, not navigable buttons.
    // Clamp selection to the button range [2 .. backRow].
    if (serverConnectSelection < 2) serverConnectSelection = 2;
    if (serverConnectSelection > backRow) serverConnectSelection = backRow;

    // UP/DOWN navigate only through button rows (2 .. backRow), skipping input fields.
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !inp.upPressed) {
        inp.upPressed = true;
        int range = backRow - 2 + 1; // number of button rows
        if (range > 0) {
            serverConnectSelection = 2 + (serverConnectSelection - 2 - 1 + range) % range;
        }
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        inp.upPressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !inp.downPressed) {
        inp.downPressed = true;
        int range = backRow - 2 + 1;
        if (range > 0) {
            serverConnectSelection = 2 + (serverConnectSelection - 2 + 1) % range;
        }
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        inp.downPressed = false;
    }

    // TAB still switches between the two input fields (rows 0 and 1)
    enteringServerAddress = (serverConnectSelection == 0);

    // TAB toggles between the IP (row 0) and Port (row 1) input fields.
    if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS && !inp.tabPressed) {
        inp.tabPressed = true;
        // Jump into IP field or cycle between the two input fields
        if (serverConnectSelection >= 2) {
            // First TAB press from a button row: enter IP field
            serverConnectSelection = 0;
        } else if (serverConnectSelection == 0) {
            serverConnectSelection = 1;
        } else {
            // From Port back to first button row
            serverConnectSelection = 2;
        }
        enteringServerAddress = (serverConnectSelection == 0);
    } else if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_RELEASE) {
        inp.tabPressed = false;
    }

    // Auto-scan while on this screen (lightweight)
    if (discoverySocketFd != SOCKET_INVALID && discoverySendCooldown <= 0.0f) {
        uint16_t port = serverPort == 0 ? DEFAULT_PORT : serverPort;
        sendDiscoverPing(discoverySocketFd, port);
        discoverySendCooldown = 1.0f;
    }

    // Rescan LAN (R) - works on any row
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !inp.rPressed) {
        inp.rPressed = true;
        uint16_t port = serverPort == 0 ? DEFAULT_PORT : serverPort;
        sendDiscoverPing(discoverySocketFd, port);
        discoverySendCooldown = 0.75f;
    } else if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) {
        inp.rPressed = false;
    }

    // Handle number keys for IP/port input (only when on address or port row via TAB)
    if (serverConnectSelection == 0 || serverConnectSelection == 1) {
        for (int key = GLFW_KEY_0; key <= GLFW_KEY_9; key++) {
            int idx = key - GLFW_KEY_0;
            if (glfwGetKey(window, key) == GLFW_PRESS && !inp.digitPressed[idx]) {
                inp.digitPressed[idx] = true;
                char digit = '0' + (key - GLFW_KEY_0);
                if (serverConnectSelection == 0) {
                    serverAddress += digit;
                } else if (serverConnectSelection == 1) {
                    if (serverPort < 65535) {
                        serverPort = serverPort * 10 + (digit - '0');
                    }
                }
            } else if (glfwGetKey(window, key) == GLFW_RELEASE) {
                inp.digitPressed[idx] = false;
            }
        }

        // Handle period for IP address
        if (serverConnectSelection == 0 && glfwGetKey(window, GLFW_KEY_PERIOD) == GLFW_PRESS && !inp.periodPressed) {
            inp.periodPressed = true;
            serverAddress += '.';
        } else if (glfwGetKey(window, GLFW_KEY_PERIOD) == GLFW_RELEASE) {
            inp.periodPressed = false;
        }

        // Also support keypad decimal for IP entry
        if (serverConnectSelection == 0 && glfwGetKey(window, GLFW_KEY_KP_DECIMAL) == GLFW_PRESS && !inp.periodPressed) {
            inp.periodPressed = true;
            serverAddress += '.';
        } else if (glfwGetKey(window, GLFW_KEY_KP_DECIMAL) == GLFW_RELEASE) {
            inp.periodPressed = false;
        }

        // Handle backspace
        if (glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS && !inp.backspacePressed) {
            inp.backspacePressed = true;
            if (serverConnectSelection == 0) {
                if (!serverAddress.empty()) {
                    serverAddress.pop_back();
                }
            } else if (serverConnectSelection == 1) {
                serverPort = serverPort / 10;
            }
        } else if (glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_RELEASE) {
            inp.backspacePressed = false;
        }
    }

    // Handle ENTER to select server or go back
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !inp.enterPressed) {
        inp.enterPressed = true;
        if (serverConnectSelection >= 2 && serverConnectSelection < backRow) {
            // Selecting a discovered server
            int serverIndex = serverConnectSelection - 2;
            if (serverIndex < (int)discoveredServers.size()) {
                const auto& s = discoveredServers[serverIndex];
                serverAddress = s.ip;
                serverPort    = s.port;
                // Try to connect immediately
                if (connectToServer(serverAddress, serverPort)) {
                    std::cout << "Connected to discovered server!" << std::endl;
                }
            }
        } else if (serverConnectSelection == backRow) {
            // Back button: always return to "Connect to Server" item in current menu layout
            inp.resetNavKeys();
            menuJustEntered = true;
            gameState = MENU;
            menuSelection = isConnectedToServer() ? 2 : 3;
        }
    } else if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
        inp.enterPressed = false;
    }

    // Explicit ESC handling for server menu to avoid state getting stuck
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !inp.escapePressed) {
        inp.escapePressed = true;
        inp.resetNavKeys();
        menuJustEntered = true;
        gameState = MENU;
        menuSelection = isConnectedToServer() ? 2 : 3;
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
        inp.escapePressed = false;
    }
}