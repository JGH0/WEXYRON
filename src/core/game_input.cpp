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
                              gameState == MAP_SELECTION || gameState == SERVER_CONNECT ||
                              gameState == TEAM_SETUP);
    if (inMenuState && !activeMenuView().stack.current()) {
        if (gameState == MENU) openMainMenu();
        else if (gameState == SETTINGS) openSettings();
        else if (gameState == MAP_SELECTION) openMapSelection();
        else if (gameState == TEAM_SETUP) openTeamSetup();
        else openServerConnect();
    }

    if (gameState == PLAYING) {
        // A playtest is still the editor: F5 or ESC drops back into it instead
        // of opening the match menu.
        if (editorPlaytesting_) {
            syncEditorKeyEdges();
            if (editorKeyPressed(GLFW_KEY_F5) ||
                editorKeyPressed(GLFW_KEY_ESCAPE)) {
                editorStopPlaytest();
                return;
            }
        }
        // ESC opens the menu (unless the player bound another key to it).
        if (bindingDown(keyBindings.menu) && !inp.escapePressed) {
            inp.escapePressed = true;
            inp.resetNavKeys();
            openMainMenu();
            gameState = MENU;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (!bindingDown(keyBindings.menu)) {
            inp.escapePressed = false;
        }
        processGameInput(deltaTime);
    } else if (gameState == MENU || gameState == SETTINGS ||
               gameState == MAP_SELECTION || gameState == SERVER_CONNECT ||
               gameState == TEAM_SETUP) {
        processMenuInput();
    } else if (gameState == MAP_EDITOR) {
        // The editor handles ESC itself so it can warn about unsaved changes.
        processEditorInput(deltaTime);
    } else if (gameState == DEAD) {
        if (!isConnectedToServer()) {
            // Single player: ENTER/SPACE/click restarts the run as before.
            if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS ||
                glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS ||
                glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                resetGame();
            }
        } else {
            // Multiplayer: respawn in place, on a key/click edge (holding the
            // button that got you killed must not instantly respawn you).
            const bool enter = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
            const bool space = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
            const bool click = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            static bool respawnHeld = false;
            if ((enter || space || click) && !respawnHeld) {
                respawnHeld = true;
                respawnLocally();
            } else if (!enter && !space && !click) {
                respawnHeld = false;
            }
        }
    }

    // ── Final cursor mode enforcement ────────────────────────────────
    // Runs after every state's input handler above. If any code path
    // transitioned to PLAYING (mouse click, keyboard ENTER, etc.), the
    // cursor must be hidden. Lightweight per-frame path: no look reset
    // (that happens once on entry via capturePlayCursor(true)).
    if (gameState == PLAYING) {
        capturePlayCursor(false);
    } else if (gameState == MAP_EDITOR) {
        // The editor keeps a visible pointer for its panels; only the
        // right-mouse look owns the pointer (it sets DISABLED itself).
        if (!editorLookActive_) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    } else {
        // Menus and other screens use a visible pointer.
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

bool Game::isInMatchMenu() const {
    if (!gameInProgress_) return false;
    switch (gameState) {
        case MENU:
        case SETTINGS:
        case MAP_SELECTION:
        case SERVER_CONNECT:
        case TEAM_SETUP:
            return true;
        default:
            return false;
    }
}

void Game::resumeGameFromMenu() {
    // Returning to the game re-grabs + forgets the menu cursor position so the
    // first move does not spin.
    inp.resetNavKeys();
    menuJustEntered = true;

    if (pendingRoundStart_) {
        // A round ended while the menu was open. Starting it respawns
        // everyone, so this is exactly the "respawn on return" the player
        // expects (and it keeps round-based modes on their normal cycle).
        const bool serverStarted = pendingRoundStartFromServer_;
        pendingRoundStart_ = false;
        pendingRoundStartFromServer_ = false;
        if (!matchOver_) {
            if (serverStarted) beginServerRound();
            else               startRound();
            return;
        }
    }

    if (isDead || respawnPending_) {
        // Died while browsing the menu: the server held the respawn (or it
        // arrived before we left the menu), so rejoin from a fresh spawn.
        if (bombDefusalMode) {
            // Round-based mode: a dead player spectates until the next round,
            // exactly like a normal death. Do not sneak back into the round.
            respawnPending_ = false;
            gameState = PLAYING;
            capturePlayCursor(true);
            return;
        }
        respawnLocally();
        return;
    }

    gameState = PLAYING;
    capturePlayCursor(true);
}

void Game::closeMenuToGame() {
    // ESC on the bottom page of a menu that was opened from the game returns
    // to it; from the main menu (no game in progress) it just re-opens the
    // main menu itself.
    if (gameState == MENU) {
        openMainMenu();
        return;
    }
    resumeGameFromMenu();
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
        if (gameState == TEAM_SETUP) sendLobbySetup();
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

    // Menu feedback: a click on a row, and a soft blip when the highlighted
    // row changes (keyboard or mouse), so navigating the menus is audible.
    static int lastMenuRow = -1;
    static int lastHoverRow = -1;
    const int selected = view.stack.selection();
    const int hoverRow = menu::hitTest(view, mouse.x, mouse.y);
    if (mouseClicked && !capturing) {
        if (selected != lastMenuRow || selected >= 0) playUiSound("ui_select");
    } else if (hoverRow != lastHoverRow && hoverRow >= 0) {
        playUiSound("ui_hover");
    } else if (selected != lastMenuRow && selected >= 0 && !mouseClicked) {
        playUiSound("ui_hover");
    }
    lastMenuRow = selected;
    lastHoverRow = hoverRow;

    if (lobbyDirty_) {
        lobbyDirty_ = false;
        if (gameState == TEAM_SETUP) {
            refreshTeamSetupRows();
        } else if (gameState == MAP_SELECTION) {
            refreshMapSelectionRows();
        }
    }

    if (gameState == SERVER_CONNECT) {
        processServerConnectTextInput();
        // The list, status and typed values change outside the framework, so
        // rebuild the rows whenever one of them changed. The session state
        // drives the status row and the CONNECT/DISCONNECT row, and the error
        // text changes when a handshake fails or a server goes away.
        static int lastServerCount = -1;
        static bool lastConnected = false;
        static bool lastConnecting = false;
        static std::string lastError;
        static std::string lastAddress;
        static int lastPort = -1;
        const bool connected  = isConnectedToServer();
        const bool connecting = isConnectingToServer();
        if (static_cast<int>(discoveredServers.size()) != lastServerCount ||
            connected != lastConnected || connecting != lastConnecting ||
            getServerConnectError() != lastError ||
            serverAddress != lastAddress || serverPort != lastPort) {
            refreshServerRows();
            lastServerCount = static_cast<int>(discoveredServers.size());
            lastConnected = connected;
            lastConnecting = connecting;
            lastError = getServerConnectError();
            lastAddress = serverAddress;
            lastPort = serverPort;
        }
    }

    // A row action may have changed the state (play, back, exit...).
    if (gameState == PLAYING) {
        capturePlayCursor(true);
    }
}

// ---------------------------------------------------------------------------
// Gameplay input
// ---------------------------------------------------------------------------

bool Game::keyDown(int key) const {
    return window && key >= 0 && !settings::isMouseBinding(key) &&
           glfwGetKey(window, key) == GLFW_PRESS;
}

bool Game::mouseDown(int button) const {
    return window && button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST &&
           glfwGetMouseButton(window, button) == GLFW_PRESS;
}

bool Game::bindingDown(int code) const {
    const int button = settings::mouseButtonOf(code);
    return button >= 0 ? mouseDown(button) : keyDown(code);
}

void Game::processGameInput(float deltaTime) {
    // Dead spectators only fly the camera (updateSpectator); they must not
    // shoot, switch weapons or plant/defuse while the round continues.
    if (spectatorMode) {
        pendingWeaponScroll_ = 0;
        return;
    }

    // Match over: ENTER starts a fresh match; other gameplay input is frozen.
    if (matchOver_) {
        if (!isConnectedToServer()) {
            const bool enter = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS ||
                               glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS;
            if (enter && !matchEnterHeld_) restartMatch();
            matchEnterHeld_ = enter;
        }
        pendingWeaponScroll_ = 0;
        return;
    }
    matchEnterHeld_ = false;

    // Counter-Strike style round end: players may still walk around for a
    // few seconds, but shooting and bomb interactions are disabled and any
    // plant/defuse in progress is cancelled.
    const bool roundIntermission =
        matchOver_ || (roundEndTimer > 0.0f && currentGameMode == BOMB_DEFUSAL);
    if (roundIntermission) {
        isPlanting = false;
        isDefusing = false;
        plantProgress = 0.0f;
        defuseProgress = 0.0f;
    }

    // Mouse wheel weapon switch (queued by scrollCallback). One notch per
    // frame; scrolling no longer changes the camera FOV.
    if (pendingWeaponScroll_ != 0) {
        const int direction = pendingWeaponScroll_ > 0 ? 1 : -1;
        pendingWeaponScroll_ = 0;
        cycleWeapon(direction);
    }

    // Unstuck (U by default, rebindable) - move player to a safe centre spot.
    if (bindingDown(keyBindings.unstuck)) {
        renderer.markCameraCut();   // teleport: no motion history
        playerPos = glm::vec3(0.0f, 2.0f, 0.0f); // Reset to center well above floor
        playerVelocity = glm::vec3(0.0f, 0.0f, 0.0f); // Stop all movement
        stance_ = STANCE_STANDING;
        eyeHeightCurrent_ = eyeHeight;
        camera.Position = glm::vec3(0.0f, playerPos.y + eyeHeight, 0.0f); // Reset camera
    }
    
    // Bomb defusal mode - USE key (E by default) for planting/defusing.
    // Only the attacking team may plant and only the defending team may
    // defuse. Checking the role before calling the action also avoids the
    // per-frame "cannot defuse your own bomb" console spam while E is held
    // next to a bomb that was just planted.
    if (bombDefusalMode) {
        const bool useHeld = !roundIntermission && bindingDown(keyBindings.use);
        // If the server has already assigned the interaction to another
        // player or a bot, do not start a competing local request. Otherwise
        // the local state would flip on and off with every state packet and
        // replay the start sound.
        const uint32_t selfId = networkClient ? networkClient->getClientId() : 0;
        const bool otherIsPlanting = serverPlantActor_ != 0 && serverPlantActor_ != selfId;
        const bool otherIsDefusing = serverDefuseActor_ != 0 && serverDefuseActor_ != selfId;
        if (useHeld) {
            if (bomb.planted) {
                if (!bomb.defused && playerTeam != bomb.plantingTeam && !otherIsDefusing) {
                    startDefusing();
                }
            } else if (playerTeam == attackingTeam && !otherIsPlanting) {
                startPlanting();
            }
        }

        // Releasing USE (or the round ending) cancels the local interaction.
        // The server expires the request as soon as the re-sends stop.
        if (!useHeld) {
            isPlanting = false;
            isDefusing = false;
            plantProgress = 0.0f;
            defuseProgress = 0.0f;
            bombInteractResend_ = 0.0f;
        } else if (networkClient && networkClient->isConnected()) {
            // Walked off the site / away from the bomb while still holding
            // E: drop the local interaction instead of crouching in place.
            // The bomb becoming planted ends a plant; it is not a reason to
            // cancel one that is still in progress.
            if (isPlanting && (bomb.planted || getNearbyBombSite() < 0)) {
                isPlanting = false;
                plantProgress = 0.0f;
            }
            if (isDefusing && (bomb.defused || !isNearBomb())) {
                isDefusing = false;
                defuseProgress = 0.0f;
            }
        }
    }

    // ── Stance (sprint / sneak / prone) ─────────────────────────────────────
    // Sneak is held. Lying down is a toggle or a hold, depending on the
    // "C LIE TOGGLE" gameplay setting. Standing up is refused when the body
    // would not fit under a low ceiling, so the player stays crouched instead
    // of being pushed into geometry.
    const bool sprintHeld = debugKeyDownForTest(keyBindings.sprint);
    const bool sneakHeld  = debugKeyDownForTest(keyBindings.sneak);
    const bool proneHeld  = debugKeyDownForTest(keyBindings.prone);

    const bool proneToggle = gameSettings_.proneToggle;
    if (proneToggle) {
        if (proneHeld && !inp.gameplayPronePressed) {
            inp.gameplayPronePressed = true;
            if (stance_ == STANCE_PRONE) {
                if (canStandAtPlayer() && !sneakHeld) {
                    stance_ = STANCE_STANDING;
                } else {
                    stance_ = bodyFitsHeight(playerPos, 1.2f) ? STANCE_CROUCHING
                                                              : STANCE_PRONE;
                }
            } else {
                stance_ = STANCE_PRONE;
            }
        } else if (!proneHeld) {
            inp.gameplayPronePressed = false;
        }
    } else {
        // Held mode: lying down only while the key is down.
        inp.gameplayPronePressed = false;
        if (proneHeld) {
            stance_ = STANCE_PRONE;
        } else if (stance_ == STANCE_PRONE) {
            if (sneakHeld) {
                stance_ = STANCE_CROUCHING;
            } else if (canStandAtPlayer()) {
                stance_ = STANCE_STANDING;
            } else {
                stance_ = bodyFitsHeight(playerPos, 1.2f) ? STANCE_CROUCHING
                                                          : STANCE_PRONE;
            }
        }
    }

    if (stance_ != STANCE_PRONE) {
        if (sneakHeld) {
            stance_ = STANCE_CROUCHING;
        } else if (stance_ == STANCE_CROUCHING && canStandAtPlayer()) {
            // Crouching was only held down; stand back up when there is room.
            stance_ = STANCE_STANDING;
        }
    }

    // Planting / defusing puts the player into the low, crouched pose used by
    // CS and Valorant. The body stays put until the interaction ends.
    const bool bombInteraction = isPlanting || isDefusing;
    if (bombInteraction) stance_ = STANCE_CROUCHING;

    // Movement using key bindings (from the settings file)
    glm::vec3 movement(0.0f);
    
    if (debugKeyDownForTest(keyBindings.forward)) {
        movement.x += cos(glm::radians(camera.Yaw));
        movement.z += sin(glm::radians(camera.Yaw));
    }
    if (debugKeyDownForTest(keyBindings.backward)) {
        movement.x -= cos(glm::radians(camera.Yaw));
        movement.z -= sin(glm::radians(camera.Yaw));
    }
    if (debugKeyDownForTest(keyBindings.left)) {
        movement.x += cos(glm::radians(camera.Yaw - 90.0f));
        movement.z += sin(glm::radians(camera.Yaw - 90.0f));
    }
    if (debugKeyDownForTest(keyBindings.right)) {
        movement.x += cos(glm::radians(camera.Yaw + 90.0f));
        movement.z += sin(glm::radians(camera.Yaw + 90.0f));
    }

    const bool moving = glm::length(movement) > 0.0001f;
    if (moving) {
        movement = glm::normalize(movement);
    }

    float moveSpeed = 5.0f;
    switch (stance_) {
        case STANCE_CROUCHING: moveSpeed = 2.6f; break;
        case STANCE_PRONE:     moveSpeed = 1.2f; break;
        case STANCE_STANDING:
        default:
            moveSpeed = (sprintHeld && moving) ? 8.5f : 5.0f;
            break;
    }

    // Apply movement to velocity. A bomb interaction locks the body in place.
    if (moving && !bombInteraction) {
        playerVelocity.x = movement.x * moveSpeed;
        playerVelocity.z = movement.z * moveSpeed;
    } else {
        // Stop horizontal movement when no keys pressed (or while interacting).
        playerVelocity.x = 0.0f;
        playerVelocity.z = 0.0f;
    }
    
    // Jump input: holding the key keeps jumping. handleJump() only starts a jump
    // from the ground, so the press jumps at once and a held key jumps again the
    // moment the feet touch the floor (bunny-hop), never mid-air.
    inp.jumpHeld = debugKeyDownForTest(keyBindings.jump) &&
                   stance_ != STANCE_PRONE && !bombInteraction;
    if (inp.jumpHeld) {
        if (isOnGround()) handleJump();   // no point above the floor
    }
    
    // Shoot input: mouse button by default, rebindable in Settings > Controls
    // (the binding may also be a key). No firing between rounds or mid-plant.
    const bool shootDown = bindingDown(gameSettings_.bindShoot);
    if (shootDown && !roundIntermission && !bombInteraction) {
        shoot();
    }
    
    // Weapon switching input.
    // Key 1..5 selects the weapon in array order (1 pistol, 2 shotgun,
    // 3 sniper, 4 grenade, 5 flashbang); the grenade key toggles
    // grenade/flashbang. Only the press edge is handled, so holding a key
    // doesn't re-trigger it.
    static const WeaponType weaponForKey[5] = {
        PISTOL, PUMP_SHOTGUN, SNIPER, GRENADE, FLASHBANG
    };
    for (int key = 0; key < 5; key++) {
        if (!bindingDown(keyBindings.weaponSlot[key])) {
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
    if (bindingDown(keyBindings.grenade)) {
        if (!inp.gameplayGPressed) {
            inp.gameplayGPressed = true;
            switchWeapon(currentWeapon == GRENADE ? FLASHBANG : GRENADE);
        }
    } else {
        inp.gameplayGPressed = false;
    }

    // Inspect: play the weapon's inspection animation. Only the press edge
    // starts it, and it is allowed to finish even while other keys are held.
    if (bindingDown(keyBindings.inspect)) {
        if (!inp.gameplayInspectPressed) {
            inp.gameplayInspectPressed = true;
            weaponInspect_ = kWeaponInspectDuration;
        }
    } else {
        inp.gameplayInspectPressed = false;
    }
}
