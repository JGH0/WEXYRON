/**
 * @file game.cpp
 * @brief Implementation of Game class for main game loop and state management
 * @author OpenGL Game Developer
 * @date 2025
 */

#include "game.hpp"
#include <iostream>
#include <random>
#include <cstring>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <glm/gtc/matrix_transform.hpp>

Game::Game(int width, int height) 
    : camera(glm::vec3(0.0f, 0.0f, 3.0f)), 
      gameState(MENU),
      menuSelection(0),
      mapSelection(0),
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
      discoverySocketFd(-1),
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
    if (discoverySocketFd >= 0) {
        close(discoverySocketFd);
        discoverySocketFd = -1;
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
        // Handle UP key
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !upPressed) {
            upPressed = true;
            menuSelection = (menuSelection - 1 + 5) % 5;
        } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
            upPressed = false;
        }
        
        // Handle DOWN key
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !downPressed) {
            downPressed = true;
            menuSelection = (menuSelection + 1) % 5;
        } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
            downPressed = false;
        }
        
        // Handle ENTER key
        if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterPressed) {
            enterPressed = true;
            switch (menuSelection) {
                case 0: // Play Game
                    gameState = PLAYING;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    break;
                case 1: // Map Selection
                    std::cout << "ENTERING MAP SELECTION MODE" << std::endl;
                    gameState = MAP_SELECTION;
                    mapSelection = 0;
                    break;
                case 2: // Connect to Server
                    std::cout << "ENTERING SERVER CONNECTION" << std::endl;
                    gameState = SERVER_CONNECT;
                    serverAddress = "";
                    serverPort = 7777;
                    enteringServerAddress = true;
                    break;
                case 3: // Settings
                    gameState = SETTINGS;
                    settingsSelection = 0;
                    break;
                case 4: // Exit
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
        shoot();
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
        // Update player physics
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
                
                networkClient->sendPlayerInput(moveX, moveZ, camera.Yaw, camera.Pitch, jump, shoot, static_cast<uint8_t>(currentWeapon));
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
    }
}

void Game::render() {
    // Render everything into a consistent 4:3 area to avoid stretching on widescreen.
    // The virtual UI space is 800x600; we letterbox/pillarbox into the actual framebuffer.
    int fbW = 800, fbH = 600;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = 800;
    if (fbH <= 0) fbH = 600;

    const float targetAspect = 800.0f / 600.0f; // 4:3
    float fbAspect = (float)fbW / (float)fbH;

    int vpX = 0, vpY = 0, vpW = fbW, vpH = fbH;
    if (fbAspect > targetAspect) {
        // Too wide: pillarbox
        vpH = fbH;
        vpW = (int)((float)fbH * targetAspect);
        vpX = (fbW - vpW) / 2;
        vpY = 0;
    } else if (fbAspect < targetAspect) {
        // Too tall: letterbox
        vpW = fbW;
        vpH = (int)((float)fbW / targetAspect);
        vpX = 0;
        vpY = (fbH - vpH) / 2;
    }

    renderer.setViewport(vpX, vpY, vpW, vpH);

    if (gameState == PLAYING) {
        renderer.clear();

        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom),
                                                (float)vpW / (float)vpH, 0.1f, 100.0f);
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
                    glm::mat4 model = glm::mat4(1.0f);
                    model = glm::translate(model, remotePlayers[i].position);
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
        
        // Set up orthographic projection for 2D death screen
        glm::mat4 projection = glm::ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f);
        glm::mat4 view = glm::mat4(1.0f);
        renderer.setViewProjection(view, projection);
        
        // Render death screen
        renderDeathScreen();
        glfwSwapBuffers(window);
        glfwPollEvents();
    } else if (gameState == MENU) {
        // Set up 2D rendering for menu
        renderer.clear();
        
        // Set up orthographic projection for 2D menu
        glm::mat4 projection = glm::ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f);
        glm::mat4 view = glm::mat4(1.0f);
        renderer.setViewProjection(view, projection);
        
        renderMenu();
        glfwSwapBuffers(window);
        glfwPollEvents();
    } else if (gameState == SETTINGS) {
        // Set up 2D rendering for settings
        renderer.clear();
        
        glm::mat4 projection = glm::ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f);
        glm::mat4 view = glm::mat4(1.0f);
        renderer.setViewProjection(view, projection);
        
        renderSettings();
        glfwSwapBuffers(window);
        glfwPollEvents();
    } else if (gameState == MAP_SELECTION) {
        // Set up 2D rendering for map selection
        renderer.clear();
        
        glm::mat4 projection = glm::ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f);
        glm::mat4 view = glm::mat4(1.0f);
        renderer.setViewProjection(view, projection);
        
        renderMapSelection();
        glfwSwapBuffers(window);
        glfwPollEvents();
    } else if (gameState == SERVER_CONNECT) {
        // Set up 2D rendering for server connection
        renderServerConnect();
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
    return currentMap != nullptr;
}

Map* Game::getCurrentMap() {
    return currentMap;
}

void Game::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    // Viewport is controlled in `Game::render()` (letterboxed) to prevent stretching.
    // Keep this as a no-op other than ensuring non-zero sizes.
    if (width <= 0 || height <= 0) return;
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
    if (currentMap && currentMap->checkCollision(playerPos, 0.3f)) {
        int unstuckCounter = 0;
        while (currentMap->checkCollision(playerPos, 0.3f) && unstuckCounter < 50) {
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
        collision = currentMap->checkCollision(newPos, 0.3f); // Larger collision radius
    }
    
    if (collision) {
        // Try X axis movement only
        glm::vec3 testPosX = originalPos;
        testPosX.x = newPos.x;
        bool xCollision = currentMap->checkCollision(testPosX, 0.3f);
        
        // Try Z axis movement only  
        glm::vec3 testPosZ = originalPos;
        testPosZ.z = newPos.z;
        bool zCollision = currentMap->checkCollision(testPosZ, 0.3f);
        
        // Try Y axis movement only (for gravity)
        glm::vec3 testPosY = originalPos;
        testPosY.y = newPos.y;
        bool yCollision = currentMap->checkCollision(testPosY, 0.3f);
        
        // Apply movement only on axes without collision
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
        
        for (int i = 0; i < 5; i++) {
            if (currentMap->checkCollision(checkPoints[i], 0.15f)) {
                // Check if we're actually on top of the block (not inside it from the side)
                // by verifying we're above the block's top surface
                onGround = true;
                
                // Only stop falling if we're moving downward
                if (playerVelocity.y <= 0.0f) {
                    playerVelocity.y = 0.0f;
                    isJumping = false;
                }
                break;
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

bool Game::createProjectile(const glm::vec3& position, const glm::vec3& velocity, float lifetime) {
    // Find an inactive projectile slot
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) {
            projectiles[i].position = position;
            projectiles[i].velocity = velocity;
            projectiles[i].lifetime = lifetime;
            projectiles[i].active = true;
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
    
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) {
            projectiles[i].position = camera.Position;
            
            projectiles[i].velocity = camera.Front * 120.0f; 
            projectiles[i].lifetime = 3.0f; 
            
            projectiles[i].active = true;
            activeProjectiles++;
            if (!infiniteAmmoMode) {
                pistolAmmo--;
            }
            pistolCooldown = 0.15f; 
            std::cout << "Pistol shot! Active: " << activeProjectiles << " Ammo: " << (infiniteAmmoMode ? "∞" : std::to_string(pistolAmmo)) << std::endl;
            break;
        }
    }
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
            projectiles[i].position = camera.Position;
            
            // Add spread to shotgun pellets - REDUCED SPREAD
            glm::vec3 spread = glm::vec3(
                (rand() % 100 - 50) * 0.002f, // -0.1 to 0.1 (reduced from 0.5)
                (rand() % 100 - 50) * 0.002f, // -0.1 to 0.1 (reduced from 0.5)
                0.0f
            );
            projectiles[i].velocity = (camera.Front + spread) * 50.0f; // 50 units per second
            projectiles[i].lifetime = 1.0f; // 1 second lifetime
            projectiles[i].active = true;
            activeProjectiles++;
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
    
    // Find an inactive projectile slot for grenade
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) {
            projectiles[i].position = camera.Position;
            
            // Throw straight from crosshair, then arc down naturally
            projectiles[i].velocity = camera.Front * 30.0f; // Forward from crosshair
            // No initial upward arc - let gravity handle the downward curve
            
            projectiles[i].lifetime = 5.0f; // Longer lifetime for arc travel
            projectiles[i].active = true;
            activeProjectiles++;
            if (!infiniteAmmoMode) {
                grenadeCount--;
            }
            grenadeCooldown = 1.5f; // Longer cooldown
            std::cout << "Threw grenade! Grenades left: " << (infiniteAmmoMode ? "∞" : std::to_string(grenadeCount)) << std::endl;
            break;
        }
    }
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
    
    // Find an inactive projectile slot for flashbang
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) {
            projectiles[i].position = camera.Position;
            
            // Throw straight from crosshair, then arc down naturally
            projectiles[i].velocity = camera.Front * 25.0f; // Slightly slower than grenade
            // No initial upward arc - let gravity handle the downward curve
            
            projectiles[i].lifetime = 4.0f; // Shorter lifetime
            projectiles[i].active = true;
            activeProjectiles++;
            if (!infiniteAmmoMode) {
                flashbangCount--;
            }
            flashbangCooldown = 2.0f; // Longer cooldown
            std::cout << "Threw flashbang! Flashbangs left: " << (infiniteAmmoMode ? "∞" : std::to_string(flashbangCount)) << std::endl;
            break;
        }
    }
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

    // Always provide some visible feedback on detonation; scale strength by distance.
    float flashIntensity = 1.0f - (playerDistance / flashRadius);
    if (flashIntensity < 0.0f) flashIntensity = 0.0f;
    if (flashIntensity > 1.0f) flashIntensity = 1.0f;

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
        
        // Set up 2D rendering for flash effect
        glm::mat4 projection = glm::ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f);
        glm::mat4 view = glm::mat4(1.0f);
        renderer.setViewProjection(view, projection);
        
        // Ensure overlay is visible over the 3D scene
        glDisable(GL_DEPTH_TEST);

        // Draw white overlay
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(400.0f, 300.0f, 0.0f)); // Center of screen
        model = glm::scale(model, glm::vec3(800.0f, 600.0f, 1.0f)); // Full screen
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
                // Normalize direction and move towards player
                direction = glm::normalize(direction);
                enemies[i].velocity = direction * enemies[i].speed;
                enemies[i].position += enemies[i].velocity * deltaTime;
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
    // Set up 2D rendering for crosshair (after 3D rendering)
    glm::mat4 projection = glm::ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f);
    glm::mat4 view = glm::mat4(1.0f);
    renderer.setViewProjection(view, projection);
    
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
        // Set up 2D rendering for damage effect
        glm::mat4 projection = glm::ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f);
        glm::mat4 view = glm::mat4(1.0f);
        renderer.setViewProjection(view, projection);
        
        // Calculate damage effect alpha (fade out over time)
        float alpha = damageEffectTime / 0.5f; // 0.5 second duration
        
        // Draw subtle red side overlays for damage effect
        float sideWidth = 150.0f; // Width of side effects
        float sideAlpha = alpha * 0.3f; // Much more subtle
        
        // Left side red tint
        glm::mat4 leftModel = glm::mat4(1.0f);
        leftModel = glm::translate(leftModel, glm::vec3(sideWidth/2, 300.0f, 0.0f));
        leftModel = glm::scale(leftModel, glm::vec3(sideWidth, 600.0f, 1.0f));
        glm::vec3 leftColor = glm::vec3(0.8f, 0.0f, 0.0f) * sideAlpha; // Subtle red
        renderer.drawColoredQuad(leftModel, leftColor);
        
        // Right side red tint
        glm::mat4 rightModel = glm::mat4(1.0f);
        rightModel = glm::translate(rightModel, glm::vec3(800.0f - sideWidth/2, 300.0f, 0.0f));
        rightModel = glm::scale(rightModel, glm::vec3(sideWidth, 600.0f, 1.0f));
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
    
    // Menu items: Play Game, Map Selection, Connect to Server, Settings, Exit
    const char* menuItems[] = {"Play Game", "Map Selection", "Connect to Server", "Settings", "Exit"};
    glm::vec3 colors[] = {
        glm::vec3(0.2f, 0.8f, 0.2f), // Green for Play
        glm::vec3(0.2f, 0.4f, 0.8f), // Blue for Map Selection
        glm::vec3(0.8f, 0.2f, 0.8f), // Purple for Connect to Server
        glm::vec3(0.8f, 0.6f, 0.2f), // Orange for Settings
        glm::vec3(0.8f, 0.2f, 0.2f)  // Red for Exit
    };
    
    for (int i = 0; i < 5; i++) {
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
        statusBg = glm::scale(statusBg, glm::vec3(200.0f, 30.0f, 1.0f));
        renderer.drawColoredQuad(statusBg, glm::vec3(0.0f, 0.8f, 0.0f)); // Green background
        renderer.drawText("CONNECTED", 360.0f, 515.0f, 1.0f);
    }
    
    // Print to console for debugging and text display
    std::cout << "\n=== MAIN MENU ===" << std::endl;
    std::cout << (menuSelection == 0 ? "> " : "  ") << "Play Game" << std::endl;
    std::cout << (menuSelection == 1 ? "> " : "  ") << "Map Selection" << std::endl;
    std::cout << (menuSelection == 2 ? "> " : "  ") << "Connect to Server" << std::endl;
    std::cout << (menuSelection == 3 ? "> " : "  ") << "Settings" << std::endl;
    std::cout << (menuSelection == 4 ? "> " : "  ") << "Exit" << std::endl;
    std::cout << "Use UP/DOWN to select, ENTER to confirm, ESC to resume" << std::endl;
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
    
    // Print to console for debugging and text display
    std::cout << "\n=== SETTINGS ===" << std::endl;
    std::cout << (settingsSelection == 0 ? "> " : "  ") << "Forward: " << keyNames[0] << std::endl;
    std::cout << (settingsSelection == 1 ? "> " : "  ") << "Backward: " << keyNames[1] << std::endl;
    std::cout << (settingsSelection == 2 ? "> " : "  ") << "Left: " << keyNames[2] << std::endl;
    std::cout << (settingsSelection == 3 ? "> " : "  ") << "Right: " << keyNames[3] << std::endl;
    std::cout << (settingsSelection == 4 ? "> " : "  ") << "Jump: " << keyNames[4] << std::endl;
    std::cout << (settingsSelection == 5 ? "> " : "  ") << "Menu: " << keyNames[5] << std::endl;
    std::cout << "Use UP/DOWN to select, ENTER to change, ESC to go back" << std::endl;
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
    
    // Set up 2D rendering
    glm::mat4 projection = glm::ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f);
    glm::mat4 view = glm::mat4(1.0f);
    renderer.setViewProjection(view, projection);
    
    // Draw full screen dark background
    glm::mat4 bgModel = glm::mat4(1.0f);
    bgModel = glm::translate(bgModel, glm::vec3(400.0f, 300.0f, 0.0f));
    bgModel = glm::scale(bgModel, glm::vec3(800.0f, 600.0f, 1.0f));
    renderer.drawColoredQuad(bgModel, glm::vec3(0.05f, 0.05f, 0.15f)); // Very dark blue
    
    // Title area - bright colored bar at top
    glm::mat4 titleBg = glm::mat4(1.0f);
    titleBg = glm::translate(titleBg, glm::vec3(400.0f, 60.0f, 0.0f));
    titleBg = glm::scale(titleBg, glm::vec3(600.0f, 60.0f, 1.0f));
    renderer.drawColoredQuad(titleBg, glm::vec3(0.8f, 0.4f, 0.1f)); // Orange title bar
    
    // Map data
    const char* mapNames[] = {"SIMPLE", "LARGE", "ARENA", "SNIPER", "COMPLEX"};
    glm::vec3 mapColors[] = {
        glm::vec3(0.3f, 0.8f, 0.3f),  // Green - Simple
        glm::vec3(0.3f, 0.5f, 1.0f),  // Blue - Large
        glm::vec3(1.0f, 0.6f, 0.2f),  // Orange - Arena
        glm::vec3(0.8f, 0.3f, 0.8f),  // Purple - Sniper
        glm::vec3(0.5f, 0.8f, 1.0f)   // Cyan - Complex
    };
    const int numMaps = 5;
    
    float startY = 140.0f;
    float boxHeight = 70.0f;
    float spacing = 80.0f;
    
    // Draw map boxes
    for (int i = 0; i < numMaps; i++) {
        float y = startY + i * spacing;
        bool isSelected = (i == mapSelection);
        
        glm::mat4 box = glm::mat4(1.0f);
        box = glm::translate(box, glm::vec3(400.0f, y, 0.0f));
        
        if (isSelected) {
            box = glm::scale(box, glm::vec3(420.0f, boxHeight + 10.0f, 1.0f));
            renderer.drawColoredQuad(box, mapColors[i] * 1.3f); // Brighter when selected
            
            // White indicator
            glm::mat4 indicator = glm::mat4(1.0f);
            indicator = glm::translate(indicator, glm::vec3(140.0f, y, 0.0f));
            indicator = glm::scale(indicator, glm::vec3(25.0f, 25.0f, 1.0f));
            renderer.drawColoredQuad(indicator, glm::vec3(1.0f, 1.0f, 1.0f));
        } else {
            box = glm::scale(box, glm::vec3(380.0f, boxHeight, 1.0f));
            renderer.drawColoredQuad(box, mapColors[i]); // Normal color
        }
    }
    
    // Instructions at bottom - gray bar
    glm::mat4 instrBg = glm::mat4(1.0f);
    instrBg = glm::translate(instrBg, glm::vec3(400.0f, 540.0f, 0.0f));
    instrBg = glm::scale(instrBg, glm::vec3(700.0f, 50.0f, 1.0f));
    renderer.drawColoredQuad(instrBg, glm::vec3(0.3f, 0.3f, 0.3f)); // Gray bar
    
    // Print to console
    std::cout << "\n=== MAP SELECTION ===" << std::endl;
    for (int i = 0; i < numMaps; i++) {
        std::cout << (i == mapSelection ? "> " : "  ") << mapNames[i] << std::endl;
    }
    std::cout << "UP/DOWN = change, ENTER = load, ESC = back" << std::endl;
}

void Game::processMapSelectionInput() {
    static bool upPressed = false;
    static bool downPressed = false;
    static bool enterPressed = false;
    static bool escapePressed = false;
    
    const int numMaps = 5;
    
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
        const char* mapFiles[] = {"simple.map", "large.map", "arena.map", "sniper.map", "complex.map"};
        
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
        menuSelection = 1; // Keep on Map Selection option
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
        // Apply server position to local player (server authoritative)
        if (networkClient && packet.playerId == networkClient->getClientId()) {
            // Update local player position from server
            playerPos.x = packet.posX;
            playerPos.y = packet.posY;
            playerPos.z = packet.posZ;
            playerVelocity.x = packet.velX;
            playerVelocity.y = packet.velY;
            playerVelocity.z = packet.velZ;
            // Client controls its own camera - don't apply server yaw/pitch
            playerHealth = packet.health;
            isDead = packet.isDead;
            return;
        }
        
        // Update remote player state
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
    };
    
    networkClient->onPlayerJoin = [this](const PlayerJoinPacket& packet) {
        std::cout << "Player " << packet.playerId << " joined the game!" << std::endl;
        // Add new remote player
        for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
            if (!remotePlayers[i].active) {
                remotePlayers[i].id = packet.playerId;
                remotePlayers[i].position = glm::vec3(packet.spawnX, packet.spawnY, packet.spawnZ);
                remotePlayers[i].active = true;
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
                break;
            }
        }
    };
    
    networkClient->onProjectileSpawn = [this](const ProjectileSpawnPacket& packet) {
        // Create projectile from other player
        createProjectile(
            glm::vec3(packet.posX, packet.posY, packet.posZ),
            glm::vec3(packet.velX, packet.velY, packet.velZ),
            packet.lifetime
        );
    };
    
    if (!networkClient->connect(address, port)) {
        delete networkClient;
        networkClient = nullptr;
        std::cerr << "Failed to connect to server at " << address << ":" << port << std::endl;
        return false;
    }
    
    multiplayerMode = true;
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
    
    // Clear remote players
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        remotePlayers[i].active = false;
    }
}

bool Game::isConnectedToServer() const {
    return networkClient && networkClient->isConnected();
}

static bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void ensureDiscoverySocket(int& sock) {
    if (sock >= 0) return;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setNonBlocking(sock);
}

static void sendDiscoverPing(int sock, uint16_t port) {
    if (sock < 0) return;

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
    sendto(sock, &p, sizeof(p), 0, (sockaddr*)&local, sizeof(local));

    // Broadcast
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = inet_addr("255.255.255.255");
    sendto(sock, &p, sizeof(p), 0, (sockaddr*)&to, sizeof(to));
}

// ========== SERVER CONNECTION MENU ==========

void Game::renderServerConnect() {
    // Clear screen with dark background
    renderer.clear();
    
    // Set up 2D rendering
    glm::mat4 projection = glm::ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f);
    glm::mat4 view = glm::mat4(1.0f);
    renderer.setViewProjection(view, projection);
    
    // Draw full screen dark background
    glm::mat4 bgModel = glm::mat4(1.0f);
    bgModel = glm::translate(bgModel, glm::vec3(400.0f, 300.0f, 0.0f));
    bgModel = glm::scale(bgModel, glm::vec3(800.0f, 600.0f, 1.0f));
    renderer.drawColoredQuad(bgModel, glm::vec3(0.05f, 0.05f, 0.15f)); // Very dark blue
    
    // Title bar
    glm::mat4 titleBg = glm::mat4(1.0f);
    titleBg = glm::translate(titleBg, glm::vec3(400.0f, 80.0f, 0.0f));
    titleBg = glm::scale(titleBg, glm::vec3(600.0f, 80.0f, 1.0f));
    renderer.drawColoredQuad(titleBg, glm::vec3(0.8f, 0.2f, 0.8f)); // Purple title bar
    renderer.drawText("CONNECT TO SERVER", 280.0f, 75.0f, 1.5f);
    
    // Server address input box
    float boxY = 220.0f;
    glm::mat4 addressBox = glm::mat4(1.0f);
    addressBox = glm::translate(addressBox, glm::vec3(400.0f, boxY, 0.0f));
    addressBox = glm::scale(addressBox, glm::vec3(500.0f, 60.0f, 1.0f));
    renderer.drawColoredQuad(addressBox, enteringServerAddress && !focusServerList ? glm::vec3(0.25f, 0.25f, 0.55f) : glm::vec3(0.2f, 0.2f, 0.4f));
    
    // Draw server address text
    renderer.drawText("Server IP:", 150.0f, boxY - 10.0f, 1.2f);
    std::string ipText = serverAddress.empty() ? std::string("192.168.1.50") : serverAddress;
    renderer.drawText(ipText.c_str(), 250.0f, boxY - 10.0f, 1.2f);
    
    // Port input box
    float portY = 320.0f;
    glm::mat4 portBox = glm::mat4(1.0f);
    portBox = glm::translate(portBox, glm::vec3(400.0f, portY, 0.0f));
    portBox = glm::scale(portBox, glm::vec3(500.0f, 60.0f, 1.0f));
    renderer.drawColoredQuad(portBox, (!enteringServerAddress && !focusServerList) ? glm::vec3(0.25f, 0.25f, 0.55f) : glm::vec3(0.2f, 0.2f, 0.4f));
    
    // Draw port text
    renderer.drawText("Port:", 150.0f, portY - 10.0f, 1.2f);
    std::string portStr = std::to_string(serverPort);
    renderer.drawText(portStr.c_str(), 250.0f, portY - 10.0f, 1.2f);

    // Discovered servers list
    float listY = 380.0f;
    renderer.drawText("FOUND SERVERS (LAN):", 150.0f, listY - 30.0f, 1.0f);
    if (discoveredServers.empty()) {
        renderer.drawText("No servers found yet. Press R to rescan.", 150.0f, listY, 0.9f);
    } else {
        int maxRows = 3;
        for (int i = 0; i < (int)discoveredServers.size() && i < maxRows; i++) {
            const auto& s = discoveredServers[i];
            std::string row = s.name + " - " + s.ip + ":" + std::to_string(s.port);
            renderer.drawText(row.c_str(), 160.0f, listY + i * 25.0f, 0.9f);
            if (focusServerList && i == discoveredSelection) {
                glm::mat4 indicator = glm::mat4(1.0f);
                indicator = glm::translate(indicator, glm::vec3(145.0f, listY + i * 25.0f + 5.0f, 0.0f));
                indicator = glm::scale(indicator, glm::vec3(10.0f, 10.0f, 1.0f));
                renderer.drawColoredQuad(indicator, glm::vec3(1.0f, 1.0f, 1.0f));
            }
        }
    }
    
    // Connect button
    float connectY = 420.0f;
    glm::mat4 connectBtn = glm::mat4(1.0f);
    connectBtn = glm::translate(connectBtn, glm::vec3(400.0f, connectY, 0.0f));
    connectBtn = glm::scale(connectBtn, glm::vec3(300.0f, 60.0f, 1.0f));
    
    if (isConnectedToServer()) {
        renderer.drawColoredQuad(connectBtn, glm::vec3(0.8f, 0.2f, 0.2f)); // Red for disconnect
        renderer.drawText("DISCONNECT", 340.0f, connectY - 10.0f, 1.3f);
    } else if (networkClient && networkClient->isConnecting()) {
        renderer.drawColoredQuad(connectBtn, glm::vec3(0.8f, 0.8f, 0.2f)); // Yellow for connecting
        renderer.drawText("CONNECTING...", 320.0f, connectY - 10.0f, 1.2f);
    } else {
        renderer.drawColoredQuad(connectBtn, glm::vec3(0.2f, 0.8f, 0.2f)); // Green for connect
        renderer.drawText("CONNECT", 350.0f, connectY - 10.0f, 1.3f);
    }
    
    // Back button
    float backY = 520.0f;
    glm::mat4 backBtn = glm::mat4(1.0f);
    backBtn = glm::translate(backBtn, glm::vec3(400.0f, backY, 0.0f));
    backBtn = glm::scale(backBtn, glm::vec3(200.0f, 50.0f, 1.0f));
    renderer.drawColoredQuad(backBtn, glm::vec3(0.4f, 0.4f, 0.4f)); // Gray
    renderer.drawText("BACK", 370.0f, backY - 8.0f, 1.2f);
    
    // Instructions at bottom
    renderer.drawText("TAB switch field, S focus list, R rescan LAN, ENTER connect/select, ESC back", 120.0f, 580.0f, 0.75f);
    
}

void Game::processServerConnectInput() {
    static bool tabPressed = false;
    static bool enterPressed = false;
    static bool escapePressed = false;
    static bool backspacePressed = false;
    static bool periodPressed = false;
    static bool rPressed = false;
    static bool sPressed = false;
    static bool upPressed = false;
    static bool downPressed = false;
    static bool digitPressed[10] = {false};

    // Keep LAN discovery socket alive and poll for responses
    discoveryTimeSeconds += 0.016f; // best-effort (menu doesn't have deltaTime)
    if (discoverySendCooldown > 0.0f) discoverySendCooldown -= 0.016f;
    ensureDiscoverySocket(discoverySocketFd);

    // Poll discovery responses (non-blocking)
    if (discoverySocketFd >= 0) {
        char buffer[MAX_PACKET_SIZE];
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        while (true) {
            ssize_t bytes = recvfrom(discoverySocketFd, buffer, sizeof(buffer), 0, (sockaddr*)&from, &fromLen);
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
    
    // Handle TAB to switch between IP and Port fields
    if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS && !tabPressed) {
        tabPressed = true;
        enteringServerAddress = !enteringServerAddress;
        focusServerList = false;
    } else if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_RELEASE) {
        tabPressed = false;
    }

    // Toggle focus to server list (S)
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS && !sPressed) {
        sPressed = true;
        focusServerList = !focusServerList;
    } else if (glfwGetKey(window, GLFW_KEY_S) == GLFW_RELEASE) {
        sPressed = false;
    }

    // Auto-scan while on this screen (lightweight)
    if (discoverySocketFd >= 0 && discoverySendCooldown <= 0.0f) {
        uint16_t port = serverPort == 0 ? DEFAULT_PORT : serverPort;
        sendDiscoverPing(discoverySocketFd, port);
        discoverySendCooldown = 1.0f;
    }

    // Rescan LAN (R)
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !rPressed) {
        rPressed = true;
        uint16_t port = serverPort == 0 ? DEFAULT_PORT : serverPort;
        sendDiscoverPing(discoverySocketFd, port);
        discoverySendCooldown = 0.75f;
    } else if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) {
        rPressed = false;
    }

    // Navigate discovered list
    if (focusServerList) {
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !upPressed) {
            upPressed = true;
            if (!discoveredServers.empty()) {
                discoveredSelection = (discoveredSelection - 1 + (int)discoveredServers.size()) % (int)discoveredServers.size();
            }
        } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
            upPressed = false;
        }
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !downPressed) {
            downPressed = true;
            if (!discoveredServers.empty()) {
                discoveredSelection = (discoveredSelection + 1) % (int)discoveredServers.size();
            }
        } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
            downPressed = false;
        }
    }
    
    // Handle number keys for IP/port input
    for (int key = GLFW_KEY_0; key <= GLFW_KEY_9; key++) {
        int idx = key - GLFW_KEY_0;
        if (glfwGetKey(window, key) == GLFW_PRESS && !digitPressed[idx] && !focusServerList) {
            digitPressed[idx] = true;
            char digit = '0' + (key - GLFW_KEY_0);
            if (enteringServerAddress) {
                serverAddress += digit;
            } else {
                if (serverPort < 65535) {
                    serverPort = serverPort * 10 + (digit - '0');
                }
            }
        } else if (glfwGetKey(window, key) == GLFW_RELEASE) {
            digitPressed[idx] = false;
        }
    }
    
    // Handle period for IP address
    if (enteringServerAddress && glfwGetKey(window, GLFW_KEY_PERIOD) == GLFW_PRESS && !periodPressed && !focusServerList) {
        periodPressed = true;
        serverAddress += '.';
    } else if (glfwGetKey(window, GLFW_KEY_PERIOD) == GLFW_RELEASE) {
        periodPressed = false;
    }

    // Also support keypad decimal for IP entry
    if (enteringServerAddress && glfwGetKey(window, GLFW_KEY_KP_DECIMAL) == GLFW_PRESS && !periodPressed && !focusServerList) {
        periodPressed = true;
        serverAddress += '.';
    } else if (glfwGetKey(window, GLFW_KEY_KP_DECIMAL) == GLFW_RELEASE) {
        // don't reset here if normal period is still held; keep it simple
        periodPressed = false;
    }
    
    // Handle backspace
    if (glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS && !backspacePressed) {
        backspacePressed = true;
        if (enteringServerAddress) {
            if (!serverAddress.empty()) {
                serverAddress.pop_back();
            }
        } else {
            serverPort = serverPort / 10;
        }
    } else if (glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_RELEASE) {
        backspacePressed = false;
    }
    
    // Handle ENTER to connect/disconnect
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterPressed) {
        enterPressed = true;
        if (isConnectedToServer()) {
            disconnectFromServer();
            std::cout << "Disconnected from server" << std::endl;
        } else {
            if (focusServerList && !discoveredServers.empty()) {
                const auto& s = discoveredServers[discoveredSelection];
                serverAddress = s.ip;
                serverPort = s.port;
                focusServerList = false;
            } else if (connectToServer(serverAddress, serverPort)) {
                std::cout << "Connected! Press ESC to return to menu" << std::endl;
            } else {
                std::cout << "Failed to connect to " << serverAddress << ":" << serverPort << std::endl;
            }
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