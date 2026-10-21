/**
 * @file game_input.cpp
 * @brief Input routing: ESC handling and the per-state dispatch
 *
 * The game states that show a menu (MENU, SETTINGS, MAP_SELECTION,
 * SERVER_CONNECT) are all driven by the menu framework: processMenuInput()
 * builds the per-frame mouse/keyboard state and calls menu::processInput().
 * This file only decides which state gets the input and handles the
 * gameplay-specific keys in the remaining states.
 */

#define GLFW_INCLUDE_NONE
#include "game.hpp"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

/// Build the framework's mouse state from the cursor position in framebuffer
/// pixels (the same mapping UIHelpers uses for drawing).
static menu::MouseState makeMenuMouse(GLFWwindow* window, double mx, double my, bool clicked) {
    int fbW = 800, fbH = 600;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = 800;
    if (fbH <= 0) fbH = 600;
    const float designW = 800.0f, designH = 600.0f;
    float uiScale = std::min(static_cast<float>(fbW) / designW, static_cast<float>(fbH) / designH);
    if (uiScale <= 0.0f) uiScale = 1.0f;
    const float offX = (static_cast<float>(fbW) / uiScale - designW) * 0.5f;
    const float offY = (static_cast<float>(fbH) / uiScale - designH) * 0.5f;

    menu::MouseState mouse;
    mouse.x = static_cast<float>(mx) / uiScale - offX;
    mouse.y = static_cast<float>(my) / uiScale - offY;
    mouse.inside = true;
    mouse.clicked = clicked;
    return mouse;
}

/// Input callbacks for the framework, backed by Game::menuKeyPressed().
static menu::MenuInput makeMenuInput(Game& game, GLFWwindow* window) {
    menu::MenuInput in;
    in.pressed = [&game, window](int key) { return game.menuKeyPressed(key, window); };
    in.held = [window](int key) { return glfwGetKey(window, key) == GLFW_PRESS; };
    in.isModifier = [](int key) {
        return key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT ||
               key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL ||
               key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT ||
               key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER;
    };
    in.onCaptureChanged = [&game, window](bool capturing) {
        // Key edges from the capture (ENTER opens it, ESC cancels it) must not
        // leak into the state that comes after a successful binding.
        if (!capturing) game.resetMenuKeyEdges(window);
    };
    return in;
}

void Game::processInput(float deltaTime) {
    // Menu key edges are re-read once per frame (never between the menu's
    // separate queries for the same press, e.g. ENTER and KP ENTER).
    if (inp.menuEdgesNeedSync) syncMenuEdges(window);
    // ── Mouse click edge detection ───────────────────────────────────
    bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    mouseClicked = mouseDown && !prevMouseDown;
    prevMouseDown = mouseDown;

    // Convert framebuffer mouse coords to virtual UI coords for GUI hit-testing
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    {   // Same scaling as UIHelpers::getBounds — project from actual fb pixels to virtual 800x600
        int fbW = 800, fbH = 600;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW <= 0) fbW = 800;
        if (fbH <= 0) fbH = 600;
        const float DESIGN_W = 800.0f, DESIGN_H = 600.0f;
        float scaleX = (float)fbW / DESIGN_W;
        float scaleY = (float)fbH / DESIGN_H;
        float uiScale = std::min(scaleX, scaleY);
        if (uiScale <= 0.0f) uiScale = 1.0f;
        float uiW = (float)fbW / uiScale;
        float uiH = (float)fbH / uiScale;
        float offX = (uiW - DESIGN_W) * 0.5f;
        float offY = (uiH - DESIGN_H) * 0.5f;
        // The ortho projection flips Y once, so canvas y grows downwards and
        // matches the cursor position directly (no Y flip needed).
        mouseGUIPosX = (float)mx / uiScale - offX;
        mouseGUIPosY = (float)my / uiScale - offY;
    }

    // ── State dispatch ────────────────────────────────────────────────
    menuActive_ = false;

    // A menu state always needs a page to show. If one is missing (e.g. the
    // state was set before the menu was built) the screen would be blank, so
    // build the menu for it now. (Only then: openMainMenu() also resets the
    // key edges and must not run on every frame.)
    const bool inMenuState = (gameState == MENU || gameState == SETTINGS ||
                              gameState == MAP_SELECTION || gameState == SERVER_CONNECT);
    if (inMenuState && !activeMenuView().stack.current()) {
        if (gameState == MENU) openMainMenu();
        else if (gameState == SETTINGS) openSettings();
        else if (gameState == MAP_SELECTION) openMapSelection();
        else openServerConnect();
    }

    if (gameState == PLAYING) {
        // ESC opens the menu (unless the player bound another key to it).
        if (glfwGetKey(window, keyBindings.menu) == GLFW_PRESS && !inp.escapePressed) {
            inp.escapePressed = true;
            inp.resetNavKeys();
            openMainMenu();
            gameState = MENU;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (glfwGetKey(window, keyBindings.menu) == GLFW_RELEASE) {
            inp.escapePressed = false;
        }
        processGameInput(deltaTime);
    } else if (gameState == MENU || gameState == SETTINGS ||
               gameState == MAP_SELECTION || gameState == SERVER_CONNECT) {
        processMenuInput();
    } else if (gameState == MAP_EDITOR) {
        // ESC leaves the editor back to the main menu.
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !inp.escapePressed) {
            inp.escapePressed = true;
            inp.resetNavKeys();
            menuJustEntered = true;
            openMainMenu();
            gameState = MENU;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
            inp.escapePressed = false;
        }
        processEditorInput();
    } else if (gameState == DEAD) {
        if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS ||
            glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            resetGame();
        }
    }

    // ── Final cursor mode enforcement ────────────────────────────────
    // Runs after every state's input handler above. If any code path
    // transitioned to PLAYING (mouse click, keyboard ENTER, etc.), the
    // cursor must be hidden.
    if (gameState == PLAYING) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    } else if (gameState != MAP_EDITOR) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void Game::closeMenuToGame() {
    // ESC on the bottom page of a menu that was opened from the game returns
    // to it; from the main menu (no game in progress) it just re-opens the
    // main menu itself.
    inp.resetNavKeys();
    menuJustEntered = true;
    if (gameState == MENU) {
        openMainMenu();
        return;
    }
    gameState = PLAYING;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

// ---------------------------------------------------------------------------
// Menu input (framework)
// ---------------------------------------------------------------------------

void Game::processMenuInput() {
    menu::MenuView& view = activeMenuView();
    menuActive_ = true;

    // First frame after the screen opened: only refresh navigation edges so a
    // key still held from the previous screen does not activate something.
    if (menuJustEntered) {
        inp.resetNavKeys();   // clears the edges and re-arms the key re-sync
        menuJustEntered = false;
        if (gameState == SERVER_CONNECT) refreshServerRows();
        return;
    }

    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    const menu::MouseState mouse = makeMenuMouse(window, mx, my, mouseClicked);
    const menu::MenuInput input = makeMenuInput(*this, window);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    const bool capturing = menu::isCapturing(view);
    menu::processInput(view, window, mouse, input);

    // Hand cursor over selectable rows (never while capturing a key).
    if (!capturing && !menu::isCapturing(view)) {
        const int hovered = menu::hitTest(view, mouse.x, mouse.y);
        glfwSetCursor(window, hovered >= 0 ? handCursor : nullptr);
    } else {
        glfwSetCursor(window, nullptr);
    }

    if (gameState == SERVER_CONNECT) {
        processServerConnectTextInput();
        // The list, status and typed values change outside the framework, so
        // rebuild the rows whenever one of them changed.
        static int lastServerCount = -1;
        static bool lastConnected = false;
        static std::string lastAddress;
        static int lastPort = -1;
        if (static_cast<int>(discoveredServers.size()) != lastServerCount ||
            isConnectedToServer() != lastConnected ||
            serverAddress != lastAddress || serverPort != lastPort) {
            refreshServerRows();
            lastServerCount = static_cast<int>(discoveredServers.size());
            lastConnected = isConnectedToServer();
            lastAddress = serverAddress;
            lastPort = serverPort;
        }
    }

    // A row action may have changed the state (play, back, exit...).
    if (gameState == PLAYING) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
}

// ---------------------------------------------------------------------------
// Gameplay input
// ---------------------------------------------------------------------------

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
    
    // Movement using key bindings (from the settings file)
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
    
    // Shoot input: mouse button by default, rebindable in Settings > Controls
    // (the binding may also be a key). glfwGetMouseButton() only accepts mouse
    // buttons and glfwGetKey() only accepts keys, hence the two checks.
    const bool shootDown =
        (keyBindings.shootButton >= 0 &&
         glfwGetMouseButton(window, keyBindings.shootButton) == GLFW_PRESS) ||
        (keyBindings.shootKey >= 0 && glfwGetKey(window, keyBindings.shootKey) == GLFW_PRESS);
    if (shootDown) {
        shoot();
        inputProcessed = true;
    }
    
    // Weapon switching input.
    // Key 1..4 selects the weapon in array order; the grenade key toggles
    // grenade/flashbang. Only the press edge is handled, so holding a key
    // doesn't re-trigger it.
    static const WeaponType weaponForKey[4] = { PISTOL, PUMP_SHOTGUN, GRENADE, FLASHBANG };
    for (int key = 0; key < 4; key++) {
        if (glfwGetKey(window, GLFW_KEY_1 + key) != GLFW_PRESS) {
            inp.gameplayWeaponPressed[key] = false;
            continue;
        }
        if (!inp.gameplayWeaponPressed[key]) {
            inp.gameplayWeaponPressed[key] = true;
            switchWeapon(weaponForKey[key]);
        }
    }

    // Grenade key: toggle between the two throwables (selecting the throwable
    // you're not holding, or the grenade if you're holding a gun).
    if (glfwGetKey(window, keyBindings.grenade) == GLFW_PRESS) {
        if (!inp.gameplayGPressed) {
            inp.gameplayGPressed = true;
            switchWeapon(currentWeapon == GRENADE ? FLASHBANG : GRENADE);
        }
    } else {
        inp.gameplayGPressed = false;
    }
}
