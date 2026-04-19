/**
 * @file game.cpp
 * @brief Core Game lifecycle: constructor, destructor, init, map loading,
 *        resetGame, key-binding init, and GLFW callbacks.
 *        All other methods live in the split files:
 *          game_input.cpp   – processInput, processGameInput
 *          game_loop.cpp    – update, render, cleanup, shouldClose
 *          game_gameplay.cpp– physics, weapons, projectiles, enemies
 *          game_hud.cpp     – crosshair, damage/flashbang overlays, death screen
 *          game_menus.cpp   – menu / settings / map-selection render+input
 *          game_server.cpp  – server connect render+input, connectToServer, disconnect
 *          game_editor.cpp  – map editor render+input, saveMap
 */

#include "game.hpp"
#include "network/network.hpp"
#include <iostream>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <algorithm>
#include <cctype>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Game::Game(int width, int height)
    : currentMap(nullptr),
      window(nullptr),
      camera(glm::vec3(0.0f, 0.0f, 3.0f)),
      currentWeaponInstance(nullptr),
      pistol(nullptr),
      shotgun(nullptr),
      grenade(nullptr),
      flashbang(nullptr),
      physics(),
      playerPos(glm::vec3(0.0f, 2.0f, 0.0f)),
      playerVelocity(glm::vec3(0.0f, 0.0f, 0.0f)),
      isJumping(false),
      jumpCooldown(0.0f),
      score(0),
      lastFrame(0.0f),
      firstMouse(true),
      lastX(400.0f),
      lastY(300.0f),
      gameState(MENU),
      currentWeapon(PISTOL),
      menuSelection(0),
      settingsSelection(0),
      isDead(false),
      mapSelection(0),
      gameModeSelection(0),
      menuJustEntered(true),
      inp(),
      keyBindings(),
      shootCooldown(0.0f),
      pistolCooldown(0.0f),
      shotgunCooldown(0.0f),
      pistolAmmo(1000),
      shotgunAmmo(25),
      grenadeCount(5),
      flashbangCount(3),
      grenadeCooldown(0.0f),
      flashbangCooldown(0.0f),
      flashEffectTime(0.0f),
      isFlashed(false),
      infiniteAmmoMode(false),
      currentGameMode(DEATHMATCH),
      playerTeam(0),
      playerHealth(100),
      playerMaxHealth(100),
      playerDamageCooldown(0.0f),
      damageEffectTime(0.0f),
      networkClient(nullptr),
      multiplayerMode(false),
      playerCount(0),
      serverAddress("127.0.0.1"),
      serverPort(7777),
      enteringServerAddress(false),
      serverConnectSelection(0),
      discoverySocketFd(SOCKET_INVALID),
      discoveryTimeSeconds(0.0f),
      discoverySendCooldown(0.0f),
      discoveredSelection(0),
      focusServerList(false),
      activeProjectiles(0),
      bulletHoleCount(0),
      activeEnemies(0)
{
    std::cout << "Game initializing..." << std::endl;

    // Remote players
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++)
        remotePlayers[i].active = false;

    initKeyBindings();

    // Projectiles
    for (int i = 0; i < MAX_PROJECTILES; i++)
        projectiles[i].active = false;

    // Bullet holes
    for (int i = 0; i < MAX_BULLET_HOLES; i++)
        bulletHoles[i].lifetime = 0.0f;

    // Weapons
    pistol    = new Pistol(infiniteAmmoMode);
    shotgun   = new PumpShotgun(infiniteAmmoMode);
    grenade   = new Grenade(3);
    flashbang = new Flashbang(2);
    currentWeaponInstance = pistol;

    // Physics
    physics.setPlayerPosition(playerPos);
    physics.setPlayerVelocity(playerVelocity);

    // Enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = false;
        enemies[i].alive  = false;
    }
    activeEnemies = 0;

    // ── GLFW / OpenGL window setup ──────────────────────────────────────────
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        exit(-1);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

    window = glfwCreateWindow(width, height, "Cube CS Lite", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        exit(-1);
    }
    glfwMakeContextCurrent(window);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        exit(-1);
    }

    std::cout << "Game constructor complete." << std::endl;
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
Game::~Game() {
    disconnectFromServer();
    if (discoverySocketFd != SOCKET_INVALID) {
        CLOSE_SOCKET(discoverySocketFd);
        discoverySocketFd = SOCKET_INVALID;
    }
    delete pistol;
    delete shotgun;
    delete grenade;
    delete flashbang;
    if (currentMap) delete currentMap;
    glfwTerminate();
}

// ---------------------------------------------------------------------------
// init – called after constructor; sets up renderer and spawns initial enemies
// ---------------------------------------------------------------------------
void Game::init() {
    std::cout << "Initializing renderer..." << std::endl;
    renderer.init();
    std::cout << "Renderer initialized." << std::endl;

    // Scan maps folder for available maps
    scanMapsFolder();

    if (!currentMap && !mapFiles.empty())
        loadMap(mapFiles[0]);

    playerVelocity = glm::vec3(0.0f);
    isJumping = false;

    spawnEnemy(glm::vec3( 5.0f, -1.0f,  5.0f));
    spawnEnemy(glm::vec3(-5.0f, -1.0f,  5.0f));
    spawnEnemy(glm::vec3( 5.0f, -1.0f, -5.0f));

    std::cout << "Game initialization complete." << std::endl;
}

// ---------------------------------------------------------------------------
// loadMap
// ---------------------------------------------------------------------------
bool Game::loadMap(const std::string& mapFile) {
    if (currentMap) delete currentMap;

    currentMap = new Map("assets/maps/" + mapFile);

    if (currentMap) {
        currentMap->loadTexture("grass.png",  0);
        currentMap->loadTexture("stone.png",  1);
        currentMap->loadTexture("brick.png",  2);
        currentMap->loadTexture("metal.png",  3);
        currentMap->loadTexture("wood.png",   4);
        currentMap->loadTexture("sand.png",   5);
        currentMap->loadTexture("ice.png",    6);
        currentMap->loadTexture("dirt.png",   7);
        resetGame();
    }

    return currentMap != nullptr;
}

Map* Game::getCurrentMap() {
    return currentMap;
}

// ---------------------------------------------------------------------------
// formatMapDisplayName – Convert filename to display name
// ---------------------------------------------------------------------------
std::string Game::formatMapDisplayName(const std::string& filename) {
    // Remove .map extension
    std::string name = filename;
    size_t extPos = name.find(".map");
    if (extPos != std::string::npos) {
        name = name.substr(0, extPos);
    }

    // Replace underscores with spaces
    std::replace(name.begin(), name.end(), '_', ' ');

    // Capitalize first letter of each word
    bool capitalizeNext = true;
    for (char& c : name) {
        if (capitalizeNext && c >= 'a' && c <= 'z') {
            c = std::toupper(c);
            capitalizeNext = false;
        } else if (c == ' ') {
            capitalizeNext = true;
        }
    }

    return name;
}

// ---------------------------------------------------------------------------
// scanMapsFolder – Scan assets/maps folder for .map files
// ---------------------------------------------------------------------------
void Game::scanMapsFolder() {
    mapFiles.clear();
    mapDisplayNames.clear();
    mapPlayerCounts.clear();

    std::string mapsPath = "assets/maps";

    try {
        if (std::filesystem::exists(mapsPath) && std::filesystem::is_directory(mapsPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(mapsPath)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    // Check if it's a .map file
                    if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".map") {
                        mapFiles.push_back(filename);
                        mapDisplayNames.push_back(formatMapDisplayName(filename));
                        mapPlayerCounts.push_back(0);
                    }
                }
            }
        }

        // Sort maps alphabetically by display name
        std::vector<size_t> indices(mapFiles.size());
        for (size_t i = 0; i < indices.size(); i++) indices[i] = i;
        std::sort(indices.begin(), indices.end(), [this](size_t a, size_t b) {
            return mapDisplayNames[a] < mapDisplayNames[b];
        });

        // Reorder vectors based on sorted indices
        std::vector<std::string> sortedFiles = mapFiles;
        std::vector<std::string> sortedNames = mapDisplayNames;
        std::vector<int> sortedCounts = mapPlayerCounts;

        for (size_t i = 0; i < indices.size(); i++) {
            mapFiles[i] = sortedFiles[indices[i]];
            mapDisplayNames[i] = sortedNames[indices[i]];
            mapPlayerCounts[i] = sortedCounts[indices[i]];
        }

        std::cout << "Loaded " << mapFiles.size() << " maps from " << mapsPath << std::endl;
        for (size_t i = 0; i < mapFiles.size(); i++) {
            std::cout << "  " << mapDisplayNames[i] << " (" << mapFiles[i] << ")" << std::endl;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error scanning maps folder: " << e.what() << std::endl;
        // Fallback to hardcoded maps if scanning fails
        mapFiles = {"simple.map", "large.map", "arena.map", "sniper.map", "complex.map", "team_arena.map"};
        mapDisplayNames = {"Simple", "Large", "Arena", "Sniper", "Complex", "Team Arena"};
        mapPlayerCounts = {0, 0, 0, 0, 0, 0};
    }
}



// ---------------------------------------------------------------------------
// resetGame – spawn player, reset health/ammo/weapons
// ---------------------------------------------------------------------------
void Game::resetGame() {
    playerHealth         = playerMaxHealth;
    playerDamageCooldown = 0.0f;
    damageEffectTime     = 0.0f;
    isDead               = false;

    // Spawn at map spawn point
    if (currentMap) {
        if (currentGameMode == TEAM_VS_TEAM) {
            TeamSpawn spawn = currentMap->getRandomTeamSpawn(playerTeam);
            playerPos    = spawn.position;
            camera.Yaw   = spawn.yaw;
            camera.Pitch = spawn.pitch;
        } else {
            SpawnPoint spawn = currentMap->getRandomSpawnPoint();
            playerPos    = spawn.position;
            camera.Yaw   = spawn.yaw;
            camera.Pitch = spawn.pitch;
        }
        // Fall back if spawn point is at origin
        if (playerPos == glm::vec3(0.0f)) {
            playerPos    = glm::vec3(0.0f, 2.0f, 0.0f);
            camera.Yaw   = 0.0f;
            camera.Pitch = 0.0f;
        }
        // Unstuck from geometry
        int safety = 0;
        while (currentMap->checkCollision(playerPos, 0.3f) && safety < 100) {
            playerPos.y += 1.0f;
            ++safety;
        }
    } else {
        playerPos    = glm::vec3(0.0f, 2.0f, 0.0f);
        camera.Yaw   = 0.0f;
        camera.Pitch = 0.0f;
    }

    playerVelocity = glm::vec3(0.0f);
    camera.Position = glm::vec3(playerPos.x, playerPos.y + 1.6f, playerPos.z);
    camera.updateCameraVectors();

    // Projectiles
    activeProjectiles = 0;
    for (int i = 0; i < MAX_PROJECTILES; i++)
        projectiles[i].active = false;

    // Bullet holes
    bulletHoleCount = 0;
    for (int i = 0; i < MAX_BULLET_HOLES; i++)
        bulletHoles[i].lifetime = 0.0f;

    // Enemies
    activeEnemies = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = false;
        enemies[i].alive  = false;
    }

    // Weapons
    currentWeapon    = PISTOL;
    currentWeaponInstance = pistol;
    pistolCooldown   = 0.0f;
    shotgunCooldown  = 0.0f;
    grenadeCooldown  = 0.0f;
    flashbangCooldown= 0.0f;

    // Ammo
    pistolAmmo    = 999;
    shotgunAmmo   = 24;
    grenadeCount  = 3;
    flashbangCount= 2;

    // Effects
    flashEffectTime = 0.0f;
    isFlashed       = false;

    // Re-spawn initial enemies
    spawnEnemy(glm::vec3( 5.0f, -1.0f,  5.0f));
    spawnEnemy(glm::vec3(-5.0f, -1.0f,  5.0f));
    spawnEnemy(glm::vec3( 5.0f, -1.0f, -5.0f));

    gameState = PLAYING;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    std::cout << "Game reset. HP=" << playerHealth << "/" << playerMaxHealth << std::endl;
}

// ---------------------------------------------------------------------------
// initKeyBindings
// ---------------------------------------------------------------------------
void Game::initKeyBindings() {
    keyBindings.forward  = GLFW_KEY_W;
    keyBindings.backward = GLFW_KEY_S;
    keyBindings.left     = GLFW_KEY_A;
    keyBindings.right    = GLFW_KEY_D;
    keyBindings.jump     = GLFW_KEY_SPACE;
    keyBindings.menu     = GLFW_KEY_ESCAPE;
    keyBindings.shoot    = GLFW_MOUSE_BUTTON_LEFT;
}

// ---------------------------------------------------------------------------
// GLFW callbacks
// ---------------------------------------------------------------------------
void Game::framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    if (width > 0 && height > 0)
        glViewport(0, 0, width, height);
}

void Game::mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    Game* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;
    if (game->firstMouse) {
        game->lastX      = (float)xpos;
        game->lastY      = (float)ypos;
        game->firstMouse = false;
    }
    float xoffset =  (float)xpos - game->lastX;
    float yoffset =  game->lastY - (float)ypos;
    game->lastX   = (float)xpos;
    game->lastY   = (float)ypos;
    // Only process mouse look while playing or in the editor
    if (game->gameState == PLAYING || game->gameState == MAP_EDITOR)
        game->camera.ProcessMouseMovement(xoffset, yoffset);
}

void Game::scrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
    Game* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (game)
        game->camera.ProcessMouseScroll((float)yoffset);
}