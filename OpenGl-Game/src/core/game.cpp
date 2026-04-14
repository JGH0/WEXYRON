/**
 * @file game.cpp
 * @brief Implementation of Game class for main game loop and state management
 * @author OpenGL Game Developer
 * @date 2025
 */

#include "game.hpp"
#include "network/network.hpp"
#include <iostream>
#include <random>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>

// UI helpers (defined later)
static void setVirtualUIProjection(Renderer& renderer, GLFWwindow* window);
static void getVirtualUIBounds(GLFWwindow* window, float& left, float& right, float& top, float& bottom);

Game::Game(int width, int height) 
    : camera(glm::vec3(0.0f, 0.0f, 3.0f)), 
      gameState(MENU),
      menuSelection(0),
      settingsSelection(0),
      currentWeapon(PISTOL),
      playerPos(glm::vec3(0.0f, 2.0f, 0.0f)),
      playerVelocity(glm::vec3(0.0f, 0.0f, 0.0f)),
      isJumping(false),
      jumpCooldown(0.0f),
      score(0),
      playerMaxHealth(100),
      playerHealth(100),
      playerDamageCooldown(0.0f),
      damageEffectTime(0.0f),
      isDead(false),
      pistolAmmo(1000),
      shotgunAmmo(25),
      pistolCooldown(0.0f),
      shotgunCooldown(0.0f),
      grenadeCooldown(0.0f),
      flashbangCooldown(0.0f),
      flashEffectTime(0.0f),
      isFlashed(false),
      infiniteAmmoMode(false),
      activeProjectiles(0),
      bulletHoleCount(0),
      currentMap(nullptr),
      currentWeaponInstance(nullptr),
      pistol(nullptr),
      shotgun(nullptr),
      grenade(nullptr),
      flashbang(nullptr),
      networkClient(nullptr),
      multiplayerMode(false),
      discoverySocketFd(SOCKET_INVALID),
      discoveryTimeSeconds(0.0f),
      discoverySendCooldown(0.0f),
      discoveredSelection(0),
      focusServerList(false) {
    std::cout << "Game initialized - Player at: (" << playerPos.x << ", " << playerPos.y << ", " << playerPos.z << ")" << std::endl;
    
    // Initialize remote players
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        remotePlayers[i].active = false;
    }
    
    // Initialize key bindings
    initKeyBindings();
    
    // Initialize projectiles
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectiles[i].active = false;
    }
    
    // Initialize bullet holes
    for (int i = 0; i < MAX_BULLET_HOLES; i++) {
        bulletHoles[i].lifetime = 0.0f;
    }
    
    // Initialize weapon system
    pistol = new Pistol(infiniteAmmoMode);
    shotgun = new PumpShotgun(infiniteAmmoMode);
    grenade = new Grenade(3);  // Fixed: use literal instead of undefined variable
    flashbang = new Flashbang(2);  // Fixed: use literal instead of undefined variable
    currentWeaponInstance = pistol;
    
    // Initialize physics system
    physics.setPlayerPosition(playerPos);
    physics.setPlayerVelocity(playerVelocity);
    
    // Initialize enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = false;
        enemies[i].alive = false;
    }
    
    // Initialize active enemies count
    activeEnemies = 0;
    
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        exit(-1);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

    window = glfwCreateWindow(width, height, "Simple 3D Game", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
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
}

Game::~Game() {
    disconnectFromServer();
    if (discoverySocketFd != SOCKET_INVALID) {
        CLOSE_SOCKET(discoverySocketFd);
        discoverySocketFd = SOCKET_INVALID;
    }
    if (currentMap) delete currentMap;
    glfwTerminate();
}

void Game::init() {
    std::cout << "Initializing renderer..." << std::endl;
    renderer.init();
    std::cout << "Renderer initialized." << std::endl;
    
    // Load default map
    if (!currentMap) {
        loadMap("simple.map");
    }
    
    // Initialize player physics
    playerVelocity = glm::vec3(0.0f);
    isJumping = false;
    
    // Spawn some initial enemies
    spawnEnemy(glm::vec3(5.0f, -1.0f, 5.0f));
    spawnEnemy(glm::vec3(-5.0f, -1.0f, 5.0f));
    spawnEnemy(glm::vec3(5.0f, -1.0f, -5.0f));
    
    std::cout << "Game initialization complete." << std::endl;
}

void Game::processInput(float deltaTime) {
    static bool escapePressed = false;
    static bool upPressed = false;
    static bool downPressed = false;
    static bool enterPressed = false;
    
    // Handle ESC key toggle
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !escapePressed) {
        escapePressed = true;
        if (gameState == PLAYING) {
            gameState = MENU;
            menuSelection = 0;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (gameState == MENU || gameState == SETTINGS || gameState == MAP_SELECTION || gameState == SERVER_CONNECT) {
            gameState = PLAYING;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
        escapePressed = false;
    }
    
    if (gameState == PLAYING) {
        processGameInput(deltaTime);
    } else if (gameState == MENU) {
        // Calculate number of menu items (Map Editor hidden when connected)
        int numItems = isConnectedToServer() ? 5 : 6;
        
        // Handle UP key
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !upPressed) {
            upPressed = true;
            menuSelection = (menuSelection - 1 + numItems) % numItems;
        } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
            upPressed = false;
        }
        
        // Handle DOWN key
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !downPressed) {
            downPressed = true;
            menuSelection = (menuSelection + 1) % numItems;
        } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
            downPressed = false;
        }
        
        // Handle ENTER key - map filtered index to actual action
        if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterPressed) {
            enterPressed = true;
            
            // Map the filtered menu selection to actual action
            // When connected: 0=Play, 1=Map Select, 2=Connect, 3=Settings, 4=Exit
            // When offline: 0=Play, 1=Map Select, 2=Editor, 3=Connect, 4=Settings, 5=Exit
            int actionIndex = menuSelection;
            if (isConnectedToServer() && menuSelection >= 2) {
                actionIndex = menuSelection + 1; // Skip Map Editor
            }
            
            switch (actionIndex) {
                case 0: // Play Game
                    gameState = PLAYING;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    break;
                case 1: // Map Selection
                    std::cout << "ENTERING MAP SELECTION MODE" << std::endl;
                    gameState = MAP_SELECTION;
                    break;
                case 2: // Map Editor
                    std::cout << "ENTERING MAP EDITOR" << std::endl;
                    gameState = MAP_EDITOR;
                    editorNoclip = true;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    break;
                case 3: // Connect to Server
                    std::cout << "ENTERING SERVER CONNECTION" << std::endl;
                    gameState = SERVER_CONNECT;
                    serverAddress = "";
                    serverPort = 7777;
                    enteringServerAddress = true;
                    serverConnectSelection = 0;
                    break;
                case 4: // Settings
                    gameState = SETTINGS;
                    settingsSelection = 0;
                    break;
                case 5: // Exit
                    glfwSetWindowShouldClose(window, true);
                    break;
            }
        } else if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
            enterPressed = false;
        }
    } else if (gameState == SETTINGS) {
        processSettingsInput();
    } else if (gameState == MAP_SELECTION) {
        processMapSelectionInput();
    } else if (gameState == SERVER_CONNECT) {
        processServerConnectInput();
    } else if (gameState == MAP_EDITOR) {
        processEditorInput();
    } else if (gameState == DEAD) {
        // Handle death screen input - any key to restart
        if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS || 
            glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS ||
            glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            // Reset game
            resetGame();
        }
    }
}

void Game::processGameInput(float deltaTime) {
    bool inputProcessed = false;
    
    // Unstuck key (U) - move player to safe center position
    if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS) {
        playerPos = glm::vec3(0.0f, 2.0f, 0.0f); // Reset to center well above floor
        playerVelocity = glm::vec3(0.0f, 0.0f, 0.0f); // Stop all movement
        camera.Position = glm::vec3(0.0f, 3.6f, 0.0f); // Reset camera
        inputProcessed = true;
    }
    
    // Movement using key bindings
    glm::vec3 movement(0.0f);
    float moveSpeed = 5.0f;
    
    if (glfwGetKey(window, keyBindings.forward) == GLFW_PRESS) {
        movement.x += cos(glm::radians(camera.Yaw));
        movement.z += sin(glm::radians(camera.Yaw));
        inputProcessed = true;
    }
    if (glfwGetKey(window, keyBindings.backward) == GLFW_PRESS) {
        movement.x -= cos(glm::radians(camera.Yaw));
        movement.z -= sin(glm::radians(camera.Yaw));
        inputProcessed = true;
    }
    if (glfwGetKey(window, keyBindings.left) == GLFW_PRESS) {
        movement.x += cos(glm::radians(camera.Yaw - 90.0f));
        movement.z += sin(glm::radians(camera.Yaw - 90.0f));
        inputProcessed = true;
    }
    if (glfwGetKey(window, keyBindings.right) == GLFW_PRESS) {
        movement.x += cos(glm::radians(camera.Yaw + 90.0f));
        movement.z += sin(glm::radians(camera.Yaw + 90.0f));
        inputProcessed = true;
    }
    
    // Apply movement to velocity
    if (inputProcessed) {
        playerVelocity.x = movement.x * moveSpeed;
        playerVelocity.z = movement.z * moveSpeed;
    } else {
        // Stop horizontal movement when no keys pressed
        playerVelocity.x = 0.0f;
        playerVelocity.z = 0.0f;
    }
    
    // Jump input
    if (glfwGetKey(window, keyBindings.jump) == GLFW_PRESS) {
        handleJump();
        inputProcessed = true;
    }
    
    // Shoot input (now uses mouse button)
    if (glfwGetMouseButton(window, keyBindings.shoot) == GLFW_PRESS) {
        // Only shoot locally if not connected to server
        if (!isConnectedToServer()) {
            shoot();
        }
        inputProcessed = true;
    }
    
    // Weapon switching input
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
        switchWeapon(1);
    }
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
        switchWeapon(2);
    }
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) {
        switchWeapon(3);
    }
    if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) {
        switchWeapon(4);
    }
    if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS) {
        // Toggle between grenade and flashbang
        if (currentWeapon == GRENADE) {
            currentWeapon = FLASHBANG;
            currentWeaponInstance = flashbang;
            std::cout << "Switched to Flashbang! Flashbangs left: " << (infiniteAmmoMode ? "∞" : std::to_string(flashbangCount)) << std::endl;
        } else if (currentWeapon == FLASHBANG) {
            currentWeapon = GRENADE;
            currentWeaponInstance = grenade;
            std::cout << "Switched to Grenade! Grenades left: " << (infiniteAmmoMode ? "∞" : std::to_string(grenadeCount)) << std::endl;
        } else {
            currentWeapon = GRENADE;
            currentWeaponInstance = grenade;
            std::cout << "Switched to Grenade! Grenades left: " << (infiniteAmmoMode ? "∞" : std::to_string(grenadeCount)) << std::endl;
        }
    }
}

void Game::update(float deltaTime) {
    if (gameState == PLAYING) {
        // Update player physics (always run, client controls own movement in multiplayer)
        updatePlayerPhysics(deltaTime);
        
        // Update network client
        if (networkClient && (networkClient->isConnected() || networkClient->isConnecting())) {
            networkClient->update();
            
            // Send player input to server
            if (networkClient->isConnected()) {
                float moveX = 0.0f, moveZ = 0.0f;
                if (glfwGetKey(window, keyBindings.forward) == GLFW_PRESS) {
                    moveX += cos(glm::radians(camera.Yaw));
                    moveZ += sin(glm::radians(camera.Yaw));
                }
                if (glfwGetKey(window, keyBindings.backward) == GLFW_PRESS) {
                    moveX -= cos(glm::radians(camera.Yaw));
                    moveZ -= sin(glm::radians(camera.Yaw));
                }
                if (glfwGetKey(window, keyBindings.left) == GLFW_PRESS) {
                    moveX += cos(glm::radians(camera.Yaw - 90.0f));
                    moveZ += sin(glm::radians(camera.Yaw - 90.0f));
                }
                if (glfwGetKey(window, keyBindings.right) == GLFW_PRESS) {
                    moveX += cos(glm::radians(camera.Yaw + 90.0f));
                    moveZ += sin(glm::radians(camera.Yaw + 90.0f));
                }
                
                bool jump = glfwGetKey(window, keyBindings.jump) == GLFW_PRESS;
                bool shoot = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                
                // Send eye-level position for projectile spawning (playerPos + eye height)
                glm::vec3 eyePos = playerPos + glm::vec3(0.0f, 1.6f, 0.0f); // Eye level
                networkClient->sendPlayerInput(moveX, moveZ, camera.Yaw, camera.Pitch, jump, shoot, static_cast<uint8_t>(currentWeapon), eyePos.x, eyePos.y, eyePos.z);
            }
        }
        
        // Update weapon cooldowns
        if (currentWeaponInstance) {
            currentWeaponInstance->updateCooldown(deltaTime);
        }
        
        // Update projectiles
        updateProjectiles(deltaTime);
        
        // TEMPORARILY DISABLE explosion updates to test stability
        // updateExplosions(deltaTime);
        
        // Update enemies
        updateEnemies(deltaTime);
        
        // Check enemy-player collision
        checkEnemyPlayerCollision(deltaTime);
        
        // Update bullet holes (decay lifetime)
        for (int i = 0; i < bulletHoleCount; i++) {
            if (bulletHoles[i].lifetime > 0.0f) {
                bulletHoles[i].lifetime -= deltaTime;
            }
        }
        
        // Update shooting cooldowns
        if (shootCooldown > 0.0f) {
            shootCooldown -= deltaTime;
        }
        if (pistolCooldown > 0.0f) {
            pistolCooldown -= deltaTime;
        }
        if (shotgunCooldown > 0.0f) {
            shotgunCooldown -= deltaTime;
        }
        if (grenadeCooldown > 0.0f) {
            grenadeCooldown -= deltaTime;
        }
        if (flashbangCooldown > 0.0f) {
            flashbangCooldown -= deltaTime;
        }
        
        // Update flashbang effect
        updateFlashbangEffect(deltaTime);
        
        // Update explosions
        updateExplosions(deltaTime);
        
        // Update damage effect
        if (damageEffectTime > 0.0f) {
            damageEffectTime -= deltaTime;
        }
        
        // PVP collision checks (only in multiplayer)
        if (isConnectedToServer()) {
            checkProjectilePlayerCollision();
            checkPlayerPlayerCollision();
        }
    }
}

void Game::render() {
    // Fullscreen rendering: no borders/letterboxing.
    // Wider windows show more horizontally; taller windows show more vertically (depending on FOV).
    int fbW = 800, fbH = 600;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = 800;
    if (fbH <= 0) fbH = 600;
    renderer.setViewport(0, 0, fbW, fbH);

    if (gameState == PLAYING) {
        renderer.clear();

        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom),
                                                (float)fbW / (float)fbH, 0.1f, 100.0f);
        renderer.setViewProjection(view, projection);

        // Render the map
        if (currentMap) {
            renderer.renderMap(currentMap);
        }
        
        // Render enemies
        renderEnemies();
        
        // Render explosions
        renderExplosions();
        
        // Render bullet holes
        renderBulletHoles();
        
        // Render projectiles (for debugging - make them visible)
        for (int i = 0; i < MAX_PROJECTILES; i++) {
            if (projectiles[i].active) {
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, projectiles[i].position);
                
                // Check if this is a grenade (slower velocity, longer lifetime)
                if (glm::length(projectiles[i].velocity) < 35.0f && projectiles[i].lifetime > 4.0f) {
                    model = glm::scale(model, glm::vec3(0.2f, 0.2f, 0.2f)); // Larger for grenades
                    renderer.drawCube(model, glm::vec3(0.0f, 1.0f, 0.0f)); // Green for grenades
                } else if (glm::length(projectiles[i].velocity) < 30.0f && projectiles[i].lifetime <= 4.0f) {
                    model = glm::scale(model, glm::vec3(0.15f, 0.15f, 0.15f)); // Medium for flashbangs
                    renderer.drawCube(model, glm::vec3(1.0f, 1.0f, 1.0f)); // WHITE for flashbangs
                } else {
                    model = glm::scale(model, glm::vec3(0.1f, 0.1f, 0.1f)); // Smaller for bullets
                    renderer.drawCube(model, glm::vec3(1.0f, 1.0f, 0.0f)); // Yellow for bullets
                }
            }
        }

        // Don't render player in first-person view
        
        // Render remote players (multiplayer)
        if (multiplayerMode) {
            for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                if (remotePlayers[i].active && !remotePlayers[i].isDead) {
                    // Smooth interpolation for remote players to reduce bouncing
                    const float interpolationFactor = 0.2f; // 20% smoothing per frame
                    glm::vec3 targetPos = remotePlayers[i].position;
                    glm::vec3 currentPos = remotePlayers[i].renderPosition;
                    
                    // Initialize render position if not set
                    if (remotePlayers[i].renderPosition.x == 0 && remotePlayers[i].renderPosition.y == 0 && remotePlayers[i].renderPosition.z == 0) {
                        remotePlayers[i].renderPosition = targetPos; // First update, no interpolation
                    } else {
                        // Smooth movement towards target position
                        remotePlayers[i].renderPosition += (targetPos - currentPos) * interpolationFactor;
                    }
                    
                    glm::mat4 model = glm::mat4(1.0f);
                    model = glm::translate(model, remotePlayers[i].renderPosition);
                    model = glm::scale(model, glm::vec3(0.6f, 1.6f, 0.6f));
                    glm::vec3 color = (remotePlayers[i].id % 2 == 0) ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
                    renderer.drawCube(model, color);
                }
            }
        }
        
        // Render crosshair
        renderCrosshair();
        
        // Render damage effect
        renderDamageEffect();
        
        // Render flashbang effect
        renderFlashbangEffect();
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    } else if (gameState == DEAD) {
        // Set up 2D rendering for death screen
        renderer.clear();
        
        setVirtualUIProjection(renderer, window);
        
        // Render death screen
        renderDeathScreen();
        glfwSwapBuffers(window);
        glfwPollEvents();
    } else if (gameState == MENU) {
        // Set up 2D rendering for menu
        renderer.clear();
        
        setVirtualUIProjection(renderer, window);
        
        renderMenu();
        glfwSwapBuffers(window);
        glfwPollEvents();
    } else if (gameState == SETTINGS) {
        // Set up 2D rendering for settings
        renderer.clear();
        
        setVirtualUIProjection(renderer, window);
        
        renderSettings();
        glfwSwapBuffers(window);
        glfwPollEvents();
    } else if (gameState == MAP_SELECTION) {
        // Set up 2D rendering for map selection
        renderer.clear();
        
        setVirtualUIProjection(renderer, window);
        
        renderMapSelection();
        glfwSwapBuffers(window);
        glfwPollEvents();
    } else if (gameState == SERVER_CONNECT) {
        // Set up 2D rendering for server connection
        renderServerConnect();
        glfwSwapBuffers(window);
        glfwPollEvents();
    } else if (gameState == MAP_EDITOR) {
        // Render map editor - 3D view
        renderEditor();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}

void Game::cleanup() {
    if (currentMap) {
        delete currentMap;
        currentMap = nullptr;
    }
}

bool Game::shouldClose() const {
    return glfwWindowShouldClose(window);
}

bool Game::loadMap(const std::string& mapFile) {
    if (currentMap) {
        delete currentMap;
    }
    
    currentMap = new Map("assets/maps/" + mapFile);
    
    // Load textures for the map
    if (currentMap) {
        currentMap->loadTexture("grass.png", 0);
        currentMap->loadTexture("stone.png", 1);
        currentMap->loadTexture("brick.png", 2);
        currentMap->loadTexture("metal.png", 3);
        currentMap->loadTexture("wood.png", 4);
        currentMap->loadTexture("sand.png", 5);
        currentMap->loadTexture("ice.png", 6);
        currentMap->loadTexture("dirt.png", 7);
    }
    
    return currentMap != nullptr;
}

Map* Game::getCurrentMap() {
    return currentMap;
}

void Game::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    // Keep OpenGL viewport in sync with window size.
    if (width <= 0 || height <= 0) return;
    glViewport(0, 0, width, height);
}

void Game::mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    Game* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (game) {
        if (game->firstMouse) {
            game->lastX = xpos;
            game->lastY = ypos;
            game->firstMouse = false;
        }
        float xoffset = xpos - game->lastX;
        float yoffset = game->lastY - ypos;
        game->lastX = xpos;
        game->lastY = ypos;
        game->camera.ProcessMouseMovement(xoffset, yoffset);
    }
}

void Game::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    Game* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (game) {
        game->camera.ProcessMouseScroll(yoffset);
    }
}

void Game::updatePlayerPhysics(float deltaTime) {
    // AUTOMATIC UNSTUCK: Check if player is inside a block and move them out
    if (currentMap && currentMap->checkCollision(playerPos, 0.15f)) {
        int unstuckCounter = 0;
        while (currentMap->checkCollision(playerPos, 0.15f) && unstuckCounter < 50) {
            playerPos.y += 0.2f; // Move up gradually
            unstuckCounter++;
        }
        if (unstuckCounter > 0) {
            std::cout << "UNSTUCK: Moved player up by " << (unstuckCounter * 0.2f) << " units" << std::endl;
            playerVelocity = glm::vec3(0.0f, 0.0f, 0.0f); // Reset velocity
        }
    }
    
    // Check for death by falling
    if (playerPos.y < -20.0f && !isDead) {
        isDead = true;
        playerHealth = 0.0f;
        gameState = DEAD;
        std::cout << "Player fell out of the world and died!" << std::endl;
        return;
    }
    
    // Update jump cooldown
    if (jumpCooldown > 0.0f) {
        jumpCooldown -= deltaTime;
    }
    
    // Apply gravity
    playerVelocity.y -= 9.8f * deltaTime;
    
    // Calculate intended new position
    glm::vec3 newPos = playerPos + playerVelocity * deltaTime;
    
    // Store original position
    glm::vec3 originalPos = playerPos;
    
    // Check collision at new position
    bool collision = false;
    if (currentMap) {
        collision = currentMap->checkCollision(newPos, 0.2f); // Smaller collision radius for accurate hit detection
    }
    
    if (collision) {
        // Try X axis movement only
        glm::vec3 testPosX = originalPos;
        testPosX.x = newPos.x;
        bool xCollision = currentMap->checkCollision(testPosX, 0.2f);
        
        // Try Z axis movement only  
        glm::vec3 testPosZ = originalPos;
        testPosZ.z = newPos.z;
        bool zCollision = currentMap->checkCollision(testPosZ, 0.2f);
        
        // Apply horizontal movement first (walls should stop horizontal movement)
        if (!xCollision) {
            playerPos.x = newPos.x;
        } else {
            playerVelocity.x = 0.0f;
        }
        
        if (!zCollision) {
            playerPos.z = newPos.z;
        } else {
            playerVelocity.z = 0.0f;
        }
        
        // Now check Y axis movement only if horizontal movement was successful
        // This prevents getting pushed up when hitting walls
        glm::vec3 testPosY = playerPos; // Use updated position after horizontal movement
        testPosY.y = newPos.y;
        bool yCollision = currentMap->checkCollision(testPosY, 0.2f);
        
        if (!yCollision) {
            playerPos.y = newPos.y;
        } else {
            // Check if we're hitting a floor (moving down and blocked)
            if (playerVelocity.y < 0.0f) {
                playerVelocity.y = 0.0f;
                isJumping = false;
            } else if (playerVelocity.y > 0.0f) {
                // Hitting ceiling
                playerVelocity.y = 0.0f;
            }
        }
    } else {
        // No collision - allow full movement (falling through air)
        playerPos = newPos;
    }
    
    // Ground detection - check if we're standing on a solid block
    bool onGround = false;
    if (currentMap) {
        // Check if standing on a block - cast down from feet
        glm::vec3 footPos = playerPos;
        footPos.y -= 0.05f; // Just below feet
        
        // Check multiple points around the player's feet for better detection
        glm::vec3 checkPoints[5] = {
            footPos,                                    // Center
            footPos + glm::vec3(0.2f, 0, 0),           // Forward
            footPos + glm::vec3(-0.2f, 0, 0),          // Back
            footPos + glm::vec3(0, 0, 0.2f),           // Right
            footPos + glm::vec3(0, 0, -0.2f)           // Left
        };
        
        int groundPoints = 0;
        bool centerOnGround = false;
        float groundHeight = 0.0f;
        
        for (int i = 0; i < 5; i++) {
            if (currentMap->checkCollision(checkPoints[i], 0.15f)) {
                groundPoints++;
                if (i == 0) {
                    centerOnGround = true;
                    groundHeight = checkPoints[i].y;
                }
            }
        }
        
        // Only consider on ground if center point is on ground (prevents edge bouncing)
        // or if majority of points are on ground
        if (centerOnGround || groundPoints >= 3) {
            onGround = true;
            
            // Only snap Y position when center is on ground (prevents edge bouncing)
            if (centerOnGround && playerVelocity.y <= 0.0f) {
                playerPos.y = groundHeight + 1.6f; // Eye level when on ground
            }
            
            // Stop falling if we're moving downward
            if (playerVelocity.y <= 0.0f) {
                playerVelocity.y = 0.0f;
                isJumping = false;
            }
        }
    }
    
    // Debug output
    static int frameCount = 0;
    if (frameCount++ % 120 == 0) {
        std::cout << "Physics - Pos: (" << playerPos.x << ", " << playerPos.y << ", " << playerPos.z 
                  << ") Vel: (" << playerVelocity.x << ", " << playerVelocity.y << ", " << playerVelocity.z 
                  << ") - " << (onGround ? "ON GROUND" : "IN AIR/FALLING") << std::endl;
    }
    
    // Update camera
    camera.Position = glm::vec3(playerPos.x, playerPos.y + 1.6f, playerPos.z);
    camera.updateCameraVectors();
}

void Game::handleJump() {
    // Collision-based ground detection - check if there's a block under player's feet
    bool onGround = false;
    
    if (currentMap) {
        // Check if standing on a block - cast a small ray down from feet
        // Use a small box below player's feet for better detection
        glm::vec3 footPos = playerPos;
        footPos.y -= 0.3f; // Further below feet for better detection
        
        // Check multiple points around the player's feet
        glm::vec3 checkPoints[9] = {
            footPos,                                    // Center
            footPos + glm::vec3(0.2f, 0, 0),           // Forward
            footPos + glm::vec3(-0.2f, 0, 0),          // Back
            footPos + glm::vec3(0, 0, 0.2f),           // Right
            footPos + glm::vec3(0, 0, -0.2f),          // Left
            footPos + glm::vec3(0.15f, 0, 0.15f),     // Forward-Right
            footPos + glm::vec3(-0.15f, 0, 0.15f),    // Forward-Left
            footPos + glm::vec3(0.15f, 0, -0.15f),    // Back-Right
            footPos + glm::vec3(-0.15f, 0, -0.15f)    // Back-Left
        };
        
        for (int i = 0; i < 9; i++) {
            if (currentMap->checkCollision(checkPoints[i], 0.15f)) {
                onGround = true;
                break;
            }
        }
    }
    
    if (onGround && !isJumping && jumpCooldown <= 0.0f) {
        playerVelocity.y = 4.0f; // Jump velocity
        isJumping = true;
        jumpCooldown = 0.5f; // 0.5 second cooldown
        std::cout << "Jump triggered! Player y: " << playerPos.y << std::endl;
    }
}

bool Game::createProjectile(const glm::vec3& position, const glm::vec3& velocity, float lifetime, uint32_t owner) {
    // Find an inactive projectile slot
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) {
            projectiles[i].position = position;
            projectiles[i].velocity = velocity;
            projectiles[i].lifetime = lifetime;
            projectiles[i].active = true;
            projectiles[i].owner = owner;
            activeProjectiles++;
            return true;
        }
    }
    return false;
}

void Game::shoot() {
    if (currentWeaponInstance && currentWeaponInstance->canShoot()) {
        currentWeaponInstance->shoot(this);
    }
}

void Game::switchWeapon(int weaponNumber) {
    switch (weaponNumber) {
        case 1:
            currentWeapon = PISTOL;
            currentWeaponInstance = pistol;
            std::cout << "Switched to Pistol! Ammo: " << (infiniteAmmoMode ? "∞" : std::to_string(pistolAmmo)) << std::endl;
            break;
        case 2:
            currentWeapon = PUMP_SHOTGUN;
            currentWeaponInstance = shotgun;
            std::cout << "Switched to Pump Shotgun! Ammo: " << (infiniteAmmoMode ? "∞" : std::to_string(shotgunAmmo)) << std::endl;
            break;
        case 3:
            currentWeapon = GRENADE;
            currentWeaponInstance = grenade;
            std::cout << "Switched to Grenade! Count: " << (infiniteAmmoMode ? "∞" : std::to_string(grenadeCount)) << std::endl;
            break;
        case 4:
            currentWeapon = FLASHBANG;
            currentWeaponInstance = flashbang;
            std::cout << "Switched to Flashbang! Count: " << (infiniteAmmoMode ? "∞" : std::to_string(flashbangCount)) << std::endl;
            break;
    }
}

void Game::shootPistol() {
    // Safety check
    if (activeProjectiles >= MAX_PROJECTILES) {
        std::cout << "Max projectiles reached!" << std::endl;
        return;
    }
    
    if (pistolCooldown > 0.0f) {
        return;
    }
    
    if (pistolAmmo <= 0 && !infiniteAmmoMode) {
        std::cout << "Out of pistol ammo!" << std::endl;
        return;
    }
    
    createProjectile(camera.Position, camera.Front * 120.0f, 3.0f, 0); // owner=0 for local player
    if (!infiniteAmmoMode) {
        pistolAmmo--;
    }
    pistolCooldown = 0.15f; 
    std::cout << "Pistol shot! Active: " << activeProjectiles << " Ammo: " << (infiniteAmmoMode ? "∞" : std::to_string(pistolAmmo)) << std::endl;
}

void Game::shootShotgun() {
    // Safety check
    if (activeProjectiles >= MAX_PROJECTILES) {
        std::cout << "Max projectiles reached!" << std::endl;
        return;
    }
    
    if (shotgunAmmo <= 0 && !infiniteAmmoMode) {
        std::cout << "Out of shotgun ammo!" << std::endl;
        return;
    }
    
    // Shoot 5 projectiles in a spread pattern
    int pelletsShot = 0;
    for (int i = 0; i < MAX_PROJECTILES && pelletsShot < 5; i++) {
        if (!projectiles[i].active) {
            // Add spread to shotgun pellets - REDUCED SPREAD
            glm::vec3 spread = glm::vec3(
                (rand() % 100 - 50) * 0.002f, // -0.1 to 0.1 (reduced from 0.5)
                (rand() % 100 - 50) * 0.002f, // -0.1 to 0.1 (reduced from 0.5)
                0.0f
            );
            createProjectile(camera.Position, (camera.Front + spread) * 50.0f, 1.0f, 0); // owner=0 for local player
            pelletsShot++;
        }
    }
    if (!infiniteAmmoMode) {
        shotgunAmmo--;
    }
    shotgunCooldown = 0.8f; // 0.8 second cooldown (pump action)
    std::cout << "Shotgun blast! Pellets: " << pelletsShot << " Ammo: " << (infiniteAmmoMode ? "∞" : std::to_string(shotgunAmmo)) << std::endl;
}

void Game::throwGrenade() {
    // Safety check
    if (activeProjectiles >= MAX_PROJECTILES) {
        std::cout << "Max projectiles reached!" << std::endl;
        return;
    }
    
    if (grenadeCount <= 0 && !infiniteAmmoMode) {
        std::cout << "Out of grenades!" << std::endl;
        return;
    }
    
    createProjectile(camera.Position, camera.Front * 30.0f, 5.0f, 0); // owner=0 for local player
    if (!infiniteAmmoMode) {
        grenadeCount--;
    }
    grenadeCooldown = 1.5f; // Longer cooldown
    std::cout << "Threw grenade! Grenades left: " << (infiniteAmmoMode ? "∞" : std::to_string(grenadeCount)) << std::endl;
}

void Game::throwFlashbang() {
    // Safety check
    if (activeProjectiles >= MAX_PROJECTILES) {
        std::cout << "Max projectiles reached!" << std::endl;
        return;
    }
    
    if (flashbangCount <= 0 && !infiniteAmmoMode) {
        std::cout << "Out of flashbangs!" << std::endl;
        return;
    }
    
    createProjectile(camera.Position, camera.Front * 25.0f, 4.0f, 0); // owner=0 for local player
    if (!infiniteAmmoMode) {
        flashbangCount--;
    }
    flashbangCooldown = 2.0f; // Longer cooldown
    std::cout << "Threw flashbang! Flashbangs left: " << (infiniteAmmoMode ? "∞" : std::to_string(flashbangCount)) << std::endl;
}

void Game::updateProjectiles(float deltaTime) {
    // Store previous positions for swept collision
    static glm::vec3 prevGrenadePositions[MAX_PROJECTILES];
    
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (projectiles[i].active) {
            // Store position before update
            glm::vec3 prevPos = projectiles[i].position;
            
            // Apply gravity to all projectiles (pistol bullets, grenades, flashbangs)
            // All projectiles should arc - pistol bullets are fast (120), grenades are medium (30), flashbangs are slow (25)
            if (glm::length(projectiles[i].velocity) > 20.0f) {
                projectiles[i].velocity.y -= 15.0f * deltaTime; // Gravity effect
            }
            
            // Simple position update
            projectiles[i].position += projectiles[i].velocity * deltaTime;
            
            // Check if this is a grenade or flashbang
            bool isGrenade = glm::length(projectiles[i].velocity) < 35.0f && projectiles[i].lifetime > 4.0f;
            bool isFlashbang = glm::length(projectiles[i].velocity) < 35.0f && projectiles[i].lifetime <= 4.0f;

            auto detonateAndDeactivate = [&](const glm::vec3& atPos) {
                if (isGrenade) {
                    createSimpleExplosion(atPos);
                } else if (isFlashbang) {
                    createFlashbangEffect(atPos);
                } else {
                    // Bullets: no explosion, no bounce. Just leave a mark.
                    createBulletHole(atPos, glm::vec3(0.0f, 1.0f, 0.0f));
                }
                projectiles[i].active = false;
                activeProjectiles--;
            };
            
            // Check map block collisions for grenades/flashbangs (they should bounce, not stick)
            if ((isGrenade || isFlashbang) && currentMap) {
                const std::vector<MapBlock>& blocks = currentMap->getBlocks();
                glm::vec3 rayDir = projectiles[i].position - prevPos;
                float rayLength = glm::length(rayDir);
                
                if (rayLength > 0.001f) {
                    rayDir = rayDir / rayLength; // Normalize
                }
                
                for (const MapBlock& block : blocks) {
                    if (!block.solid) continue;
                    
                    // Block bounds
                    glm::vec3 blockMin = block.position - block.scale * 0.5f;
                    glm::vec3 blockMax = block.position + block.scale * 0.5f;
                    
                    // Simple point check first (for slow grenades)
                    if (projectiles[i].position.x >= blockMin.x && projectiles[i].position.x <= blockMax.x &&
                        projectiles[i].position.y >= blockMin.y && projectiles[i].position.y <= blockMax.y &&
                        projectiles[i].position.z >= blockMin.z && projectiles[i].position.z <= blockMax.z) {
                        
                        // No bounce: detonate on impact.
                        detonateAndDeactivate(projectiles[i].position);
                        break;
                    }
                    
                    // Swept collision for fast-moving grenades - ray-box intersection
                    if (rayLength > 0.1f) {
                        // Ray-box intersection using slab method
                        float tmin = 0.0f;
                        float tmax = rayLength;
                        
                        // Check X slab
                        if (abs(rayDir.x) < 0.0001f) {
                            if (prevPos.x < blockMin.x || prevPos.x > blockMax.x) continue;
                        } else {
                            float tx1 = (blockMin.x - prevPos.x) / rayDir.x;
                            float tx2 = (blockMax.x - prevPos.x) / rayDir.x;
                            float txmin = std::min(tx1, tx2);
                            float txmax = std::max(tx1, tx2);
                            tmin = std::max(tmin, txmin);
                            tmax = std::min(tmax, txmax);
                            if (tmin > tmax) continue;
                        }
                        
                        // Check Y slab
                        if (abs(rayDir.y) < 0.0001f) {
                            if (prevPos.y < blockMin.y || prevPos.y > blockMax.y) continue;
                        } else {
                            float ty1 = (blockMin.y - prevPos.y) / rayDir.y;
                            float ty2 = (blockMax.y - prevPos.y) / rayDir.y;
                            float tymin = std::min(ty1, ty2);
                            float tymax = std::max(ty1, ty2);
                            tmin = std::max(tmin, tymin);
                            tmax = std::min(tmax, tymax);
                            if (tmin > tmax) continue;
                        }
                        
                        // Check Z slab
                        if (abs(rayDir.z) < 0.0001f) {
                            if (prevPos.z < blockMin.z || prevPos.z > blockMax.z) continue;
                        } else {
                            float tz1 = (blockMin.z - prevPos.z) / rayDir.z;
                            float tz2 = (blockMax.z - prevPos.z) / rayDir.z;
                            float tzmin = std::min(tz1, tz2);
                            float tzmax = std::max(tz1, tz2);
                            tmin = std::max(tmin, tzmin);
                            tmax = std::min(tmax, tzmax);
                            if (tmin > tmax) continue;
                        }
                        
                        // Collision detected along the ray
                        if (tmin <= rayLength && tmin >= 0.0f) {
                            // Position grenade at impact point
                            projectiles[i].position = prevPos + rayDir * (tmin - 0.01f);

                            // No bounce: detonate on impact.
                            detonateAndDeactivate(projectiles[i].position);
                            break;
                        }
                    }
                }
                
                // Store current position for next frame
                prevGrenadePositions[i] = projectiles[i].position;
            }
            
            // Simple boundary check (no bounce): treat edges/floor as walls
            float boundary = 20.0f;
            bool hitBoundary =
                projectiles[i].position.x > boundary || projectiles[i].position.x < -boundary ||
                projectiles[i].position.z > boundary || projectiles[i].position.z < -boundary ||
                projectiles[i].position.y <= -10.0f;

            if (hitBoundary) {
                // Clamp so effects/holes spawn near the boundary, not far outside it.
                glm::vec3 clamped = projectiles[i].position;
                clamped.x = std::min(boundary, std::max(-boundary, clamped.x));
                clamped.z = std::min(boundary, std::max(-boundary, clamped.z));
                clamped.y = std::max(-9.8f, clamped.y);

                detonateAndDeactivate(clamped);
                continue;
            }

            // Update lifetime
            projectiles[i].lifetime -= deltaTime;

            // Detonate / expire with no bounce
            if (projectiles[i].lifetime <= 0.0f) {
                // Grenade -> explode, Flashbang -> flash, Bullets -> just disappear.
                if (isGrenade || isFlashbang) {
                    detonateAndDeactivate(projectiles[i].position);
                } else {
                    projectiles[i].active = false;
                    activeProjectiles--;
                }
                continue;
            }

            // Check collision with enemies first
            if (checkProjectileEnemyCollision(projectiles[i])) {
                // Create simple explosion for grenades
                if (glm::length(projectiles[i].velocity) < 35.0f && projectiles[i].lifetime > 4.0f) {
                    createSimpleExplosion(projectiles[i].position);
                }
                // Create flashbang effect for flashbangs when they expire
                else if (glm::length(projectiles[i].velocity) < 35.0f && projectiles[i].lifetime <= 4.0f) {
                    createFlashbangEffect(projectiles[i].position);
                }
                // Deactivate projectile
                projectiles[i].active = false;
                activeProjectiles--;
                std::cout << "Projectile hit enemy! Active: " << activeProjectiles << std::endl;
            }
            
            // Check collision with walls (different behavior for different projectile types)
            if (checkProjectileCollision(projectiles[i])) {
                // Check projectile type by velocity
                bool isGrenade = glm::length(projectiles[i].velocity) < 35.0f && projectiles[i].lifetime > 4.0f;
                bool isFlashbang = glm::length(projectiles[i].velocity) < 35.0f && projectiles[i].lifetime <= 4.0f;
                bool isPistolBullet = glm::length(projectiles[i].velocity) > 35.0f; // Fast velocity = pistol bullet
                
                if (isPistolBullet) {
                    // Pistol bullets: simple damage, no bounce, no explosion
                    createBulletHole(projectiles[i].position, glm::vec3(0.0f, 1.0f, 0.0f)); // Default normal up
                    projectiles[i].active = false;
                    activeProjectiles--;
                    std::cout << "Bullet hit wall! Active: " << activeProjectiles << std::endl;
                }
                else if (isGrenade) {
                    // Grenades: explode on wall hit
                    createSimpleExplosion(projectiles[i].position);
                    projectiles[i].active = false;
                    activeProjectiles--;
                    std::cout << "Grenade exploded on wall! Active: " << activeProjectiles << std::endl;
                }
                else if (isFlashbang) {
                    // Flashbangs: flash effect on wall hit
                    createFlashbangEffect(projectiles[i].position);
                    projectiles[i].active = false;
                    activeProjectiles--;
                    std::cout << "Flashbang hit wall! Active: " << activeProjectiles << std::endl;
                }
            }
        }
    }
}

void Game::createSimpleExplosion(glm::vec3 position) {
    std::cout << "BOOM! Grenade exploded at: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
    
    // Create big red explosion cube that will expand and fade
    // Find an inactive explosion slot
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!explosions[i].active) {
            explosions[i].position = position;
            explosions[i].scale = 0.5f;  // Start small
            explosions[i].maxScale = 3.0f;  // Grow to 3 units
            explosions[i].lifetime = 0.5f;  // 0.5 seconds duration
            explosions[i].active = true;
            explosions[i].color = glm::vec3(1.0f, 0.0f, 0.0f);  // RED
            std::cout << "Explosion effect created! Scale: " << explosions[i].scale << std::endl;
            break;
        }
    }
    
    // Also draw immediate red cube for instant feedback
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = glm::scale(model, glm::vec3(2.0f)); // Big 2-unit cube
    renderer.drawCube(model, glm::vec3(1.0f, 0.0f, 0.0f)); // RED color for explosion
    
    // Damage nearby enemies
    float explosionRadius = 3.0f; // 3 unit explosion radius
    float explosionDamage = 75.0f; // 75 damage
    
    for (int j = 0; j < MAX_ENEMIES; j++) {
        if (enemies[j].active && enemies[j].alive) {
            glm::vec3 enemyPos(enemies[j].x, enemies[j].y, enemies[j].z);
            float distance = glm::length(position - enemyPos);
            if (distance < explosionRadius) {
                // Damage enemy based on distance
                float damageMultiplier = 1.0f - (distance / explosionRadius);
                enemies[j].health -= explosionDamage * damageMultiplier;
                std::cout << "Enemy hit by explosion! Health: " << enemies[j].health << std::endl;
                
                if (enemies[j].health <= 0.0f) {
                    enemies[j].alive = false;
                    enemies[j].active = false;
                    activeEnemies--;
                    std::cout << "Enemy killed by explosion!" << std::endl;
                }
            }
        }
    }
    
    // Damage player if too close
    float playerDistance = glm::length(position - playerPos);
    if (playerDistance < explosionRadius && !isDead) {
        float damageMultiplier = 1.0f - (playerDistance / explosionRadius);
        playerHealth -= 40.0f * damageMultiplier; // 40 max damage
        damageEffectTime = 1.5f; // Longer screen effect
        std::cout << "Player hit by explosion! Health: " << playerHealth << "/" << playerMaxHealth << std::endl;
        
        if (playerHealth <= 0.0f && !isDead) {
            playerHealth = 0.0f;
            isDead = true;
            gameState = DEAD;
            std::cout << "Player defeated by explosion!" << std::endl;
        }
    }
}

void Game::createFlashbangEffect(glm::vec3 position) {
    std::cout << "FLASH! Flashbang detonated at: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
    
    // Calculate distance from player to flashbang
    float playerDistance = glm::length(position - playerPos);
    float flashRadius = 5.0f; // 5 unit flash radius

    // Require the player to be looking roughly toward the flash to get flashed.
    glm::vec3 toFlash = position - camera.Position;
    float toLen = glm::length(toFlash);
    if (toLen < 0.001f) toLen = 0.001f;
    toFlash /= toLen;
    float facing = glm::dot(glm::normalize(camera.Front), toFlash); // 1=looking at it, 0=90deg, -1=behind
    if (facing < 0.2f) {
        // Not looking at the flash: no effect.
        return;
    }

    // Scale strength by distance and facing.
    float flashIntensity = 1.0f - (playerDistance / flashRadius);
    if (flashIntensity < 0.0f) flashIntensity = 0.0f;
    if (flashIntensity > 1.0f) flashIntensity = 1.0f;
    if (facing > 1.0f) facing = 1.0f;
    flashIntensity *= facing;

    // Up to 3 seconds when close; minimal flash even if just outside radius.
    float duration = 3.0f * flashIntensity;
    if (duration < 0.35f) duration = 0.35f;

    flashEffectTime = duration;
    isFlashed = true;
    std::cout << "Player flashed! Duration: " << flashEffectTime << " seconds" << std::endl;
    
    // Disorient enemies temporarily (log-only for now)
    for (int j = 0; j < MAX_ENEMIES; j++) {
        if (enemies[j].active && enemies[j].alive) {
            glm::vec3 enemyPos(enemies[j].x, enemies[j].y, enemies[j].z);
            float enemyDistance = glm::length(position - enemyPos);
            if (enemyDistance < flashRadius) {
                std::cout << "Enemy disoriented by flashbang!" << std::endl;
            }
        }
    }
}

void Game::updateFlashbangEffect(float deltaTime) {
    if (flashEffectTime > 0.0f) {
        flashEffectTime -= deltaTime;
        if (flashEffectTime <= 0.0f) {
            isFlashed = false;
            std::cout << "Flash effect worn off" << std::endl;
        }
    }
}

void Game::renderFlashbangEffect() {
    if (isFlashed && flashEffectTime > 0.0f) {
        // Create a white overlay effect for flash
        float alpha = flashEffectTime / 3.0f; // Normalize to 0-1 range
        if (alpha > 1.0f) alpha = 1.0f;
        
        setVirtualUIProjection(renderer, window);
        float left, right, top, bottom;
        getVirtualUIBounds(window, left, right, top, bottom);
        float centerX = (left + right) * 0.5f;
        float centerY = (top + bottom) * 0.5f;
        float w = (right - left);
        float h = (bottom - top);
        
        // Ensure overlay is visible over the 3D scene
        glDisable(GL_DEPTH_TEST);

        // Draw white overlay
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(centerX, centerY, 0.0f));
        model = glm::scale(model, glm::vec3(w, h, 1.0f));
        renderer.drawCube(model, glm::vec3(alpha, alpha, alpha)); // White with alpha

        glEnable(GL_DEPTH_TEST);
    }
}

void Game::updateExplosions(float deltaTime) {
    // Update all active explosions
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (explosions[i].active) {
            // Reduce lifetime
            explosions[i].lifetime -= deltaTime;
            
            // Grow the explosion
            float growthRate = explosions[i].maxScale / 0.5f; // Grow to max in 0.5s
            explosions[i].scale += growthRate * deltaTime;
            
            // Cap at max scale
            if (explosions[i].scale > explosions[i].maxScale) {
                explosions[i].scale = explosions[i].maxScale;
            }
            
            // Deactivate if lifetime expired
            if (explosions[i].lifetime <= 0.0f) {
                explosions[i].active = false;
            }
        }
    }
}

void Game::renderExplosions() {
    // Render all active explosions
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (explosions[i].active) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, explosions[i].position);
            model = glm::scale(model, glm::vec3(explosions[i].scale));
            
            // Fade out as lifetime decreases
            float alpha = explosions[i].lifetime / 0.5f;
            if (alpha > 1.0f) alpha = 1.0f;
            if (alpha < 0.0f) alpha = 0.0f;
            
            // Red color with alpha based on lifetime
            glm::vec3 color = explosions[i].color * alpha;
            renderer.drawCube(model, color);
        }
    }
}

bool Game::checkProjectileCollision(Projectile& projectile) {
    // Simple boundary check - treat world edges as walls
    float boundary = 50.0f;
    
    // Only check boundaries if projectile has moved significantly from spawn
    float distanceFromPlayer = glm::length(projectile.position - camera.Position);
    if (distanceFromPlayer > 1.0f) { // Only check boundaries after moving 1 unit
        if (abs(projectile.position.x) >= boundary || 
            abs(projectile.position.z) >= boundary ||
            projectile.position.y <= -10.0f) {
            return true;
        }
    }
    
    // Check collision with map blocks if map is loaded
    if (currentMap) {
        const std::vector<MapBlock>& blocks = currentMap->getBlocks();
        
        // Store previous position for swept collision
        static glm::vec3 prevPositions[MAX_PROJECTILES];
        int projIndex = -1;
        for (int i = 0; i < MAX_PROJECTILES; i++) {
            if (&projectiles[i] == &projectile) {
                projIndex = i;
                break;
            }
        }
        
        glm::vec3 prevPos = (projIndex >= 0) ? prevPositions[projIndex] : projectile.position;
        glm::vec3 currPos = projectile.position;
        glm::vec3 rayDir = currPos - prevPos;
        float rayLength = glm::length(rayDir);
        
        if (rayLength > 0.001f) {
            rayDir = rayDir / rayLength; // Normalize
        }
        
        // Check each block with swept collision (ray-box intersection)
        for (const MapBlock& block : blocks) {
            if (!block.solid) continue;
            
            // Block bounds
            glm::vec3 blockMin = block.position - block.scale * 0.5f;
            glm::vec3 blockMax = block.position + block.scale * 0.5f;
            
            // Only check collision if projectile is moving fast enough and has traveled some distance
            if (rayLength > 0.1f && distanceFromPlayer > 0.5f) {
                // Swept collision for fast projectiles - ray-box intersection
                float tmin = 0.0f;
                float tmax = rayLength;
                
                // Check X slab
                if (abs(rayDir.x) < 0.0001f) {
                    if (currPos.x < blockMin.x || currPos.x > blockMax.x) continue;
                } else {
                    float t1 = (blockMin.x - prevPos.x) / rayDir.x;
                    float t2 = (blockMax.x - prevPos.x) / rayDir.x;
                    tmin = std::max(tmin, std::min(t1, t2));
                    tmax = std::min(tmax, std::max(t1, t2));
                }
                
                // Check Y slab
                if (abs(rayDir.y) < 0.0001f) {
                    if (currPos.y < blockMin.y || currPos.y > blockMax.y) continue;
                } else {
                    float t1 = (blockMin.y - prevPos.y) / rayDir.y;
                    float t2 = (blockMax.y - prevPos.y) / rayDir.y;
                    tmin = std::max(tmin, std::min(t1, t2));
                    tmax = std::min(tmax, std::max(t1, t2));
                }
                
                // Check Z slab
                if (abs(rayDir.z) < 0.0001f) {
                    if (currPos.z < blockMin.z || currPos.z > blockMax.z) continue;
                } else {
                    float t1 = (blockMin.z - prevPos.z) / rayDir.z;
                    float t2 = (blockMax.z - prevPos.z) / rayDir.z;
                    tmin = std::max(tmin, std::min(t1, t2));
                    tmax = std::min(tmax, std::max(t1, t2));
                }
                
                // Check if ray intersects block
                if (tmin <= tmax && tmin >= 0.0f && tmin <= rayLength) {
                    if (projIndex >= 0) prevPositions[projIndex] = currPos;
                    return true;
                }
            }
        }
        
        // Store current position for next frame
        if (projIndex >= 0) prevPositions[projIndex] = currPos;
    }
    
    return false;
}

void Game::createBulletHole(const glm::vec3& position, const glm::vec3& normal) {
    // Find an inactive bullet hole slot or replace oldest
    int index = -1;
    if (bulletHoleCount < MAX_BULLET_HOLES) {
        index = bulletHoleCount;
        bulletHoleCount++;
    } else {
        // Replace oldest bullet hole
        index = 0;
        for (int i = 0; i < MAX_BULLET_HOLES; i++) {
            if (bulletHoles[i].lifetime < bulletHoles[index].lifetime) {
                index = i;
            }
        }
    }
    
    // Create bullet hole
    bulletHoles[index].position = position;
    bulletHoles[index].normal = normal;
    bulletHoles[index].size = 0.2f;
    bulletHoles[index].lifetime = 30.0f; // 30 seconds lifetime
    
    std::cout << "Created bullet hole at: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
}

void Game::renderBulletHoles() {
    for (int i = 0; i < bulletHoleCount; i++) {
        if (bulletHoles[i].lifetime > 0.0f) {
            // Create a small dark quad for the bullet hole
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, bulletHoles[i].position);
            model = glm::scale(model, glm::vec3(bulletHoles[i].size, bulletHoles[i].size, bulletHoles[i].size));
            
            // Dark color for bullet hole
            renderer.drawCube(model, glm::vec3(0.1f, 0.1f, 0.1f));
        }
    }
}

void Game::spawnEnemy(const glm::vec3& position) {
    // Find an inactive enemy slot
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            // Initialize enemy with simplified struct
            enemies[i].x = position.x;
            enemies[i].y = position.y;
            enemies[i].z = position.z;
            enemies[i].health = 50.0f;
            enemies[i].active = true;
            enemies[i].alive = true;
            activeEnemies++;

            std::cout << "Spawned enemy at: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
            break;
        }
    }

    if (activeEnemies >= MAX_ENEMIES) {
        std::cout << "Max enemies reached!" << std::endl;
    }
}

/*
void Game::spawnEnemy(const glm::vec3& position) {
    // Find an inactive enemy slot
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            // Initialize enemy
            enemies[i].position = position;
            enemies[i].velocity = glm::vec3(0.0f);
            enemies[i].health = 50.0f; // 50 health
            enemies[i].maxHealth = 50.0f;
            enemies[i].speed = 2.0f; // 2 units per second
            enemies[i].damageCooldown = 0.0f;
            enemies[i].active = true;
            enemies[i].alive = true;
            activeEnemies++;

            std::cout << "Spawned enemy at: (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
            break;
        }
    }

    if (activeEnemies >= MAX_ENEMIES) {
        std::cout << "Max enemies reached!" << std::endl;
    }
}

void Game::updateEnemies(float deltaTime) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].alive) {
            // Calculate direction to player
            glm::vec3 direction = playerPos - enemies[i].position;
            float distance = glm::length(direction);
            
            if (distance > 0.1f) { // Avoid division by zero
                // Normalize direction and move towards player (horizontal only)
                direction.y = 0.0f; // Keep horizontal movement
                direction = glm::normalize(direction);
                enemies[i].velocity.x = direction.x * enemies[i].speed;
                enemies[i].velocity.z = direction.z * enemies[i].speed;
            }
            
            // Apply gravity to enemies
            enemies[i].velocity.y -= 9.8f * deltaTime;
            
            // Update position
            enemies[i].position += enemies[i].velocity * deltaTime;
            
            // Ground collision for enemies
            if (enemies[i].position.y < -1.0f) {
                enemies[i].position.y = -1.0f;
                enemies[i].velocity.y = 0.0f;
            }
            
            // Update damage cooldown
            if (enemies[i].damageCooldown > 0.0f) {
                enemies[i].damageCooldown -= deltaTime;
            }
        }
    }
}

bool Game::checkProjectileEnemyCollision(Projectile& projectile) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].alive) {
            // Check distance between projectile and enemy
            float distance = glm::length(projectile.position - enemies[i].position);
            if (distance < 0.5f) { // Hit radius of 0.5 units
                // Damage the enemy
                damageEnemy(i, 25.0f); // 25 damage per hit
                return true; // Hit detected
            }
        }
    }
    return false; // No hit
}

void Game::damageEnemy(int enemyIndex, float damage) {
    if (enemyIndex >= 0 && enemyIndex < MAX_ENEMIES && enemies[enemyIndex].active && enemies[enemyIndex].alive) {
        enemies[enemyIndex].health -= damage;
        std::cout << "Enemy hit! Health: " << enemies[enemyIndex].health << "/" << enemies[enemyIndex].maxHealth << std::endl;
        
        if (enemies[enemyIndex].health <= 0.0f) {
            enemies[enemyIndex].alive = false;
            enemies[enemyIndex].active = false;
            activeEnemies--;
            std::cout << "Enemy defeated!" << std::endl;
        }
    }
}

void Game::checkEnemyPlayerCollision(float deltaTime) {
    if (playerDamageCooldown > 0.0f) {
        playerDamageCooldown -= deltaTime;
        return;
    }
    
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].alive) {
            // Check distance between enemy and player
            float distance = glm::length(enemies[i].position - playerPos);
            if (distance < 1.0f) { // Damage radius of 1 unit
                // Damage player
                playerHealth -= 10.0f; // 10 damage per hit
                playerDamageCooldown = 1.0f; // 1 second cooldown
                std::cout << "Player hit! Health: " << playerHealth << "/" << playerMaxHealth << std::endl;
                
                if (playerHealth <= 0.0f) {
                    playerHealth = 0.0f;
                    std::cout << "Player defeated!" << std::endl;
                    // Could add game over logic here
                }
                break; // Only one enemy can damage per frame
            }
        }
    }
}
*/

bool Game::checkProjectileEnemyCollision(Projectile& projectile) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].alive) {
            // Check distance between projectile and enemy
            glm::vec3 enemyPos(enemies[i].x, enemies[i].y, enemies[i].z);
            float distance = glm::length(projectile.position - enemyPos);
            if (distance < 0.5f) { // Hit radius of 0.5 units
                // Damage the enemy
                enemies[i].health -= 25.0f; // 25 damage per hit
                std::cout << "Enemy hit! Health: " << enemies[i].health << std::endl;
                
                if (enemies[i].health <= 0.0f) {
                    enemies[i].alive = false;
                    enemies[i].active = false;
                    activeEnemies--;
                    std::cout << "Enemy defeated!" << std::endl;
                }
                return true; // Hit detected
            }
        }
    }
    return false; // No hit
}

void Game::checkEnemyPlayerCollision(float deltaTime) {
    if (playerDamageCooldown > 0.0f) {
        playerDamageCooldown -= deltaTime;
        return;
    }
    
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].alive) {
            // Check distance between enemy and player
            glm::vec3 enemyPos(enemies[i].x, enemies[i].y, enemies[i].z);
            float distance = glm::length(enemyPos - playerPos);
            if (distance < 1.5f) { // Damage radius of 1.5 units
                // Damage player
                playerHealth -= 10.0f; // 10 damage per hit
                playerDamageCooldown = 1.0f; // 1 second cooldown
                damageEffectTime = 0.5f; // 0.5 second screen effect
                std::cout << "Player hit! Health: " << playerHealth << "/" << playerMaxHealth << std::endl;
                
                if (playerHealth <= 0.0f && !isDead) {
                    playerHealth = 0.0f;
                    isDead = true;
                    gameState = DEAD;
                    std::cout << "Player defeated!" << std::endl;
                }
                break; // Only one enemy can damage per frame
            }
        }
    }
}

void Game::updateEnemies(float deltaTime) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].alive) {
            // Calculate direction to player
            glm::vec3 enemyPos(enemies[i].x, enemies[i].y, enemies[i].z);
            glm::vec3 direction = playerPos - enemyPos;
            float distance = glm::length(direction);
            
            if (distance > 0.1f) { // Avoid division by zero
                // Normalize direction and move towards player (2.0 units per second)
                direction = glm::normalize(direction);
                glm::vec3 velocity = direction * 2.0f * deltaTime;
                enemies[i].x += velocity.x;
                enemies[i].y += velocity.y;
                enemies[i].z += velocity.z;
            }
        }
    }
}

void Game::renderEnemies() {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].alive) {
            // Create a larger red cube for the enemy
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(enemies[i].x, enemies[i].y, enemies[i].z));
            model = glm::scale(model, glm::vec3(0.8f, 1.2f, 0.8f)); // Much larger size
            renderer.drawCube(model, glm::vec3(1.0f, 0.0f, 0.0f)); // Red color
        }
    }
}

void Game::renderCrosshair() {
    setVirtualUIProjection(renderer, window);
    
    // Draw crosshair in center of screen
    float crosshairSize = 25.0f;
    float centerX = 400.0f; // Half of window width
    float centerY = 300.0f; // Half of window height
    float thickness = 3.0f; // Thicker for better visibility
    
    // Horizontal line
    glm::mat4 horizontalModel = glm::mat4(1.0f);
    horizontalModel = glm::translate(horizontalModel, glm::vec3(centerX, centerY, 0.0f));
    horizontalModel = glm::scale(horizontalModel, glm::vec3(crosshairSize, thickness, 1.0f));
    renderer.drawColoredQuad(horizontalModel, glm::vec3(1.0f, 1.0f, 1.0f)); // White
    
    // Vertical line
    glm::mat4 verticalModel = glm::mat4(1.0f);
    verticalModel = glm::translate(verticalModel, glm::vec3(centerX, centerY, 0.0f));
    verticalModel = glm::scale(verticalModel, glm::vec3(thickness, crosshairSize, 1.0f));
    renderer.drawColoredQuad(verticalModel, glm::vec3(1.0f, 1.0f, 1.0f)); // White
}

void Game::renderDamageEffect() {
    if (damageEffectTime > 0.0f) {
        setVirtualUIProjection(renderer, window);

        float left, right, top, bottom;
        getVirtualUIBounds(window, left, right, top, bottom);
        float centerY = (top + bottom) * 0.5f;
        float h = (bottom - top);
        
        // Calculate damage effect alpha (fade out over time)
        float alpha = damageEffectTime / 0.5f; // 0.5 second duration
        
        // Draw subtle red side overlays for damage effect
        float sideWidth = 150.0f; // Width of side effects
        float sideAlpha = alpha * 0.3f; // Much more subtle
        
        // Left side red tint
        glm::mat4 leftModel = glm::mat4(1.0f);
        leftModel = glm::translate(leftModel, glm::vec3(left + sideWidth/2.0f, centerY, 0.0f));
        leftModel = glm::scale(leftModel, glm::vec3(sideWidth, h, 1.0f));
        glm::vec3 leftColor = glm::vec3(0.8f, 0.0f, 0.0f) * sideAlpha; // Subtle red
        renderer.drawColoredQuad(leftModel, leftColor);
        
        // Right side red tint
        glm::mat4 rightModel = glm::mat4(1.0f);
        rightModel = glm::translate(rightModel, glm::vec3(right - sideWidth/2.0f, centerY, 0.0f));
        rightModel = glm::scale(rightModel, glm::vec3(sideWidth, h, 1.0f));
        glm::vec3 rightColor = glm::vec3(0.8f, 0.0f, 0.0f) * sideAlpha; // Subtle red
        renderer.drawColoredQuad(rightModel, rightColor);
    }
}

void Game::renderDeathScreen() {
    // Render death screen with red background
    glm::mat4 backgroundModel = glm::mat4(1.0f);
    backgroundModel = glm::translate(backgroundModel, glm::vec3(400.0f, 300.0f, 0.0f));
    backgroundModel = glm::scale(backgroundModel, glm::vec3(800.0f, 600.0f, 1.0f));
    renderer.drawColoredQuad(backgroundModel, glm::vec3(0.5f, 0.0f, 0.0f)); // Dark red background
    
    // Render "YOU DIED" text (using colored quads)
    float textY = 250.0f;
    float textScale = 40.0f;
    float spacing = 50.0f;
    
    // Simple representation of "YOU DIED" using colored blocks
    glm::vec3 textColor = glm::vec3(1.0f, 1.0f, 1.0f); // White text
    
    // Y
    glm::mat4 y1 = glm::mat4(1.0f);
    y1 = glm::translate(y1, glm::vec3(200.0f, textY, 0.0f));
    y1 = glm::scale(y1, glm::vec3(textScale, textScale * 3, 1.0f));
    renderer.drawColoredQuad(y1, textColor);
    
    glm::mat4 y2 = glm::mat4(1.0f);
    y2 = glm::translate(y2, glm::vec3(200.0f + textScale, textY + textScale * 2, 0.0f));
    y2 = glm::scale(y2, glm::vec3(textScale * 2, textScale, 1.0f));
    renderer.drawColoredQuad(y2, textColor);
    
    // O
    glm::mat4 o = glm::mat4(1.0f);
    o = glm::translate(o, glm::vec3(200.0f + spacing * 2, textY + textScale, 0.0f));
    o = glm::scale(o, glm::vec3(textScale * 2, textScale * 2, 1.0f));
    renderer.drawColoredQuad(o, textColor);
    
    // U
    glm::mat4 u1 = glm::mat4(1.0f);
    u1 = glm::translate(u1, glm::vec3(200.0f + spacing * 4, textY, 0.0f));
    u1 = glm::scale(u1, glm::vec3(textScale, textScale * 3, 1.0f));
    renderer.drawColoredQuad(u1, textColor);
    
    glm::mat4 u2 = glm::mat4(1.0f);
    u2 = glm::translate(u2, glm::vec3(200.0f + spacing * 5, textY, 0.0f));
    u2 = glm::scale(u2, glm::vec3(textScale, textScale * 3, 1.0f));
    renderer.drawColoredQuad(u2, textColor);
    
    glm::mat4 u3 = glm::mat4(1.0f);
    u3 = glm::translate(u3, glm::vec3(200.0f + spacing * 4, textY + textScale * 2, 0.0f));
    u3 = glm::scale(u3, glm::vec3(textScale * 2, textScale, 1.0f));
    renderer.drawColoredQuad(u3, textColor);
    
    // D
    glm::mat4 d1 = glm::mat4(1.0f);
    d1 = glm::translate(d1, glm::vec3(200.0f + spacing * 6.5f, textY, 0.0f));
    d1 = glm::scale(d1, glm::vec3(textScale, textScale * 3, 1.0f));
    renderer.drawColoredQuad(d1, textColor);
    
    glm::mat4 d2 = glm::mat4(1.0f);
    d2 = glm::translate(d2, glm::vec3(200.0f + spacing * 7.5f, textY, 0.0f));
    d2 = glm::scale(d2, glm::vec3(textScale, textScale * 2, 1.0f));
    renderer.drawColoredQuad(d2, textColor);
    
    glm::mat4 d3 = glm::mat4(1.0f);
    d3 = glm::translate(d3, glm::vec3(200.0f + spacing * 7.5f, textY + textScale * 2, 0.0f));
    d3 = glm::scale(d3, glm::vec3(textScale, textScale, 1.0f));
    renderer.drawColoredQuad(d3, textColor);
    
    glm::mat4 d4 = glm::mat4(1.0f);
    d4 = glm::translate(d4, glm::vec3(200.0f + spacing * 8.5f, textY + textScale, 0.0f));
    d4 = glm::scale(d4, glm::vec3(textScale, textScale, 1.0f));
    renderer.drawColoredQuad(d4, textColor);
    
    // I
    glm::mat4 i = glm::mat4(1.0f);
    i = glm::translate(i, glm::vec3(200.0f + spacing * 10, textY, 0.0f));
    i = glm::scale(i, glm::vec3(textScale, textScale * 3, 1.0f));
    renderer.drawColoredQuad(i, textColor);
    
    // E
    glm::mat4 e1 = glm::mat4(1.0f);
    e1 = glm::translate(e1, glm::vec3(200.0f + spacing * 11.5f, textY, 0.0f));
    e1 = glm::scale(e1, glm::vec3(textScale * 2, textScale, 1.0f));
    renderer.drawColoredQuad(e1, textColor);
    
    glm::mat4 e2 = glm::mat4(1.0f);
    e2 = glm::translate(e2, glm::vec3(200.0f + spacing * 11.5f, textY + textScale, 0.0f));
    e2 = glm::scale(e2, glm::vec3(textScale * 1.5f, textScale, 1.0f));
    renderer.drawColoredQuad(e2, textColor);
    
    glm::mat4 e3 = glm::mat4(1.0f);
    e3 = glm::translate(e3, glm::vec3(200.0f + spacing * 11.5f, textY + textScale * 2, 0.0f));
    e3 = glm::scale(e3, glm::vec3(textScale * 2, textScale, 1.0f));
    renderer.drawColoredQuad(e3, textColor);
    
    // D
    glm::mat4 d5 = glm::mat4(1.0f);
    d5 = glm::translate(d5, glm::vec3(200.0f + spacing * 13.5f, textY, 0.0f));
    d5 = glm::scale(d5, glm::vec3(textScale, textScale * 3, 1.0f));
    renderer.drawColoredQuad(d5, textColor);
    
    glm::mat4 d6 = glm::mat4(1.0f);
    d6 = glm::translate(d6, glm::vec3(200.0f + spacing * 14.5f, textY, 0.0f));
    d6 = glm::scale(d6, glm::vec3(textScale, textScale * 2, 1.0f));
    renderer.drawColoredQuad(d6, textColor);
    
    glm::mat4 d7 = glm::mat4(1.0f);
    d7 = glm::translate(d7, glm::vec3(200.0f + spacing * 14.5f, textY + textScale * 2, 0.0f));
    d7 = glm::scale(d7, glm::vec3(textScale, textScale, 1.0f));
    renderer.drawColoredQuad(d7, textColor);
    
    glm::mat4 d8 = glm::mat4(1.0f);
    d8 = glm::translate(d8, glm::vec3(200.0f + spacing * 15.5f, textY + textScale, 0.0f));
    d8 = glm::scale(d8, glm::vec3(textScale, textScale, 1.0f));
    renderer.drawColoredQuad(d8, textColor);
}

void Game::resetGame() {
    // Reset player health and position
    playerHealth = playerMaxHealth;
    playerDamageCooldown = 0.0f;
    damageEffectTime = 0.0f;
    isDead = false;
    
    // Reset player position
    playerPos = glm::vec3(0.0f, 2.0f, 0.0f);
    playerVelocity = glm::vec3(0.0f, 0.0f, 0.0f);
    
    // Check if player spawned inside a block, if so move up until free
    if (currentMap) {
        int safetyCounter = 0;
        while (currentMap->checkCollision(playerPos, 0.3f) && safetyCounter < 100) {
            playerPos.y += 1.0f; // Move up by 1.0 units
            safetyCounter++;
        }
        if (safetyCounter > 0) {
            std::cout << "Spawn collision detected! Moved player up by " << (safetyCounter * 1.0f) << " units to y=" << playerPos.y << std::endl;
        } else {
            std::cout << "Player spawned safely at y=" << playerPos.y << std::endl;
        }
    }
    
    // Update camera to match player position
    camera.Position = glm::vec3(playerPos.x, playerPos.y + 1.6f, playerPos.z);
    camera.Yaw = 0.0f;  // Changed from -90.0f to 0.0f
    camera.Pitch = 0.0f;
    // Force update camera vectors immediately
    camera.updateCameraVectors();
    
    // Reset projectiles
    activeProjectiles = 0;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectiles[i].active = false;
    }
    
    // Reset bullet holes
    bulletHoleCount = 0;
    for (int i = 0; i < MAX_BULLET_HOLES; i++) {
        bulletHoles[i].lifetime = 0.0f;
    }
    
    // Reset enemies
    activeEnemies = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = false;
        enemies[i].alive = false;
    }
    
    // Reset weapon system
    currentWeapon = PISTOL;
    pistolCooldown = 0.0f;
    shotgunCooldown = 0.0f;
    grenadeCooldown = 0.0f;
    flashbangCooldown = 0.0f;
    
    // Reset ammo
    pistolAmmo = 999;
    shotgunAmmo = 24;
    grenadeCount = 3;
    flashbangCount = 2;
    
    // Reset flashbang effect
    flashEffectTime = 0.0f;
    isFlashed = false;
    
    // Respawn enemies
    spawnEnemy(glm::vec3(5.0f, -1.0f, 5.0f));
    spawnEnemy(glm::vec3(-5.0f, -1.0f, 5.0f));
    spawnEnemy(glm::vec3(5.0f, -1.0f, -5.0f));
    
    // Change game state back to playing
    gameState = PLAYING;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    
    std::cout << "Game reset! Health: " << playerHealth << "/" << playerMaxHealth << std::endl;
}

void Game::initKeyBindings() {
    keyBindings.forward = GLFW_KEY_W;
    keyBindings.backward = GLFW_KEY_S;
    keyBindings.left = GLFW_KEY_A;
    keyBindings.right = GLFW_KEY_D;
    keyBindings.jump = GLFW_KEY_SPACE;
    keyBindings.menu = GLFW_KEY_ESCAPE;
    keyBindings.shoot = GLFW_MOUSE_BUTTON_LEFT; // Left mouse button to shoot
}

void Game::renderMenu() {
    // Render menu options as colored rectangles with text
    float menuWidth = 300.0f;
    float menuHeight = 50.0f;
    float menuX = (800.0f - menuWidth) / 2.0f; // Center horizontally
    float startY = 150.0f;
    float spacing = 70.0f;
    
    // Menu items: Play Game, Map Selection, Map Editor, Connect to Server, Settings, Exit
    // Note: Map Editor is hidden when connected to server
    const char* allMenuItems[] = {"Play Game", "Map Selection", "Map Editor", "Connect to Server", "Settings", "Exit"};
    glm::vec3 allColors[] = {
        glm::vec3(0.2f, 0.8f, 0.2f), // Green for Play
        glm::vec3(0.2f, 0.4f, 0.8f), // Blue for Map Selection
        glm::vec3(0.2f, 0.8f, 0.8f), // Cyan for Map Editor
        glm::vec3(0.8f, 0.2f, 0.8f), // Purple for Connect to Server
        glm::vec3(0.8f, 0.6f, 0.2f), // Orange for Settings
        glm::vec3(0.8f, 0.2f, 0.2f)  // Red for Exit
    };
    
    // Filter items based on connection status
    const char* menuItems[6];
    glm::vec3 colors[6];
    int numItems = 0;
    int editorIndex = -1;
    
    for (int i = 0; i < 6; i++) {
        // Skip Map Editor (index 2) if connected to server
        if (i == 2 && isConnectedToServer()) {
            continue;
        }
        menuItems[numItems] = allMenuItems[i];
        colors[numItems] = allColors[i];
        if (i == 2) editorIndex = numItems;
        numItems++;
    }
    
    // Adjust menuSelection if it now points to a non-existent item
    if (menuSelection >= numItems) {
        menuSelection = numItems - 1;
    }
    
    for (int i = 0; i < numItems; i++) {
        float y = startY + i * spacing;
        
        // Create model matrix for this menu item
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(menuX + menuWidth/2.0f, y + menuHeight/2.0f, 0.0f));
        model = glm::scale(model, glm::vec3(menuWidth, menuHeight, 1.0f));
        
        // Set color (brighter if selected)
        if (i == menuSelection) {
            renderer.drawColoredQuad(model, colors[i] * 1.5f); // Brighter when selected
        } else {
            renderer.drawColoredQuad(model, colors[i]);
        }
        
        // Add a visual indicator for selection - draw a small white square on the left
        if (i == menuSelection) {
            glm::mat4 indicatorModel = glm::mat4(1.0f);
            indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 15.0f, y + menuHeight/2.0f, 0.0f));
            indicatorModel = glm::scale(indicatorModel, glm::vec3(10.0f, 10.0f, 1.0f));
            renderer.drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f)); // White indicator
        }
        
        // Draw text label
        float textX = menuX + 30.0f; // Left padding after indicator
        float textY = y + menuHeight/2.0f - 6.0f; // Center vertically
        renderer.drawText(menuItems[i], textX, textY, 1.0f);
    }
    
    // Show multiplayer status if connected
    if (isConnectedToServer()) {
        glm::mat4 statusBg = glm::mat4(1.0f);
        statusBg = glm::translate(statusBg, glm::vec3(400.0f, 520.0f, 0.0f));
        statusBg = glm::scale(statusBg, glm::vec3(300.0f, 30.0f, 1.0f));
        renderer.drawColoredQuad(statusBg, glm::vec3(0.0f, 0.8f, 0.0f)); // Green background
        
        std::string statusText = "CONNECTED - " + std::to_string(playerCount) + " PLAYERS";
        float textWidth = statusText.length() * 8.0f;
        renderer.drawText(statusText.c_str(), 400.0f - textWidth/2.0f, 515.0f, 1.0f);
    }
    
    // Only print to console when selection actually changes (to reduce spam)
    static int lastMenuSelection = -1;
    if (menuSelection != lastMenuSelection) {
        std::cout << "\n=== MAIN MENU ===" << std::endl;
        std::cout << (menuSelection == 0 ? "> " : "  ") << "Play Game" << std::endl;
        std::cout << (menuSelection == 1 ? "> " : "  ") << "Map Selection" << std::endl;
        std::cout << (menuSelection == 2 ? "> " : "  ") << "Map Editor" << std::endl;
        std::cout << (menuSelection == 3 ? "> " : "  ") << "Connect to Server" << std::endl;
        std::cout << (menuSelection == 4 ? "> " : "  ") << "Settings" << std::endl;
        std::cout << (menuSelection == 5 ? "> " : "  ") << "Exit" << std::endl;
        std::cout << "Use UP/DOWN to select, ENTER to confirm, ESC to resume" << std::endl;
        lastMenuSelection = menuSelection;
    }
}

void Game::renderSettings() {
    // Render settings options as colored rectangles with visual indicators
    float menuWidth = 400.0f;
    float menuHeight = 40.0f;
    float menuX = (800.0f - menuWidth) / 2.0f; // Center horizontally
    float startY = 150.0f;
    float spacing = 50.0f;
    
    // Settings items: Forward, Backward, Left, Right, Jump, Menu, Infinite Ammo
    const char* settingItems[] = {"Forward", "Backward", "Left", "Right", "Jump", "Menu", "Infinite Ammo"};
    const int numSettings = 7;
    
    // Safe key name retrieval with null checks
    const char* keyNames[7];
    keyNames[0] = glfwGetKeyName(keyBindings.forward, 0) ? glfwGetKeyName(keyBindings.forward, 0) : "Unknown";
    keyNames[1] = glfwGetKeyName(keyBindings.backward, 0) ? glfwGetKeyName(keyBindings.backward, 0) : "Unknown";
    keyNames[2] = glfwGetKeyName(keyBindings.left, 0) ? glfwGetKeyName(keyBindings.left, 0) : "Unknown";
    keyNames[3] = glfwGetKeyName(keyBindings.right, 0) ? glfwGetKeyName(keyBindings.right, 0) : "Unknown";
    keyNames[4] = glfwGetKeyName(keyBindings.jump, 0) ? glfwGetKeyName(keyBindings.jump, 0) : "Unknown";
    keyNames[5] = glfwGetKeyName(keyBindings.menu, 0) ? glfwGetKeyName(keyBindings.menu, 0) : "Unknown";
    keyNames[6] = infiniteAmmoMode ? "ON" : "OFF"; // Infinite ammo status
    
    // Render each setting option
    for (int i = 0; i < numSettings; i++) {
        float y = startY + i * spacing;
        
        // Create background rectangle for this setting
        glm::mat4 bgModel = glm::mat4(1.0f);
        bgModel = glm::translate(bgModel, glm::vec3(menuX + menuWidth/2.0f, y + menuHeight/2.0f, 0.0f));
        bgModel = glm::scale(bgModel, glm::vec3(menuWidth, menuHeight, 1.0f));
        
        // Use different colors based on setting type and selection
        glm::vec3 bgColor;
        if (i == settingsSelection) {
            bgColor = glm::vec3(0.6f, 0.8f, 0.2f); // Bright green when selected
        } else if (i == 6) { // Infinite ammo - special color
            bgColor = infiniteAmmoMode ? glm::vec3(0.8f, 0.8f, 0.2f) : glm::vec3(0.4f, 0.4f, 0.4f);
        } else {
            bgColor = glm::vec3(0.3f, 0.5f, 0.7f); // Blue for controls
        }
        
        renderer.drawColoredQuad(bgModel, bgColor);
        
        // Draw setting name and key
        float textX = menuX + 20.0f;
        renderer.drawText(settingItems[i], textX, y + menuHeight/2.0f - 8.0f, 1.2f);
        
        // Draw key assignment on the right
        const char* keyText = keyNames[i];
        float keyTextWidth = strlen(keyText) * 8.0f; // Approximate width
        renderer.drawText(keyText, menuX + menuWidth - keyTextWidth - 20.0f, y + menuHeight/2.0f - 8.0f, 1.2f);
        
        // Draw selection indicator
        if (i == settingsSelection) {
            glm::mat4 indicatorModel = glm::mat4(1.0f);
            indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 10.0f, y + menuHeight/2.0f, 0.0f));
            indicatorModel = glm::scale(indicatorModel, glm::vec3(8.0f, 8.0f, 1.0f));
            renderer.drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
        }
    }
    
    // Only print to console when selection actually changes (to reduce spam)
    static int lastSettingsSelection = -1;
    if (settingsSelection != lastSettingsSelection) {
        std::cout << "\n=== SETTINGS ===" << std::endl;
        std::cout << (settingsSelection == 0 ? "> " : "  ") << "Forward: " << keyNames[0] << std::endl;
        std::cout << (settingsSelection == 1 ? "> " : "  ") << "Backward: " << keyNames[1] << std::endl;
        std::cout << (settingsSelection == 2 ? "> " : "  ") << "Left: " << keyNames[2] << std::endl;
        std::cout << (settingsSelection == 3 ? "> " : "  ") << "Right: " << keyNames[3] << std::endl;
        std::cout << (settingsSelection == 4 ? "> " : "  ") << "Jump: " << keyNames[4] << std::endl;
        std::cout << (settingsSelection == 5 ? "> " : "  ") << "Menu: " << keyNames[5] << std::endl;
        std::cout << "Use UP/DOWN to select, ENTER to change, ESC to go back" << std::endl;
        lastSettingsSelection = settingsSelection;
    }
}

void Game::saveKeyBindings() {
    // Save to file (simplified version)
    std::cout << "Key bindings saved!" << std::endl;
}

void Game::loadKeyBindings() {
    // Load from file (simplified version)
    std::cout << "Key bindings loaded!" << std::endl;
}

void Game::processSettingsInput() {
    static bool upPressed = false;
    static bool downPressed = false;
    static bool enterPressed = false;
    static bool escapePressed = false;
    
    // Handle UP key
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !upPressed) {
        upPressed = true;
        settingsSelection = (settingsSelection - 1 + 7) % 7;
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        upPressed = false;
    }
    
    // Handle DOWN key
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !downPressed) {
        downPressed = true;
        settingsSelection = (settingsSelection + 1) % 7;
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        downPressed = false;
    }
    
    // Handle ENTER key
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterPressed) {
        enterPressed = true;
        
        // Special handling for infinite ammo toggle
        if (settingsSelection == 6) {
            infiniteAmmoMode = !infiniteAmmoMode;
            pistol->setInfiniteAmmo(infiniteAmmoMode);
            shotgun->setInfiniteAmmo(infiniteAmmoMode);
            grenade->setInfiniteAmmo(infiniteAmmoMode);
            flashbang->setInfiniteAmmo(infiniteAmmoMode);
            std::cout << "Infinite ammo mode: " << (infiniteAmmoMode ? "ON" : "OFF") << std::endl;
        } else {
            // Wait for key release and capture new key
            glfwWaitEvents();
            while (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS) {
                glfwWaitEvents();
            }
            
            int newKey = -1;
            for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; key++) {
                if (glfwGetKey(window, key) == GLFW_PRESS) {
                    newKey = key;
                    break;
                }
            }
            
            if (newKey != -1) {
                switch (settingsSelection) {
                    case 0: keyBindings.forward = newKey; break;
                    case 1: keyBindings.backward = newKey; break;
                    case 2: keyBindings.left = newKey; break;
                    case 3: keyBindings.right = newKey; break;
                    case 4: keyBindings.jump = newKey; break;
                    case 5: keyBindings.menu = newKey; break;
                }
                saveKeyBindings();
            }
        }
    } else if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
        enterPressed = false;
    }
    
    // Handle ESC key
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !escapePressed) {
        escapePressed = true;
        gameState = MENU;
        menuSelection = 0;
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
        escapePressed = false;
    }
}

void Game::renderMapSelection() {
    // Clear screen with dark background
    renderer.clear();
    
    setVirtualUIProjection(renderer, window);
    
    // Title bar - orange like settings
    glm::mat4 titleBg = glm::mat4(1.0f);
    titleBg = glm::translate(titleBg, glm::vec3(400.0f, 80.0f, 0.0f));
    titleBg = glm::scale(titleBg, glm::vec3(600.0f, 60.0f, 1.0f));
    renderer.drawColoredQuad(titleBg, glm::vec3(0.8f, 0.4f, 0.1f));
    renderer.drawText("SELECT MAP", 340.0f, 75.0f, 1.5f);
    
    // Map data - styled exactly like settings menu
    const char* mapNames[] = {"Simple", "Large", "Arena", "Sniper", "Complex"};
    const char* mapSizes[] = {"Small", "Large", "Medium", "Long Range", "Multi-Level"};
    const int numMaps = 5;
    
    float menuWidth = 400.0f;
    float menuHeight = 40.0f;
    float menuX = (800.0f - menuWidth) / 2.0f;
    float startY = 150.0f;
    float spacing = 50.0f;
    
    // Draw map boxes like settings items
    for (int i = 0; i < numMaps; i++) {
        float y = startY + i * spacing;
        bool isSelected = (i == mapSelection);
        
        // Create background rectangle for this map
        glm::mat4 bgModel = glm::mat4(1.0f);
        bgModel = glm::translate(bgModel, glm::vec3(menuX + menuWidth/2.0f, y + menuHeight/2.0f, 0.0f));
        bgModel = glm::scale(bgModel, glm::vec3(menuWidth, menuHeight, 1.0f));
        
        // Use blue for normal, green for selected (matching settings style)
        glm::vec3 bgColor = isSelected ? glm::vec3(0.6f, 0.8f, 0.2f) : glm::vec3(0.3f, 0.5f, 0.7f);
        
        renderer.drawColoredQuad(bgModel, bgColor);
        
        // Draw map name on the left
        renderer.drawText(mapNames[i], menuX + 20.0f, y + menuHeight/2.0f - 8.0f, 1.2f);
        
        // Draw map size/type on the right
        float valueWidth = strlen(mapSizes[i]) * 8.0f;
        renderer.drawText(mapSizes[i], menuX + menuWidth - valueWidth - 20.0f, y + menuHeight/2.0f - 8.0f, 1.2f);
        
        // Draw selection indicator
        if (isSelected) {
            glm::mat4 indicatorModel = glm::mat4(1.0f);
            indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 10.0f, y + menuHeight/2.0f, 0.0f));
            indicatorModel = glm::scale(indicatorModel, glm::vec3(8.0f, 8.0f, 1.0f));
            renderer.drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
        }
    }
    
    // Instructions at bottom
    renderer.drawText("UP/DOWN = change, ENTER = load, ESC = back", 250.0f, 580.0f, 1.0f);
    
    // Only print to console when selection actually changes (to reduce spam)
    static int lastMapSelection = -1;
    if (mapSelection != lastMapSelection) {
        std::cout << "\n=== MAP SELECTION ===" << std::endl;
        for (int i = 0; i < numMaps; i++) {
            std::cout << (i == mapSelection ? "> " : "  ") << mapNames[i] << " - " << mapSizes[i] << std::endl;
        }
        std::cout << "Use UP/DOWN to select, ENTER to load, ESC to go back" << std::endl;
        lastMapSelection = mapSelection;
    }
}

void Game::processMapSelectionInput() {
    static bool upPressed = false;
    static bool downPressed = false;
    static bool enterPressed = false;
    static bool escapePressed = false;
    
    const int numMaps = 5;
    const char* mapFiles[] = {"simple.map", "large.map", "arena.map", "sniper.map", "complex.map"};
    
    // Handle UP key
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !upPressed) {
        upPressed = true;
        mapSelection = (mapSelection - 1 + numMaps) % numMaps;
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        upPressed = false;
    }
    
    // Handle DOWN key
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !downPressed) {
        downPressed = true;
        mapSelection = (mapSelection + 1) % numMaps;
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        downPressed = false;
    }
    
    // Handle ENTER key
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterPressed) {
        enterPressed = true;
        std::cout << "Loading " << mapFiles[mapSelection] << "..." << std::endl;
        loadMap(mapFiles[mapSelection]);
        gameState = PLAYING;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    } else if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
        enterPressed = false;
    }
    
    // Handle ESC key - go back to menu
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !escapePressed) {
        escapePressed = true;
        gameState = MENU;
        menuSelection = 1;
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
        escapePressed = false;
    }
}

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
                    remotePlayers[i].id = packet.playerId;
                    remotePlayers[i].position = glm::vec3(packet.posX, packet.posY, packet.posZ);
                    remotePlayers[i].velocity = glm::vec3(packet.velX, packet.velY, packet.velZ);
                    remotePlayers[i].yaw = packet.yaw;
                    remotePlayers[i].pitch = packet.pitch;
                    remotePlayers[i].health = packet.health;
                    remotePlayers[i].isDead = packet.isDead;
                    remotePlayers[i].active = true;
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
        
        std::cout << "Player " << packet.playerId << " joined the game!" << std::endl;
        // Add new remote player
        for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
            if (!remotePlayers[i].active) {
                remotePlayers[i].id = packet.playerId;
                remotePlayers[i].position = glm::vec3(packet.spawnX, packet.spawnY, packet.spawnZ);
                remotePlayers[i].active = true;
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
                remotePlayers[i].active = false;
                playerCount--; // Update player count
                break;
            }
        }
    };
    
    networkClient->onProjectileSpawn = [this](const ProjectileSpawnPacket& packet) {
        // Create projectile from other player
        createProjectile(
            glm::vec3(packet.posX, packet.posY, packet.posZ),
            glm::vec3(packet.velX, packet.velY, packet.velZ),
            packet.lifetime,
            packet.ownerId // Owner is the player who fired it
        );
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
    
    // Clear remote players
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        remotePlayers[i].active = false;
    }
}

bool Game::isConnectedToServer() const {
    return networkClient && networkClient->isConnected();
}

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

void ensureDiscoverySocket(socket_t& sock) {
    if (sock != SOCKET_INVALID) return;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == SOCKET_INVALID) return;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    setNonBlocking(sock);
}

static void sendDiscoverPing(socket_t sock, uint16_t port) {
    if (sock == SOCKET_INVALID) return;

    DiscoverPacket p{};
    p.header.magic = PACKET_MAGIC;
    p.header.type = PACKET_DISCOVER;
    p.header.sequence = 0;
    p.header.timestamp = 0;
    p.header.clientId = 0;

    // Localhost (fixes "doesn't find server on my PC")
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &local.sin_addr);
#ifdef _WIN32
    sendto(sock, (const char*)&p, sizeof(p), 0, (sockaddr*)&local, sizeof(local));
#else
    sendto(sock, &p, sizeof(p), 0, (sockaddr*)&local, sizeof(local));
#endif

    // Broadcast
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = inet_addr("255.255.255.255");
#ifdef _WIN32
    sendto(sock, (const char*)&p, sizeof(p), 0, (sockaddr*)&to, sizeof(to));
#else
    sendto(sock, &p, sizeof(p), 0, (sockaddr*)&to, sizeof(to));
#endif
}

static void setVirtualUIProjection(Renderer& renderer, GLFWwindow* window) {
    // Prevent UI stretching: keep 800x600 as "design space" and expand the
    // visible virtual area on wider/taller screens (no borders).
    int fbW = 800, fbH = 600;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = 800;
    if (fbH <= 0) fbH = 600;

    float scaleX = (float)fbW / 800.0f;
    float scaleY = (float)fbH / 600.0f;
    float uiScale = std::min(scaleX, scaleY);
    if (uiScale <= 0.0f) uiScale = 1.0f;

    float uiW = (float)fbW / uiScale;
    float uiH = (float)fbH / uiScale;
    float offsetX = (uiW - 800.0f) * 0.5f;
    float offsetY = (uiH - 600.0f) * 0.5f;

    glm::mat4 projection = glm::ortho(-offsetX, 800.0f + offsetX,
                                      600.0f + offsetY, -offsetY,
                                      -1.0f, 1.0f);
    glm::mat4 view = glm::mat4(1.0f);
    renderer.setViewProjection(view, projection);
}

static void getVirtualUIBounds(GLFWwindow* window, float& left, float& right, float& top, float& bottom) {
    int fbW = 800, fbH = 600;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = 800;
    if (fbH <= 0) fbH = 600;

    float scaleX = (float)fbW / 800.0f;
    float scaleY = (float)fbH / 600.0f;
    float uiScale = std::min(scaleX, scaleY);
    if (uiScale <= 0.0f) uiScale = 1.0f;

    float uiW = (float)fbW / uiScale;
    float uiH = (float)fbH / uiScale;
    float offsetX = (uiW - 800.0f) * 0.5f;
    float offsetY = (uiH - 600.0f) * 0.5f;

    left = -offsetX;
    right = 800.0f + offsetX;
    top = -offsetY;
    bottom = 600.0f + offsetY;
}

// ========== SERVER CONNECTION MENU ==========

void Game::renderServerConnect() {
    // Clear screen
    renderer.clear();
    
    setVirtualUIProjection(renderer, window);
    
    // Title bar - orange like settings
    glm::mat4 titleBg = glm::mat4(1.0f);
    titleBg = glm::translate(titleBg, glm::vec3(400.0f, 80.0f, 0.0f));
    titleBg = glm::scale(titleBg, glm::vec3(600.0f, 60.0f, 1.0f));
    renderer.drawColoredQuad(titleBg, glm::vec3(0.8f, 0.4f, 0.1f));
    renderer.drawText("CONNECT TO SERVER", 280.0f, 75.0f, 1.5f);
    
    float menuWidth = 400.0f;
    float menuHeight = 40.0f;
    float menuX = (800.0f - menuWidth) / 2.0f;
    float startY = 150.0f;
    float spacing = 50.0f;
    
    // Server address input box - styled like settings menu
    float addressY = startY;
    glm::mat4 addressBox = glm::mat4(1.0f);
    addressBox = glm::translate(addressBox, glm::vec3(menuX + menuWidth/2.0f, addressY + menuHeight/2.0f, 0.0f));
    addressBox = glm::scale(addressBox, glm::vec3(menuWidth, menuHeight, 1.0f));
    
    // Blue when not selected, green when selected (entering address)
    glm::vec3 addressColor = (enteringServerAddress && !focusServerList) ? 
        glm::vec3(0.6f, 0.8f, 0.2f) : glm::vec3(0.3f, 0.5f, 0.7f);
    renderer.drawColoredQuad(addressBox, addressColor);
    
    // Draw server address label and value - smaller text, better positioning
    renderer.drawText("Server IP:", menuX + 25.0f, addressY + menuHeight/2.0f - 8.0f, 0.8f);
    std::string ipText = serverAddress.empty() ? std::string("192.168.1.50") : serverAddress;
    float ipWidth = ipText.length() * 6.0f; // Smaller font width
    renderer.drawText(ipText.c_str(), menuX + menuWidth - ipWidth - 25.0f, addressY + menuHeight/2.0f - 8.0f, 0.8f);
    
    // Selection indicator for address
    if (serverConnectSelection == 0) {
        glm::mat4 indicatorModel = glm::mat4(1.0f);
        indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 15.0f, addressY + menuHeight/2.0f, 0.0f));
        indicatorModel = glm::scale(indicatorModel, glm::vec3(8.0f, 8.0f, 1.0f));
        renderer.drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
    }
    
    // Port input box - styled like settings menu
    float portY = startY + spacing;
    glm::mat4 portBox = glm::mat4(1.0f);
    portBox = glm::translate(portBox, glm::vec3(menuX + menuWidth/2.0f, portY + menuHeight/2.0f, 0.0f));
    portBox = glm::scale(portBox, glm::vec3(menuWidth, menuHeight, 1.0f));
    
    // Green if selected (row 1), blue otherwise
    glm::vec3 portColor = (serverConnectSelection == 1) ? 
        glm::vec3(0.6f, 0.8f, 0.2f) : glm::vec3(0.3f, 0.5f, 0.7f);
    renderer.drawColoredQuad(portBox, portColor);
    
    // Draw port label and value - smaller text, better positioning
    renderer.drawText("Port:", menuX + 25.0f, portY + menuHeight/2.0f - 8.0f, 0.8f);
    std::string portStr = std::to_string(serverPort);
    float portWidth = portStr.length() * 6.0f; // Smaller font width
    renderer.drawText(portStr.c_str(), menuX + menuWidth - portWidth - 25.0f, portY + menuHeight/2.0f - 8.0f, 0.8f);
    
    // Selection indicator for port
    if (serverConnectSelection == 1) {
        glm::mat4 indicatorModel = glm::mat4(1.0f);
        indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 15.0f, portY + menuHeight/2.0f, 0.0f));
        indicatorModel = glm::scale(indicatorModel, glm::vec3(8.0f, 8.0f, 1.0f));
        renderer.drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
    }
    
    // Discovered servers list - styled like settings items
    float listY = startY + 2 * spacing;
    renderer.drawText("FOUND SERVERS (LAN):", menuX + 25.0f, listY - 22.0f, 0.8f);
    
    if (discoveredServers.empty()) {
        renderer.drawText("No servers found. Press R to rescan.", menuX + 25.0f, listY + 3.0f, 0.8f);
    } else {
        int maxRows = std::min(3, (int)discoveredServers.size());
        for (int i = 0; i < maxRows; i++) {
            const auto& s = discoveredServers[i];
            float serverY = listY + i * spacing;
            
            // Server item box
            glm::mat4 serverBox = glm::mat4(1.0f);
            serverBox = glm::translate(serverBox, glm::vec3(menuX + menuWidth/2.0f, serverY + menuHeight/2.0f, 0.0f));
            serverBox = glm::scale(serverBox, glm::vec3(menuWidth, menuHeight, 1.0f));
            
            // Green if selected in list, blue otherwise
            glm::vec3 serverColor = (serverConnectSelection == 2 + i) ? 
                glm::vec3(0.6f, 0.8f, 0.2f) : glm::vec3(0.3f, 0.5f, 0.7f);
            renderer.drawColoredQuad(serverBox, serverColor);
            
            // Server name and address - smaller text, better positioning
            std::string serverText = s.name + " - " + s.ip + ":" + std::to_string(s.port);
            renderer.drawText(serverText.c_str(), menuX + 25.0f, serverY + menuHeight/2.0f - 8.0f, 0.8f);
            
            // Selection indicator
            if (serverConnectSelection == 2 + i) {
                glm::mat4 indicatorModel = glm::mat4(1.0f);
                indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 15.0f, serverY + menuHeight/2.0f, 0.0f));
                indicatorModel = glm::scale(indicatorModel, glm::vec3(8.0f, 8.0f, 1.0f));
                renderer.drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
            }
        }
    }
    
    // Connect button - styled as menu item
    int connectRow = discoveredServers.empty() ? 2 : 2 + std::min(3, (int)discoveredServers.size());
    float connectY = startY + connectRow * spacing;
    glm::mat4 connectBox = glm::mat4(1.0f);
    connectBox = glm::translate(connectBox, glm::vec3(menuX + menuWidth/2.0f, connectY + menuHeight/2.0f, 0.0f));
    connectBox = glm::scale(connectBox, glm::vec3(menuWidth, menuHeight, 1.0f));
    
    if (isConnectedToServer()) {
        renderer.drawColoredQuad(connectBox, glm::vec3(0.8f, 0.2f, 0.2f)); // Red for disconnect
        renderer.drawText("DISCONNECT", menuX + 25.0f, connectY + menuHeight/2.0f - 8.0f, 0.8f);
    } else if (networkClient && networkClient->isConnecting()) {
        renderer.drawColoredQuad(connectBox, glm::vec3(0.8f, 0.8f, 0.2f)); // Yellow for connecting
        renderer.drawText("CONNECTING...", menuX + 25.0f, connectY + menuHeight/2.0f - 8.0f, 0.8f);
    } else {
        // Green if selected, blue otherwise
        glm::vec3 connectColor = (serverConnectSelection == connectRow) ?
            glm::vec3(0.6f, 0.8f, 0.2f) : glm::vec3(0.3f, 0.5f, 0.7f);
        renderer.drawColoredQuad(connectBox, connectColor);
        renderer.drawText("CONNECT", menuX + 25.0f, connectY + menuHeight/2.0f - 8.0f, 0.8f);
    }
    
    // Selection indicator for connect button
    if (serverConnectSelection == connectRow) {
        glm::mat4 indicatorModel = glm::mat4(1.0f);
        indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 15.0f, connectY + menuHeight/2.0f, 0.0f));
        indicatorModel = glm::scale(indicatorModel, glm::vec3(8.0f, 8.0f, 1.0f));
        renderer.drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
    }
    
    // Back button - styled as menu item
    int backRow = connectRow + 1;
    float backY = startY + backRow * spacing;
    glm::mat4 backBox = glm::mat4(1.0f);
    backBox = glm::translate(backBox, glm::vec3(menuX + menuWidth/2.0f, backY + menuHeight/2.0f, 0.0f));
    backBox = glm::scale(backBox, glm::vec3(menuWidth, menuHeight, 1.0f));
    
    // Green if selected, blue otherwise
    glm::vec3 backColor = (serverConnectSelection == backRow) ?
        glm::vec3(0.6f, 0.8f, 0.2f) : glm::vec3(0.3f, 0.5f, 0.7f);
    renderer.drawColoredQuad(backBox, backColor);
    renderer.drawText("BACK", menuX + 30.0f, backY + menuHeight/2.0f - 6.0f, 1.0f);
    
    // Selection indicator for back button
    if (serverConnectSelection == backRow) {
        glm::mat4 indicatorModel = glm::mat4(1.0f);
        indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 15.0f, backY + menuHeight/2.0f, 0.0f));
        indicatorModel = glm::scale(indicatorModel, glm::vec3(8.0f, 8.0f, 1.0f));
        renderer.drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
    }
    
    // Instructions at bottom
    renderer.drawText("UP/DOWN=navigate, ENTER=select/connect, TAB=input, R=rescan, ESC=back", 160.0f, 580.0f, 1.0f);
    
    // Only print to console when selection actually changes (to reduce spam)
    static int lastServerConnectSelection = -1;
    if (serverConnectSelection != lastServerConnectSelection) {
        std::cout << "\n=== SERVER CONNECTION ===" << std::endl;
        std::cout << (serverConnectSelection == 0 ? "> " : "  ") << "Server IP: " << (serverAddress.empty() ? "192.168.1.50" : serverAddress) << std::endl;
        std::cout << (serverConnectSelection == 1 ? "> " : "  ") << "Port: " << serverPort << std::endl;
        
        // Show discovered servers
        if (!discoveredServers.empty()) {
            int maxRows = std::min(3, (int)discoveredServers.size());
            for (int i = 0; i < maxRows; i++) {
                const auto& s = discoveredServers[i];
                std::cout << (serverConnectSelection == 2 + i ? "> " : "  ") << s.name << " - " << s.ip << ":" << s.port << std::endl;
            }
        } else {
            std::cout << "  No servers found. Press R to rescan." << std::endl;
        }
        
        // Show connect button
        int connectRow = discoveredServers.empty() ? 2 : 2 + std::min(3, (int)discoveredServers.size());
        if (isConnectedToServer()) {
            std::cout << (serverConnectSelection == connectRow ? "> " : "  ") << "DISCONNECT" << std::endl;
        } else if (networkClient && networkClient->isConnecting()) {
            std::cout << (serverConnectSelection == connectRow ? "> " : "  ") << "CONNECTING..." << std::endl;
        } else {
            std::cout << (serverConnectSelection == connectRow ? "> " : "  ") << "CONNECT" << std::endl;
        }
        
        // Show back button
        int backRow = connectRow + 1;
        std::cout << (serverConnectSelection == backRow ? "> " : "  ") << "BACK" << std::endl;
        std::cout << "Use UP/DOWN=navigate, ENTER=select/connect, TAB=input, R=rescan, ESC=back" << std::endl;
        lastServerConnectSelection = serverConnectSelection;
    }
}

void Game::processServerConnectInput() {
    static bool tabPressed = false;
    static bool enterPressed = false;
    static bool escapePressed = false;
    static bool backspacePressed = false;
    static bool periodPressed = false;
    static bool rPressed = false;
    static bool upPressed = false;
    static bool downPressed = false;
    static bool digitPressed[10] = {false};

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

            std::string ip = inet_ntoa(from.sin_addr);
            uint16_t port = resp->port;
            std::string name(resp->name);
            if (name.empty()) name = "Server";
            std::string key = ip + ":" + std::to_string(port);

            auto it = discoveredIndexByKey.find(key);
            if (it == discoveredIndexByKey.end()) {
                DiscoveredServer s;
                s.ip = ip;
                s.port = port;
                s.name = name;
                s.lastSeenSeconds = discoveryTimeSeconds;
                discoveredIndexByKey[key] = discoveredServers.size();
                discoveredServers.push_back(s);
            } else {
                auto& s = discoveredServers[it->second];
                s.lastSeenSeconds = discoveryTimeSeconds;
                s.name = name;
            }
        }
    }
    
    // Calculate max row based on discovered servers
    int numServerRows = discoveredServers.empty() ? 0 : std::min(3, (int)discoveredServers.size());
    int connectRow = 2 + numServerRows;
    int backRow = connectRow + 1;
    int maxRow = backRow;
    
    // Handle UP navigation
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !upPressed) {
        upPressed = true;
        serverConnectSelection = (serverConnectSelection - 1 + maxRow + 1) % (maxRow + 1);
        // Update enteringServerAddress based on selection
        enteringServerAddress = (serverConnectSelection == 0);
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        upPressed = false;
    }
    
    // Handle DOWN navigation
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !downPressed) {
        downPressed = true;
        serverConnectSelection = (serverConnectSelection + 1) % (maxRow + 1);
        // Update enteringServerAddress based on selection
        enteringServerAddress = (serverConnectSelection == 0);
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        downPressed = false;
    }

    // Handle TAB to toggle between IP and Port fields (only if on rows 0 or 1)
    if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS && !tabPressed) {
        tabPressed = true;
        if (serverConnectSelection == 0) {
            serverConnectSelection = 1;
            enteringServerAddress = false;
        } else if (serverConnectSelection == 1) {
            serverConnectSelection = 0;
            enteringServerAddress = true;
        }
    } else if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_RELEASE) {
        tabPressed = false;
    }

    // Auto-scan while on this screen (lightweight)
    if (discoverySocketFd != SOCKET_INVALID && discoverySendCooldown <= 0.0f) {
        uint16_t port = serverPort == 0 ? DEFAULT_PORT : serverPort;
        sendDiscoverPing(discoverySocketFd, port);
        discoverySendCooldown = 1.0f;
    }

    // Rescan LAN (R) - works on any row
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !rPressed) {
        rPressed = true;
        uint16_t port = serverPort == 0 ? DEFAULT_PORT : serverPort;
        sendDiscoverPing(discoverySocketFd, port);
        discoverySendCooldown = 0.75f;
    } else if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) {
        rPressed = false;
    }
    
    // Handle number keys for IP/port input (only when on address or port row)
    if (serverConnectSelection == 0 || serverConnectSelection == 1) {
        for (int key = GLFW_KEY_0; key <= GLFW_KEY_9; key++) {
            int idx = key - GLFW_KEY_0;
            if (glfwGetKey(window, key) == GLFW_PRESS && !digitPressed[idx]) {
                digitPressed[idx] = true;
                char digit = '0' + (key - GLFW_KEY_0);
                if (serverConnectSelection == 0) {
                    serverAddress += digit;
                } else if (serverConnectSelection == 1) {
                    if (serverPort < 65535) {
                        serverPort = serverPort * 10 + (digit - '0');
                    }
                }
            } else if (glfwGetKey(window, key) == GLFW_RELEASE) {
                digitPressed[idx] = false;
            }
        }
        
        // Handle period for IP address
        if (serverConnectSelection == 0 && glfwGetKey(window, GLFW_KEY_PERIOD) == GLFW_PRESS && !periodPressed) {
            periodPressed = true;
            serverAddress += '.';
        } else if (glfwGetKey(window, GLFW_KEY_PERIOD) == GLFW_RELEASE) {
            periodPressed = false;
        }

        // Also support keypad decimal for IP entry
        if (serverConnectSelection == 0 && glfwGetKey(window, GLFW_KEY_KP_DECIMAL) == GLFW_PRESS && !periodPressed) {
            periodPressed = true;
            serverAddress += '.';
        } else if (glfwGetKey(window, GLFW_KEY_KP_DECIMAL) == GLFW_RELEASE) {
            periodPressed = false;
        }
        
        // Handle backspace
        if (glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS && !backspacePressed) {
            backspacePressed = true;
            if (serverConnectSelection == 0) {
                if (!serverAddress.empty()) {
                    serverAddress.pop_back();
                }
            } else if (serverConnectSelection == 1) {
                serverPort = serverPort / 10;
            }
        } else if (glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_RELEASE) {
            backspacePressed = false;
        }
    }
    
    // Handle ENTER to connect/disconnect or select server
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterPressed) {
        enterPressed = true;
        if (serverConnectSelection >= 2 && serverConnectSelection < connectRow) {
            // Selecting a discovered server
            int serverIndex = serverConnectSelection - 2;
            if (serverIndex < (int)discoveredServers.size()) {
                const auto& s = discoveredServers[serverIndex];
                serverAddress = s.ip;
                serverPort = s.port;
                // Try to connect immediately
                if (connectToServer(serverAddress, serverPort)) {
                    std::cout << "Connected to discovered server!" << std::endl;
                }
            }
        } else if (serverConnectSelection == connectRow) {
            // Connect/Disconnect button
            if (isConnectedToServer()) {
                disconnectFromServer();
                std::cout << "Disconnected from server" << std::endl;
            } else if (connectToServer(serverAddress, serverPort)) {
                std::cout << "Connected! Press ESC to return to menu" << std::endl;
            } else {
                std::cout << "Failed to connect to " << serverAddress << ":" << serverPort << std::endl;
            }
        } else if (serverConnectSelection == backRow) {
            // Back button
            gameState = MENU;
            menuSelection = 2;
        }
    } else if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
        enterPressed = false;
    }
    
    // Handle ESC to go back to menu
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !escapePressed) {
        escapePressed = true;
        gameState = MENU;
        menuSelection = 2; // Keep on Connect to Server option
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
        escapePressed = false;
    }
}

// ========== MAP EDITOR IMPLEMENTATION ==========

void Game::renderEditor() {
    int fbW = 800, fbH = 600;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = 800;
    if (fbH <= 0) fbH = 600;
    renderer.setViewport(0, 0, fbW, fbH);
    
    renderer.clear();
    
    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom),
                                            (float)fbW / (float)fbH, 0.1f, 1000.0f);
    renderer.setViewProjection(view, projection);
    
    // Render the map
    if (currentMap) {
        renderer.renderMap(currentMap);
    }
    
    // Render grid lines on ground - grid size now matches block size
    if (editorShowGrid) {
        // Use the largest block dimension as the grid cell size
        float gridCellSize = glm::max(editorBlockSize.x, glm::max(editorBlockSize.y, editorBlockSize.z));
        for (int i = -20; i <= 20; i++) {
            // X lines
            glm::mat4 lineX = glm::mat4(1.0f);
            lineX = glm::translate(lineX, glm::vec3(i * gridCellSize, -0.05f, 0.0f));
            lineX = glm::scale(lineX, glm::vec3(0.02f, 0.02f, 40.0f * gridCellSize));
            renderer.drawCube(lineX, glm::vec3(0.3f, 0.3f, 0.3f));
            
            // Z lines
            glm::mat4 lineZ = glm::mat4(1.0f);
            lineZ = glm::translate(lineZ, glm::vec3(0.0f, -0.05f, i * gridCellSize));
            lineZ = glm::scale(lineZ, glm::vec3(40.0f * gridCellSize, 0.02f, 0.02f));
            renderer.drawCube(lineZ, glm::vec3(0.3f, 0.3f, 0.3f));
        }
    }
    
    // Render ghost block at grid position - wireframe style (edges only)
    // The ghost block should match the size of the block that will be placed EXACTLY
    {
        // The renderer scales by block.scale, so we use editorBlockSize directly
        // For the wireframe, we need half-extents (center to edge distance)
        glm::vec3 halfSize = editorBlockSize * 0.5f;
        glm::vec3 center = editorGridPos;
        float edgeThickness = 0.02f;
        glm::vec3 edgeColor = glm::vec3(0.0f, 1.0f, 0.0f); // Bright green edges
        
        // Edge lengths should match the full block size minus edge thickness
        float xEdgeLen = editorBlockSize.x - edgeThickness;
        float yEdgeLen = editorBlockSize.y - edgeThickness;
        float zEdgeLen = editorBlockSize.z - edgeThickness;
        
        // Vertical edges (4 corners) - Y direction
        for (float x = -1; x <= 1; x += 2) {
            for (float z = -1; z <= 1; z += 2) {
                glm::mat4 edge = glm::mat4(1.0f);
                edge = glm::translate(edge, center + glm::vec3(x * halfSize.x, 0.0f, z * halfSize.z));
                edge = glm::scale(edge, glm::vec3(edgeThickness, yEdgeLen, edgeThickness));
                renderer.drawCube(edge, edgeColor);
            }
        }
        // Horizontal edges at top and bottom
        for (float y = -1; y <= 1; y += 2) {
            // X-direction edges
            for (float z = -1; z <= 1; z += 2) {
                glm::mat4 edge = glm::mat4(1.0f);
                edge = glm::translate(edge, center + glm::vec3(0.0f, y * halfSize.y, z * halfSize.z));
                edge = glm::scale(edge, glm::vec3(xEdgeLen, edgeThickness, edgeThickness));
                renderer.drawCube(edge, edgeColor);
            }
            // Z-direction edges
            for (float x = -1; x <= 1; x += 2) {
                glm::mat4 edge = glm::mat4(1.0f);
                edge = glm::translate(edge, center + glm::vec3(x * halfSize.x, y * halfSize.y, 0.0f));
                edge = glm::scale(edge, glm::vec3(edgeThickness, edgeThickness, zEdgeLen));
                renderer.drawCube(edge, edgeColor);
            }
        }
    }
    
    // Render crosshair
    setVirtualUIProjection(renderer, window);
    renderCrosshair();
    
    // Render editor UI overlay - smaller text with more line spacing
    float textScale = 0.6f;
    float lineHeight = 28.0f;  // Increased spacing between lines
    float startX = 20.0f;
    float startY = 20.0f;
    
    renderer.drawText("MAP EDITOR", startX, startY, textScale);
    renderer.drawText("WASD=move, SPACE/SHIFT=up/down", startX, startY + lineHeight, textScale);
    renderer.drawText("HOLD LMB=place, RCLICK=retex, ALT+RCLICK=delete", startX, startY + lineHeight * 2, textScale);
    renderer.drawText("1-8=texture, E/Q=resize (+X/Y/Z)", startX, startY + lineHeight * 3, textScale);
    renderer.drawText("UP/DOWN=placement dist, arrows=move", startX, startY + lineHeight * 4, textScale);
    renderer.drawText("[,]=grid size, G=grid toggle, N=noclip", startX, startY + lineHeight * 5, textScale);
    renderer.drawText("F9=save, ESC=exit", startX, startY + lineHeight * 6, textScale);
    
    // Show current settings
    char buf[128];
    snprintf(buf, sizeof(buf), "POS: %.1f, %.1f, %.1f", editorGridPos.x, editorGridPos.y, editorGridPos.z);
    renderer.drawText(buf, startX, startY + lineHeight * 8, textScale);
    snprintf(buf, sizeof(buf), "SIZE: %.1f, %.1f, %.1f", editorBlockSize.x, editorBlockSize.y, editorBlockSize.z);
    renderer.drawText(buf, startX, startY + lineHeight * 9, textScale);
    snprintf(buf, sizeof(buf), "TEX: %d  DIST: %.1f", editorSelectedTexture, editorBlockOffset);
    renderer.drawText(buf, startX, startY + lineHeight * 10, textScale);
    snprintf(buf, sizeof(buf), "GRID: %s  NOCLIP: %s", editorShowGrid ? "ON" : "OFF", editorNoclip ? "ON" : "OFF");
    renderer.drawText(buf, startX, startY + lineHeight * 11, textScale);
    
    // Texture preview in bottom RIGHT corner - make it larger and more visible
    // Texture color palette (matching the map texture IDs)
    glm::vec3 textureColors[8] = {
        glm::vec3(0.4f, 0.8f, 0.4f),  // 0 = grass (green)
        glm::vec3(0.6f, 0.6f, 0.6f),  // 1 = stone (gray)
        glm::vec3(0.8f, 0.4f, 0.3f),  // 2 = brick (red-brown)
        glm::vec3(0.5f, 0.5f, 0.6f),  // 3 = metal (blue-gray)
        glm::vec3(0.8f, 0.6f, 0.4f),  // 4 = wood (brown)
        glm::vec3(0.9f, 0.8f, 0.5f),  // 5 = sand (tan)
        glm::vec3(0.7f, 0.9f, 0.9f),  // 6 = ice (light blue)
        glm::vec3(0.5f, 0.4f, 0.3f)   // 7 = dirt (dark brown)
    };
    
    // Draw texture preview box in bottom RIGHT corner - larger size
    float previewSize = 80.0f;
    float previewX = fbW - previewSize - 40.0f;  // Right side, 40px margin
    float previewY = 40.0f;  // Near bottom (40px from bottom)
    
    // Draw black background first (for visibility)
    glm::mat4 bgModel = glm::mat4(1.0f);
    bgModel = glm::translate(bgModel, glm::vec3(previewX, previewY, 0.0f));
    bgModel = glm::scale(bgModel, glm::vec3(previewSize + 8.0f, previewSize + 8.0f, 1.0f));
    renderer.drawColoredQuad(bgModel, glm::vec3(0.0f, 0.0f, 0.0f)); // Black background
    
    // Draw white border
    glm::mat4 borderModel = glm::mat4(1.0f);
    borderModel = glm::translate(borderModel, glm::vec3(previewX, previewY, 0.0f));
    borderModel = glm::scale(borderModel, glm::vec3(previewSize + 6.0f, previewSize + 6.0f, 1.0f));
    renderer.drawColoredQuad(borderModel, glm::vec3(1.0f, 1.0f, 1.0f)); // White border
    
    // Draw colored preview inside
    glm::mat4 previewModel = glm::mat4(1.0f);
    previewModel = glm::translate(previewModel, glm::vec3(previewX, previewY, 0.0f));
    previewModel = glm::scale(previewModel, glm::vec3(previewSize, previewSize, 1.0f));
    renderer.drawColoredQuad(previewModel, textureColors[editorSelectedTexture]);
    
    // Preview label above the preview box
    char previewLabel[32];
    snprintf(previewLabel, sizeof(previewLabel), "TEX %d", editorSelectedTexture);
    renderer.drawText(previewLabel, previewX, previewY + previewSize + 15.0f, textScale);
}

void Game::processEditorInput() {
    static bool escPressed = false;
    static bool gPressed = false;
    static bool nPressed = false;
    static bool f9Pressed = false;
    static bool texKeyPressed[8] = {false};
    static bool rmbPressed = false;
    static bool qPressed = false;  // decrease size
    static bool ePressed = false;  // increase size
    static bool lbracketPressed = false;
    static bool rbracketPressed = false;
    static bool xPressed = false;
    static bool yPressed = false;
    static bool zPressed = false;
    static bool upArrowPressed = false;
    static bool downArrowPressed = false;
    static bool leftArrowPressed = false;
    static bool rightArrowPressed = false;
    static float sidewaysOffset = 0.0f;  // Persistent sideways offset
    static double lastPlaceTime = 0.0;   // For hold-to-place throttle
    const double placeInterval = 0.25;   // Seconds between placements when holding
    
    float moveSpeed = editorSpeed * 0.016f;
    
    // Flying camera movement (WASD + Space/Shift)
    glm::vec3 moveDir(0.0f);
    
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        moveDir += camera.Front;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        moveDir -= camera.Front;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        moveDir -= camera.Right;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        moveDir += camera.Right;
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);
    }
    
    // Normalize and apply movement
    if (glm::length(moveDir) > 0.0f) {
        moveDir = glm::normalize(moveDir);
        camera.Position += moveDir * moveSpeed;
    }
    
    // UP/DOWN arrows move block placement forward/backward along camera direction
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !upArrowPressed) {
        upArrowPressed = true;
        editorBlockOffset += editorGridSize;
        if (editorBlockOffset > 20.0f) editorBlockOffset = 20.0f;
        std::cout << "Placement forward: " << editorBlockOffset << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        upArrowPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !downArrowPressed) {
        downArrowPressed = true;
        editorBlockOffset -= editorGridSize;
        if (editorBlockOffset < 0.5f) editorBlockOffset = 0.5f;
        std::cout << "Placement backward: " << editorBlockOffset << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        downArrowPressed = false;
    }
    
    // Calculate base placement position from where camera is looking
    glm::vec3 lookPos = camera.Position + camera.Front * editorBlockOffset;
    
    // LEFT/RIGHT arrows move placement sideways (perpendicular to camera)
    static int gridCellX = 0;
    static int gridCellZ = 0;
    
    if (editorShowGrid) {
        float gridCellSize = glm::max(editorBlockSize.x, glm::max(editorBlockSize.y, editorBlockSize.z));
        
        // In grid mode, LEFT/RIGHT moves between grid cells
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS && !leftArrowPressed) {
            leftArrowPressed = true;
            gridCellX--;
            std::cout << "Grid cell: " << gridCellX << ", " << gridCellZ << std::endl;
        } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_RELEASE) {
            leftArrowPressed = false;
        }
        
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS && !rightArrowPressed) {
            rightArrowPressed = true;
            gridCellX++;
            std::cout << "Grid cell: " << gridCellX << ", " << gridCellZ << std::endl;
        } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_RELEASE) {
            rightArrowPressed = false;
        }
        
        // Calculate grid position based on where player is looking + cell offsets
        // Use floor() to align with grid lines (not between them)
        float baseGridX = floor(lookPos.x / gridCellSize) * gridCellSize + gridCellSize * 0.5f;
        float baseGridY = floor(lookPos.y / gridCellSize) * gridCellSize + gridCellSize * 0.5f;
        float baseGridZ = floor(lookPos.z / gridCellSize) * gridCellSize + gridCellSize * 0.5f;
        
        // Add sideways offset in grid cells
        editorGridPos.x = baseGridX + gridCellX * gridCellSize;
        editorGridPos.y = baseGridY;
        editorGridPos.z = baseGridZ + gridCellZ * gridCellSize;
    } else {
        // Free mode: use smooth sideways offset
        float moveStep = 0.5f;
        
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS && !leftArrowPressed) {
            leftArrowPressed = true;
            sidewaysOffset -= moveStep;
            std::cout << "Placement left: " << sidewaysOffset << std::endl;
        } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_RELEASE) {
            leftArrowPressed = false;
        }
        
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS && !rightArrowPressed) {
            rightArrowPressed = true;
            sidewaysOffset += moveStep;
            std::cout << "Placement right: " << sidewaysOffset << std::endl;
        } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_RELEASE) {
            rightArrowPressed = false;
        }
        
        // Use look position + sideways offset
        glm::vec3 placementPos = lookPos + camera.Right * sidewaysOffset;

        // Free placement mode: snap to nearest block face per axis (sticky edges)
        glm::vec3 snapPos = placementPos;
        float snapThreshold = 1.0f; // How close (center-to-face) before snapping kicks in

        if (currentMap) {
            const auto& blocks = currentMap->getBlocks();

            float bestDistX = snapThreshold, bestDistY = snapThreshold, bestDistZ = snapThreshold;
            float bestSnapX = placementPos.x, bestSnapY = placementPos.y, bestSnapZ = placementPos.z;

            for (const auto& block : blocks) {
                glm::vec3 blockHalfSize = block.scale * 0.5f;
                glm::vec3 blockMin = block.position - blockHalfSize;
                glm::vec3 blockMax = block.position + blockHalfSize;

                // Per-axis: find the closest face and snap flush to it
                float dXMin = fabs(placementPos.x - (blockMin.x - editorBlockSize.x * 0.5f));
                float dXMax = fabs(placementPos.x - (blockMax.x + editorBlockSize.x * 0.5f));
                if (dXMin < bestDistX) { bestDistX = dXMin; bestSnapX = blockMin.x - editorBlockSize.x * 0.5f; }
                if (dXMax < bestDistX) { bestDistX = dXMax; bestSnapX = blockMax.x + editorBlockSize.x * 0.5f; }

                float dYMin = fabs(placementPos.y - (blockMin.y - editorBlockSize.y * 0.5f));
                float dYMax = fabs(placementPos.y - (blockMax.y + editorBlockSize.y * 0.5f));
                if (dYMin < bestDistY) { bestDistY = dYMin; bestSnapY = blockMin.y - editorBlockSize.y * 0.5f; }
                if (dYMax < bestDistY) { bestDistY = dYMax; bestSnapY = blockMax.y + editorBlockSize.y * 0.5f; }

                float dZMin = fabs(placementPos.z - (blockMin.z - editorBlockSize.z * 0.5f));
                float dZMax = fabs(placementPos.z - (blockMax.z + editorBlockSize.z * 0.5f));
                if (dZMin < bestDistZ) { bestDistZ = dZMin; bestSnapZ = blockMin.z - editorBlockSize.z * 0.5f; }
                if (dZMax < bestDistZ) { bestDistZ = dZMax; bestSnapZ = blockMax.z + editorBlockSize.z * 0.5f; }
            }

            if (bestDistX < snapThreshold) snapPos.x = bestSnapX;
            if (bestDistY < snapThreshold) snapPos.y = bestSnapY;
            if (bestDistZ < snapThreshold) snapPos.z = bestSnapZ;
        }

        editorGridPos = snapPos;
    }
    
    // Texture selection (keys 1-8)
    for (int i = 0; i < 8; i++) {
        int key = GLFW_KEY_1 + i;
        if (glfwGetKey(window, key) == GLFW_PRESS && !texKeyPressed[i]) {
            texKeyPressed[i] = true;
            editorSelectedTexture = i;
            std::cout << "Selected texture: " << i << std::endl;
        } else if (glfwGetKey(window, key) == GLFW_RELEASE) {
            texKeyPressed[i] = false;
        }
    }
    
    // Grid toggle
    if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS && !gPressed) {
        gPressed = true;
        editorShowGrid = !editorShowGrid;
    } else if (glfwGetKey(window, GLFW_KEY_G) == GLFW_RELEASE) {
        gPressed = false;
    }
    
    // Noclip toggle
    if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS && !nPressed) {
        nPressed = true;
        editorNoclip = !editorNoclip;
    } else if (glfwGetKey(window, GLFW_KEY_N) == GLFW_RELEASE) {
        nPressed = false;
    }
    
    // Save map (F9)
    if (glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS && !f9Pressed) {
        f9Pressed = true;
        saveMap("custom.map");
    } else if (glfwGetKey(window, GLFW_KEY_F9) == GLFW_RELEASE) {
        f9Pressed = false;
    }
    
    // Block placement (Left mouse button) - hold to keep placing, throttled
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        double now = glfwGetTime();
        if (now - lastPlaceTime >= placeInterval) {
            lastPlaceTime = now;
            if (currentMap) {
                MapBlock block;
                block.position = editorGridPos;
                block.scale = editorBlockSize;
                block.color = glm::vec3(1.0f, 1.0f, 1.0f);
                block.textureId = editorSelectedTexture;
                block.solid = true;
                currentMap->addBlock(block);
                std::cout << "Added block at " << editorGridPos.x << "," << editorGridPos.y << "," << editorGridPos.z
                          << " size " << editorBlockSize.x << "x" << editorBlockSize.y << "x" << editorBlockSize.z << std::endl;
            }
        }
    }
    
    // Right mouse button:
    //   Plain RMB          = replace texture of aimed block with current texture
    //   Shift + RMB        = delete aimed block entirely
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS && !rmbPressed) {
        rmbPressed = true;
        if (currentMap) {
            glm::vec3 rayOrigin = camera.Position;
            glm::vec3 rayDir = camera.Front;
            float maxDist = 100.0f;
            float step = 0.1f;
            bool altHeld = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                          glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;

            for (float t = 0; t < maxDist; t += step) {
                glm::vec3 point = rayOrigin + rayDir * t;
                if (currentMap->findBlockAt(point, 0.15f) >= 0) {
                    if (altHeld) {
                        if (currentMap->deleteBlockAt(point, 0.15f)) {
                            std::cout << "Deleted block at distance " << t << std::endl;
                        }
                    } else {
                        if (currentMap->replaceTextureAt(point, editorSelectedTexture)) {
                            std::cout << "Replaced texture -> " << editorSelectedTexture
                                      << " at distance " << t << std::endl;
                        }
                    }
                    break;
                }
            }
        }
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_RELEASE) {
        rmbPressed = false;
    }
    
    // E key - increase block size on selected axis (or all)
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS && !ePressed) {
        ePressed = true;
        if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS) {
            editorBlockSize.x = glm::min(editorBlockSize.x + 0.5f, 20.0f);
        } else if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS) {
            editorBlockSize.y = glm::min(editorBlockSize.y + 0.5f, 20.0f);
        } else if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) {
            editorBlockSize.z = glm::min(editorBlockSize.z + 0.5f, 20.0f);
        } else {
            editorBlockSize += glm::vec3(0.5f);
            editorBlockSize = glm::min(editorBlockSize, glm::vec3(20.0f));
        }
        std::cout << "Block size: " << editorBlockSize.x << "x" << editorBlockSize.y << "x" << editorBlockSize.z << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_E) == GLFW_RELEASE) {
        ePressed = false;
    }

    // Q key - decrease block size on selected axis (or all)
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS && !qPressed) {
        qPressed = true;
        if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS) {
            editorBlockSize.x = glm::max(editorBlockSize.x - 0.5f, 0.5f);
        } else if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS) {
            editorBlockSize.y = glm::max(editorBlockSize.y - 0.5f, 0.5f);
        } else if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) {
            editorBlockSize.z = glm::max(editorBlockSize.z - 0.5f, 0.5f);
        } else {
            editorBlockSize -= glm::vec3(0.5f);
            editorBlockSize = glm::max(editorBlockSize, glm::vec3(0.5f));
        }
        std::cout << "Block size: " << editorBlockSize.x << "x" << editorBlockSize.y << "x" << editorBlockSize.z << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_RELEASE) {
        qPressed = false;
    }
    
    // Grid size adjustment with [ and ] keys
    if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS && !lbracketPressed) {
        lbracketPressed = true;
        editorGridSize = glm::max(editorGridSize - 0.5f, 0.5f);
        std::cout << "Grid size: " << editorGridSize << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_RELEASE) {
        lbracketPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS && !rbracketPressed) {
        rbracketPressed = true;
        editorGridSize = glm::min(editorGridSize + 0.5f, 5.0f);
        std::cout << "Grid size: " << editorGridSize << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_RELEASE) {
        rbracketPressed = false;
    }
    
    // ESC to exit
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !escPressed) {
        escPressed = true;
        gameState = MENU;
        menuSelection = 2;
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
        escPressed = false;
    }
}

void Game::saveMap(const std::string& filename) {
    if (!currentMap) {
        std::cerr << "No map to save!" << std::endl;
        return;
    }
    
    std::string filepath = "assets/maps/" + filename;
    std::ofstream file(filepath);
    
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << filepath << std::endl;
        return;
    }
    
    // Write header
    file << "# Custom map created in editor\n";
    file << "name Custom\n";
    file << "dimensions 100.0 50.0 100.0\n\n";
    
    // Write blocks
    const auto& blocks = currentMap->getBlocks();
    for (const auto& block : blocks) {
        // Format: block x y z width height depth textureId solid visible
        file << "block " 
             << block.position.x << " "
             << block.position.y << " "
             << block.position.z << " "
             << block.scale.x * 2.0f << " "  // Convert half-extents to full size
             << block.scale.y * 2.0f << " "
             << block.scale.z * 2.0f << " "
             << block.textureId << " "
             << (block.solid ? "1" : "0") << " "
             << "1\n"; // visible
    }
    
    file.close();
    std::cout << "Map saved to " << filepath << " with " << blocks.size() << " blocks" << std::endl;
}

// ========== PVP MULTIPLAYER FUNCTIONS ==========

void Game::checkProjectilePlayerCollision() {
    if (playerDamageCooldown > 0.0f) {
        return; // Player has damage immunity
    }
    
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) continue;
        
        // Only check projectiles from other players (owner != 0 means remote player)
        if (projectiles[i].owner == 0) continue; // Skip own projectiles
        
        // Check collision with local player
        float distance = glm::length(projectiles[i].position - playerPos);
        if (distance < 0.5f) { // Hit radius of 0.5 units
            // Check projectile type by velocity
            bool isPistolBullet = glm::length(projectiles[i].velocity) > 35.0f;
            
            float damage = 0.0f;
            if (isPistolBullet) {
                damage = 20.0f; // Pistol damage
            } else {
                // Grenade/flashbang explosion damage
                createSimpleExplosion(projectiles[i].position);
                damage = 40.0f; // Explosion damage
            }
            
            damagePlayer(damage);
            projectiles[i].active = false;
            activeProjectiles--;
            break;
        }
    }
}

void Game::damagePlayer(float damage) {
    if (isDead) return; // Already dead
    
    playerHealth -= damage;
    if (playerHealth < 0.0f) playerHealth = 0.0f;
    
    damageEffectTime = 1.0f; // Screen effect
    playerDamageCooldown = 1.0f; // 1 second immunity
    
    std::cout << "Player hit! Health: " << playerHealth << "/" << playerMaxHealth << std::endl;
    
    if (playerHealth <= 0.0f) {
        isDead = true;
        gameState = DEAD;
        std::cout << "Player defeated!" << std::endl;
    }
}

void Game::checkPlayerPlayerCollision() {
    if (playerDamageCooldown > 0.0f) {
        return; // Player has damage immunity
    }
    
    // Check collision with remote players
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        if (!remotePlayers[i].active) continue;
        
        glm::vec3 remotePlayerPos = remotePlayers[i].position;
        float distance = glm::length(playerPos - remotePlayerPos);
        
        if (distance < 1.0f) { // Collision radius of 1 unit
            // Apply collision damage (melee damage)
            damagePlayer(15.0f); // 15 damage for player collision
            break; // Only one collision per frame
        }
    }
}