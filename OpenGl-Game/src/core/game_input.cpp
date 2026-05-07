#include "game.hpp"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

void Game::processInput(float deltaTime) {
    // ── ESC ──────────────────────────────────────────────────────────
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !inp.escapePressed) {
        inp.escapePressed = true;
        if (gameState == PLAYING) {
            inp.resetNavKeys();
            menuJustEntered = true;
            gameState = MENU;
            menuSelection = 0;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (gameState == MENU) {
            gameState = PLAYING;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else if (gameState == SETTINGS) {
            // ESC from settings returns to menu, not to game
            inp.resetNavKeys();
            menuJustEntered = true;
            gameState = MENU;
            menuSelection = isConnectedToServer() ? 3 : 4; // Settings index
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (gameState == MAP_SELECTION) {
            inp.resetNavKeys();
            menuJustEntered = true;
            gameState = MENU;
            menuSelection = 1; // back to Map Selection item
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (gameState == SERVER_CONNECT) {
            inp.resetNavKeys();
            menuJustEntered = true;
            gameState = MENU;
            menuSelection = isConnectedToServer() ? 2 : 3;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (gameState == MAP_EDITOR) {
            inp.resetNavKeys();
            menuJustEntered = true;
            gameState = MENU;
            menuSelection = isConnectedToServer() ? 1 : 2;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
        inp.escapePressed = false;
    }

    // ── State dispatch ────────────────────────────────────────────────
    if (gameState == PLAYING) {
        processGameInput(deltaTime);
    } else if (gameState == MENU) {
        if (menuJustEntered) { inp.resetNavKeys(); menuJustEntered = false; }

        int numItems = isConnectedToServer() ? 5 : 6;
        if (menuSelection >= numItems) menuSelection = numItems - 1;
        if (menuSelection < 0) menuSelection = 0;

        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !inp.upPressed) {
            inp.upPressed = true;
            menuSelection = (menuSelection - 1 + numItems) % numItems;
        } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
            inp.upPressed = false;
        }
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !inp.downPressed) {
            inp.downPressed = true;
            menuSelection = (menuSelection + 1) % numItems;
        } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
            inp.downPressed = false;
        }

        if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !inp.enterPressed) {
            inp.enterPressed = true;
            // When connected the Map Editor entry is hidden, so shift indices
            int actionIndex = menuSelection;
            if (isConnectedToServer() && menuSelection >= 2) actionIndex = menuSelection + 1;

            switch (actionIndex) {
                case 0: // Play
                    inp.resetNavKeys(); menuJustEntered = true;
                    gameState = PLAYING;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    break;
                case 1: // Map Selection
                    inp.resetNavKeys(); menuJustEntered = true;
                    gameState = MAP_SELECTION;
                    break;
                case 2: // Map Editor
                    inp.resetNavKeys(); menuJustEntered = true;
                    gameState = MAP_EDITOR;
                    editorNoclip = true;
                    editorMode = MODE_BLOCK; // Ensure we start in BLOCK mode
                    editorSpawnMode = SPAWN_NONE; // Ensure we start in block placement mode
                    // Initialize camera position for editor
                    camera.Position = glm::vec3(0.0f, 5.0f, 10.0f);
                    camera.Yaw = -90.0f;
                    camera.Pitch = 0.0f;
                    camera.updateCameraVectors();
                    editorGridPos = camera.Position + camera.Front * 5.0f;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    break;
                case 3: // Connect to Server
                    inp.resetNavKeys(); menuJustEntered = true;
                    gameState = SERVER_CONNECT;
                    serverAddress = "";
                    serverPort = 7777;
                    enteringServerAddress = false;
                    serverConnectSelection = 2; // Start on first button row, not on IP input field
                    break;
                case 4: // Settings
                    inp.resetNavKeys(); menuJustEntered = true;
                    gameState = SETTINGS;
                    settingsSelection = 0;
                    break;
                case 5: // Exit
                    glfwSetWindowShouldClose(window, true);
                    break;
            }
        } else if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
            inp.enterPressed = false;
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
        if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS ||
            glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
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
    
    // Bomb defusal mode - E key for planting/defusing
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        if (bombDefusalMode) {
            if (bomb.planted && !bomb.defused) {
                startDefusing();
            } else if (!bomb.planted) {
                startPlanting();
            }
        }
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
        // Always shoot locally for responsiveness
        // In multiplayer, the server will also broadcast to other players
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
    if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS && !inp.gameplayGPressed) {
        inp.gameplayGPressed = true;
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
    } else if (glfwGetKey(window, GLFW_KEY_G) == GLFW_RELEASE) {
        inp.gameplayGPressed = false;
    }
}