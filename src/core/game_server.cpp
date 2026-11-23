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

    // A network match uses other players/bots, not an offline bot host.
    localBotsPending_ = false;
    stopLocalBots();

    networkClient = new NetworkClient();

    // Set up callbacks
    networkClient->onPlayerState = [this](const PlayerStatePacket& packet) {
        // PlayerState is sent over UDP: ignore a datagram older than the last
        // one applied for this player. A delayed death state arriving after a
        // respawn used to kill the player a second time (extra death sound).
        if (networkClient && packet.header.sequence != 0) {
            if (packet.playerId == networkClient->getClientId()) {
                if (packet.header.sequence <= lastLocalStateSequence_) return;
                lastLocalStateSequence_ = packet.header.sequence;
            } else {
                for (int i = 0; i < MAX_REMOTE_PLAYERS; ++i) {
                    if (remotePlayers[i].active &&
                        remotePlayers[i].id == packet.playerId &&
                        packet.header.sequence <= remotePlayers[i].stateSequence) {
                        return;
                    }
                }
            }
        }

        // Only sync health/death state for local player (client controls own movement)
        if (networkClient && packet.playerId == networkClient->getClientId()) {
            // The server assigns teams (balanced on connect); keep the local
            // team in sync so spawns and remote colours agree everywhere.
            if (packet.team <= 1) playerTeam = packet.team;

            // Don't override local position/velocity - client controls own movement
            // Only sync health and death state from server.
            //
            // The server is authoritative on death: when it reports health 0 /
            // isDead the player dies where they stand (after a hit), and the
            // respawned state it sends a few seconds later brings them back.
            if (packet.isDead || packet.health <= 0.0f) {
                // The server is authoritative on death. Do not ignore this
                // because of the local spawn-protection timer: if the server
                // considers us dead, staying alive locally would freeze our
                // body on everyone else's screen (and the server would never
                // accept our movement again until the next round).
                if (playerHealth > 0.0f) playerHealth = 0.0f;
                if (!isDead) dieLocally();
            } else if (isDead) {
                // The server put us back on our feet (respawn). In a menu the
                // player keeps browsing; the respawn waits for the return to
                // the round (see resumeGameFromMenu).
                if (isInMatchMenu()) {
                    respawnPending_ = true;
                    if (bombDefusalMode) {
                        // A revival in bomb defusal belongs to a new round;
                        // its BombState carries the actual reset.
                        pendingRoundStart_ = true;
                        pendingRoundStartFromServer_ = true;
                    }
                } else if (bombDefusalMode) {
                    // Round-based: the new BombState performs the reset. Doing
                    // it here as well was the second respawn after a round.
                    respawnPending_ = true;
                } else {
                    respawnLocally();
                }
            } else {
                // The server's health is authoritative. Do not subtract the
                // damage we reported to *other* players here: those books
                // belong to their health, and deducting them from our own
                // made clients die at different moments.
                playerHealth = std::max(0.0f, packet.health);
            }
            return;
        }

        // Update remote player state (skip own player)
        if (packet.playerId != networkClient->getClientId()) {
            for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                if (!remotePlayers[i].active || remotePlayers[i].id == packet.playerId) {
                    // A (re)appearing player starts its bookkeeping clean.
                    const bool isNew = !remotePlayers[i].active ||
                                       remotePlayers[i].id != packet.playerId;
                    const float oldHealth = isNew ? packet.health
                                                  : remotePlayers[i].health;
                    const bool respawned = !isNew && remotePlayers[i].isDead &&
                                           !packet.isDead;
                    // A respawn (or any health increase) invalidates pending
                    // damage reports. Resending old damage here could kill the
                    // just-respawned player a second time from one shot.
                    if (isNew || respawned || (!isNew && packet.health > oldHealth + 0.5f)) {
                        pvpRemotes_[i] = PvpRemote{};
                    }
                    if (isNew) {
                        remotePlayers[i].anim = humanoid::Anim{};
                    }
                    remotePlayers[i].id       = packet.playerId;
                    remotePlayers[i].position = glm::vec3(packet.posX, packet.posY, packet.posZ);
                    remotePlayers[i].velocity = glm::vec3(packet.velX, packet.velY, packet.velZ);
                    remotePlayers[i].yaw      = packet.yaw;
                    remotePlayers[i].pitch    = packet.pitch;
                    remotePlayers[i].health   = packet.health;
                    remotePlayers[i].isDead   = packet.isDead;
                    remotePlayers[i].active   = true;
                    remotePlayers[i].team     = packet.team;
                    remotePlayers[i].stance   = packet.stance <= 2 ? packet.stance : 0;
                    remotePlayers[i].weaponId = packet.weaponId < 6 ? packet.weaponId : 0;
                    remotePlayers[i].stateSequence = packet.header.sequence;
                    // A health drop caused by anyone's shot flinches the body.
                    if (!isNew && packet.health < oldHealth - 0.5f) {
                        remotePlayers[i].anim.damage = 1.0f;
                    }
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
                remotePlayers[i].team           = packet.team;
                remotePlayers[i].stance         = 0;
                remotePlayers[i].weaponId       = 0;
                remotePlayers[i].stateSequence  = 0;
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
                remotePlayers[i].team           = packet.team;
                remotePlayers[i].stance         = 0;
                remotePlayers[i].weaponId       = 0;
                remotePlayers[i].stateSequence  = 0;
                remotePlayers[i].anim           = humanoid::Anim{};
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
            // Show the owner firing: their next body frame raises both arms.
            // A shotgun reports one packet per pellet; only the first one in a
            // burst should restart the pose and play the shot sound.
            for (int i = 0; i < MAX_REMOTE_PLAYERS; ++i) {
                if (!remotePlayers[i].active || remotePlayers[i].id != packet.ownerId) continue;
                const bool newShot = remotePlayers[i].anim.shoot < 0.5f;
                remotePlayers[i].anim.shoot = 1.0f;
                if (newShot && (packet.projectileType == 0 ||
                                packet.projectileType == 3 ||
                                packet.projectileType == 4)) {
                    const char* sound = packet.projectileType == 3 ? "shotgun_fire"
                                      : packet.projectileType == 4 ? "sniper_fire"
                                                                   : "pistol_fire";
                    playSoundEffect(
                        sound,
                        glm::vec3(packet.posX, packet.posY, packet.posZ), 0.8f);
                }
                break;
            }
        }
    };

    networkClient->onLobbyState = [this](const LobbyStatePacket& packet) {
        handleLobbyState(packet);
    };

    networkClient->onLobbyStart = [this](const LobbyStartPacket& packet) {
        handleLobbyStart(packet);
    };

    networkClient->onMatchState = [this](const MatchStatePacket& packet) {
        if (packet.gameMode == TEAM_VS_TEAM) {
            teamAScore_ = packet.teamAScore;
            teamBScore_ = packet.teamBScore;
            checkMatchEnd();
        }
    };

    networkClient->onEconomyState = [this](const EconomyStatePacket& packet) {
        // Economic packets own money/loadout; free-mode packets still sync the
        // free armor the server accepted (all weapons stay free there).
        if (packet.economic != 0) {
            const bool hadHE = hasHEGrenade_;
            const bool hadFlash = hasFlashbang_;
            money_ = std::min<int>(economy::kMaxMoney, packet.money);
            hasPrimaryShotgun_ = packet.primary == 1;
            hasPrimarySniper_ = packet.primary == 2;
            hasHEGrenade_ = (packet.grenades & 1u) != 0;
            hasFlashbang_ = (packet.grenades & 2u) != 0;
            // Newly bought grenades get one round; an already-owned grenade
            // keeps its current count (round-1 freebies start full).
            if (grenade) {
                if (hasHEGrenade_ && (!hadHE || grenade->getAmmo() <= 0)) grenade->setAmmo(1);
                if (!hasHEGrenade_) grenade->setAmmo(0);
            }
            if (flashbang) {
                if (hasFlashbang_ && (!hadFlash || flashbang->getAmmo() <= 0)) flashbang->setAmmo(1);
                if (!hasFlashbang_) flashbang->setAmmo(0);
            }
            grenadeCount = grenade ? grenade->getAmmo() : 0;
            flashbangCount = flashbang ? flashbang->getAmmo() : 0;
        } else {
            money_ = 0;
        }
        armor_ = std::min<int>(100, packet.armor);
        defuseKit_ = packet.defuseKit != 0;
        if (!weaponOwned(currentWeapon)) {
            currentWeapon = PISTOL;
            currentWeaponInstance = pistol;
        }
        if (gameState == BUY_MENU) refreshBuyMenuRows();
    };

    networkClient->onBombState = [this](const BombStatePacket& packet) {
        if (!bombDefusalMode) return;
        // A map is still loading: packets from the map we are leaving must not
        // reconfigure (or respawn onto) the current one. The next state after
        // installMap() is the fresh map's first round.
        if (mapLoader_.isLoading()) return;

        // Ignore packets from an older round. UDP can deliver a delayed
        // "round ended" datagram after the client already started the next
        // round, which used to end that fresh round immediately (the next
        // round appeared twice in a row).
        if (packet.roundId < serverBombRoundId_) return;
        const uint32_t previousRoundId = serverBombRoundId_;
        const bool roundChanged = packet.roundId != previousRoundId;
        if (roundChanged) {
            serverBombRoundId_ = packet.roundId;
            serverRoundEnded = false;
            roundWinner = -1;
            roundEndReason = ROUND_END_NONE;
            buyWindowClosed_ = false;
        }

        // The server owns the bomb. Keep the local HUD/render state in sync;
        // planting, defusing and the fuse are advanced server-side.
        bomb.position = glm::vec3(packet.posX, packet.posY, packet.posZ);
        bomb.planted = packet.planted != 0;
        bomb.defused = packet.defused != 0;
        bomb.active = bomb.planted && !bomb.defused && packet.exploded == 0;
        bomb.plantingTeam = packet.plantingTeam <= 1 ? packet.plantingTeam : bomb.plantingTeam;
        bomb.plantTime = bomb.planted
                             ? std::max(0.0f, bomb.explodeTime - packet.timeLeft)
                             : 0.0f;
        bomb.defuseProgress = packet.defuseProgress;
        roundTimeLeft = packet.roundTimeLeft;
        roundAge_ = packet.roundAge;
        if (packet.freezeTime > 0.0f) freezeTime_ = packet.freezeTime;

        if (packet.roundActive) roundActive = true;

        // Progress bars and the crouched/locked interaction pose belong only
        // to the client that is actually doing the planting/defusing. The
        // progress values are map-global (they also advance for bots), so
        // using them directly made every player sneak in place whenever a
        // bot somewhere on the map was planting.
        const uint32_t selfId = networkClient ? networkClient->getClientId() : 0;
        const bool iAmPlanting = packet.plantActor != 0 && packet.plantActor == selfId;
        const bool iAmDefusing = packet.defuseActor != 0 && packet.defuseActor == selfId;
        serverPlantActor_ = packet.plantActor;
        serverDefuseActor_ = packet.defuseActor;

        if (packet.plantActor != 0) {
            isPlanting = iAmPlanting;
            plantProgress = iAmPlanting ? packet.plantProgress : 0.0f;
        } else if (packet.planted != 0 || packet.roundEnded != 0 || packet.exploded != 0) {
            isPlanting = false;
            plantProgress = 0.0f;
        }
        // actor == 0: no server-side actor yet. Keep the local request that
        // processGameInput() started, so it is not restarted (and re-logged)
        // on every incoming packet before the server picks an actor.

        if (packet.defuseActor != 0) {
            isDefusing = iAmDefusing;
            defuseProgress = iAmDefusing ? packet.defuseProgress : 0.0f;
        } else if (packet.defused != 0 || packet.roundEnded != 0 || packet.exploded != 0) {
            isDefusing = false;
            defuseProgress = 0.0f;
        }

        if (packet.roundActive != 0 && packet.roundEnded == 0) {
            // New/reset round: re-arm the one-shot round-result guard.
            serverRoundEnded = false;
            roundWinner = -1;
            roundEndReason = ROUND_END_NONE;
        }

        // The server is authoritative for round wins: bomb explosion/defuse,
        // team elimination, or the plant-time running out. Apply each result
        // once, no matter how many periodic state packets repeat it.
        if (packet.roundEnded != 0 && !serverRoundEnded) {
            serverRoundEnded = true;
            roundWinner = packet.roundWinner <= 1 ? packet.roundWinner : -1;
            roundEndReason = static_cast<RoundEndReason>(packet.roundEndReason);
            roundActive = false;
            endRound(roundWinner, roundEndReason);
        }

        // The server already swapped the attacking team for the next round in
        // the same packet that ended this one. Apply it *after* endRound()
        // (which swaps locally) so the two do not cancel each other out.
        if (packet.attackingTeam <= 1) attackingTeam = packet.attackingTeam;

        // The server started the next round by itself. Take the new life now;
        // this is also the recovery path for a client that missed the round
        // result and would otherwise stay a spectator forever. The very first
        // round after loading a map is already covered by resetGame(), so it
        // is not treated as a transition (previousRoundId == 0).
        if (roundChanged && previousRoundId != 0 &&
            packet.roundActive != 0 && packet.roundEnded == 0 && !matchOver_) {
            if (isInMatchMenu()) {
                // Leave the menu alone; the reset happens on return.
                pendingRoundStart_ = true;
                pendingRoundStartFromServer_ = true;
            } else {
                beginServerRound();
            }
        }
    };

    networkClient->onMapPlayerCount = [this](const MapPlayerCountPacket& packet) {
        // Update map player counts. Use the dynamic map list so newly added
        // maps (and custom.map) are shown too, not just a fixed six-map set.
        for (size_t i = 0; i < mapPlayerCounts.size(); ++i) {
            mapPlayerCounts[i] = 0;
        }
        for (int i = 0; i < packet.mapCount && i < 8; ++i) {
            const std::string mapName(packet.maps[i].mapName);
            for (size_t j = 0; j < mapFiles.size(); ++j) {
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
    localBotsPending_ = false;
    stopLocalBots();
    if (networkClient) {
        networkClient->disconnect();
        delete networkClient;
        networkClient = nullptr;
    }
    multiplayerMode = false;
    playerCount = 0;
    lobbyReady_ = false;
    lobbyViews_.clear();
    serverRoundEnded = false;
    serverBombRoundId_ = 0;
    lastLocalStateSequence_ = 0;
    roundWinner = -1;
    roundEndReason = ROUND_END_NONE;
    roundTimeLeft = 0.0f;
    serverPlantActor_ = 0;
    serverDefuseActor_ = 0;

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
    for (size_t i = 0; i < mapPlayerCounts.size(); ++i) {
        mapPlayerCounts[i] = 0;
    }
}

const LobbyView* Game::findLobby(const std::string& mapFile) const {
    auto it = lobbyViews_.find(mapFile);
    return it == lobbyViews_.end() ? nullptr : &it->second;
}

void Game::handleLobbyState(const LobbyStatePacket& packet) {
    const std::string mapName = packet.mapName[0] != '\0'
                                    ? std::string(packet.mapName)
                                    : std::string("simple.map");

    LobbyView view;
    view.mapName = mapName;
    view.gameMode = std::min<int>(2, packet.gameMode);
    view.matchFormat = std::min<int>(2, packet.matchFormat);
    view.botPool = packet.botPool;
    view.botsTeamA = packet.botsTeamA;
    view.botsTeamB = packet.botsTeamB;
    view.botAccuracy = packet.botAccuracy;
    view.botMaxAccuracy = packet.botMaxAccuracy > 0 ? packet.botMaxAccuracy : 100;
    view.players.reserve(std::min<size_t>(packet.playerCount, MAX_LOBBY_PLAYERS));
    for (size_t i = 0; i < packet.playerCount && i < MAX_LOBBY_PLAYERS; ++i) {
        const LobbyPlayerEntry& entry = packet.players[i];
        LobbyPlayerView player;
        player.id = entry.id;
        player.name = entry.name;
        if (player.name.empty()) player.name = "player" + std::to_string(entry.id);
        player.team = static_cast<uint8_t>(entry.team & 1u);
        player.ready = entry.ready != 0;
        if (player.ready) ++view.readyCount;
        view.players.push_back(std::move(player));
    }

    if (view.players.empty()) lobbyViews_.erase(mapName);
    else                      lobbyViews_[mapName] = std::move(view);

    // The server is authoritative for our own ready flag as well.
    if (networkClient) {
        const uint32_t selfId = networkClient->getClientId();
        const LobbyView* stored = findLobby(mapName);
        if (stored) {
            for (const LobbyPlayerView& player : stored->players) {
                if (player.id == selfId) {
                    lobbyReady_ = player.ready;
                    break;
                }
            }
        }
    }

    if (gameState == TEAM_SETUP && mapName == pendingMapFile_) {
        applyLobbyState(static_cast<uint8_t>(playerTeam), packet.botPool,
                        packet.botsTeamA, packet.botsTeamB, packet.botAccuracy,
                        packet.botMaxAccuracy, packet.matchFormat);
    }
    lobbyDirty_ = true;
}

void Game::handleLobbyStart(const LobbyStartPacket& packet) {
    if (gameState != TEAM_SETUP || !networkClient) return;

    const std::string mapName = packet.mapName[0] != '\0'
                                    ? std::string(packet.mapName)
                                    : pendingMapFile_;
    if (!pendingMapFile_.empty() && mapName != pendingMapFile_) return;

    const uint32_t selfId = networkClient->getClientId();
    for (size_t i = 0; i < packet.playerCount && i < MAX_LOBBY_PLAYERS; ++i) {
        if (packet.players[i].id == selfId) {
            playerTeam = packet.players[i].team & 1u;
            break;
        }
    }

    currentGameMode = static_cast<GameMode>(std::min<int>(2, packet.gameMode));
    matchFormat_ = static_cast<MatchFormat>(std::min<int>(2, packet.matchFormat));
    botSetupTeamA_ = packet.botsTeamA;
    botSetupTeamB_ = packet.botsTeamB;
    botSetupBots_ = botSetupTeamA_ + botSetupTeamB_;
    if (packet.botAccuracy > 0) botSetupAccuracy_ = packet.botAccuracy;
    lobbyReady_ = false;
    pendingMapFile_ = mapName;

    std::cout << "[Lobby] All players ready - starting " << mapName << std::endl;
    startMatchNow(mapName);
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

    // The server assigned our team in the handshake (it balances players
    // across both teams, so two clients spawn on opposite sides).
    playerTeam = networkClient->getTeam() & 1u;

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
