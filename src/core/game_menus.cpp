/**
 * @file game_menus.cpp
 * @brief Menu screens: main menu, settings, controls, map selection
 *
 * Every screen is built with the menu framework (ui/menu.hpp + ui/menu_pages.hpp):
 * a screen is a menu::Page (title + rows) shown in a menu::MenuView, and the
 * framework handles drawing, selection, scrolling, mouse handling and key
 * capture. This file only describes the screens and wires their rows to game
 * actions.
 *
 *   openMainMenu()    – (re)build the main menu for the current game state
 *   openSettings()    – settings page (values) + controls page (rebinding)
 *   openMapSelection()– map list + game mode
 *   menu()            – draw the active menu
 *   processMenuInput() (game_input.cpp) – route input to the active menu
 *
 * The connect-to-server screen lives in game_server_menu.cpp, the settings
 * file and value model in core/settings.cpp.
 */

#define GLFW_INCLUDE_NONE
#include "game.hpp"
#include "../ui/ui_helpers.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

// ---------------------------------------------------------------------------
// View management
// ---------------------------------------------------------------------------

void Game::openMenuView(menu::MenuView& view, menu::Page page) {
    view.theme = menu::Theme();
    view.legend = {{"UP/DOWN", "SELECT"}, {"ENTER", "CONFIRM"}, {"ESC", "BACK"}};
    view.onClose = nullptr;
    // Drop hit rects from the previous page: a click before the first draw of
    // the new page must not activate a row of the old one.
    view.hits.clear();
    view.hoverIndex = -1;
    menu::begin(view, std::move(page));
    menuJustEntered = true;   // ignore keys still held from the previous screen
}

menu::MenuView& Game::activeMenuView() {
    switch (gameState) {
        case SETTINGS:        return settingsMenu_;
        case MAP_SELECTION:   return mapMenu_;
        case SERVER_CONNECT:  return serverMenu_;
        case MENU:
        default:              return mainMenu_;
    }
}

const menu::MenuView& Game::activeMenuViewConst() const {
    switch (gameState) {
        case SETTINGS:        return settingsMenu_;
        case MAP_SELECTION:   return mapMenu_;
        case SERVER_CONNECT:  return serverMenu_;
        case MENU:
        default:              return mainMenu_;
    }
}

void Game::menu() {
    menu::draw(activeMenuView(), renderer, window);
}

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------

void Game::openMainMenu() {
    inp.resetNavKeys();
    menuJustEntered = true;

    menu::Page page("CUBE CS LITE", "MAIN MENU");
    if (isConnectedToServer()) {
        page.add(menu::info("CONNECTION", std::to_string(playerCount) + " PLAYERS ONLINE",
                            "Connected to a LAN server"));
    }

    // The menu can be opened before the first game starts (nothing to
    // resume) and from the game itself (the first row resumes it).
    const bool resumable = gameInProgress_;
    page.add(menu::action(resumable ? "RESUME GAME" : "PLAY GAME", [this]() {
        inp.resetNavKeys();
        menuJustEntered = true;
        gameState = PLAYING;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }, resumable ? "Back to the match" : "Start a match on the selected map"));

    page.add(menu::action("MAP SELECTION", [this]() {
        openMapSelection();
        gameState = MAP_SELECTION;
    }, "Choose the map and game mode"));

    if (!isConnectedToServer()) {
        page.add(menu::action("MAP EDITOR", [this]() {
            inp.resetNavKeys();
            menuJustEntered = true;
            gameState = MAP_EDITOR;
            editorNoclip = true;
            editorMode = MODE_BLOCK;
            editorSpawnMode = SPAWN_NONE;
            camera.Position = glm::vec3(0.0f, 5.0f, 10.0f);
            camera.Yaw = -90.0f;
            camera.Pitch = 0.0f;
            camera.updateCameraVectors();
            editorGridPos = camera.Position + camera.Front * 5.0f;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }, "Build and save maps"));
    }

    page.add(menu::action("CONNECT TO SERVER", [this]() {
        openServerConnect();
        gameState = SERVER_CONNECT;
    }, "Play over the local network"));

    page.add(menu::action("SETTINGS", [this]() {
        openSettings();
        gameState = SETTINGS;
    }, "Video, audio, gameplay and controls"));

    page.add(menu::action("EXIT", [this]() {
        glfwSetWindowShouldClose(window, true);
    }, "Close the game"));
    page.rows.back().destructive = true;

    openMenuView(mainMenu_, std::move(page));
    mainMenu_.legend = {{"UP/DOWN", "SELECT"}, {"ENTER", "CONFIRM"}, {"ESC", "CLOSE"}};
    menuSelection = mainMenu_.stack.selection();
}

// ---------------------------------------------------------------------------
// Settings + controls
// ---------------------------------------------------------------------------

void Game::openSettings() {
    // One stack: SETTINGS (values). CONTROLS is pushed on top when chosen, and
    // ESC returns to SETTINGS with the CONTROLS row still selected.
    menu::Page page = menu::buildSettingsPage(settingDefs_, [this]() {
        // Volume/gameplay change from the menu: apply it and play a feedback
        // blip so the new volume can be heard immediately.
        applySettings();
        saveSettings();
        playUiSound("ui_select");
    });
    page.add(menu::separator("INPUT"));
    page.add(menu::action("CONTROLS", [this]() {
        // Built fresh on every open: bindings may have changed since the
        // settings page was created.
        menu::Page controls = menu::buildControlsPage(settingDefs_, [this]() { saveSettings(); });
        controls.add(menu::separator("ACTIONS"));
        controls.add(menu::action("RESET BINDINGS", [this]() { resetBindingsToDefaults(); },
                                  "Restore the default key bindings"));
        menu::push(settingsMenu_, std::move(controls));
        settingsMenu_.legend = {{"ENTER", "REBIND"}, {"R", "DEFAULTS"}, {"ESC", "BACK"}};
    }, "Change the keyboard and mouse bindings"));

    openMenuView(settingsMenu_, std::move(page));
    settingsMenu_.legend = {{"UP/DOWN", "SELECT"}, {"ENTER", "CHANGE"},
                            {"LEFT/RIGHT", "ADJUST"}, {"ESC", "LEAVE"}};
    settingsMenu_.onClose = [this]() { closeMenuToGame(); };
}

void Game::appendBindingRows(menu::Page& page) {
    menu::appendBindingRows(page, settingDefs_, [this]() { saveSettings(); });
}

void Game::resetBindingsToDefaults() {
    const settings::GameSettings defaults;
    gameSettings_.bindForward  = defaults.bindForward;
    gameSettings_.bindBackward = defaults.bindBackward;
    gameSettings_.bindLeft     = defaults.bindLeft;
    gameSettings_.bindRight    = defaults.bindRight;
    gameSettings_.bindJump     = defaults.bindJump;
    gameSettings_.bindShoot    = defaults.bindShoot;
    gameSettings_.bindGrenade  = defaults.bindGrenade;
    gameSettings_.bindMenu     = defaults.bindMenu;

    // Refresh the key caps shown on the controls page.
    menu::Page* page = settingsMenu_.stack.current();
    if (page) {
        for (menu::Row& row : page->rows) {
            if (row.kind != menu::RowKind::Keybind) continue;
            for (const settings::SettingDef& def : settingDefs_) {
                if (def.label == row.label && def.value->type == settings::ValueType::Key) {
                    row.code = def.value->i;
                    break;
                }
            }
        }
    }
    settings::syncSettingDefs(gameSettings_, settingDefs_);
    applySettings();
    saveSettings();
}

// ---------------------------------------------------------------------------
// Map selection
// ---------------------------------------------------------------------------

void Game::openMapSelection() {
    menu::Page page("SELECT MAP", "CHOOSE A MAP AND A GAME MODE");
    if (mapDisplayNames.empty()) {
        page.add(menu::info("MAPS", "NONE FOUND", "No .map files in assets/maps"));
    }
    page.add(menu::separator("MAPS"));
    for (size_t i = 0; i < mapDisplayNames.size(); ++i) {
        std::string label = mapDisplayNames[i];
        if (isConnectedToServer() && i < mapPlayerCounts.size() && mapPlayerCounts[i] > 0) {
            label += "   [" + std::to_string(mapPlayerCounts[i]) + "]";
        }
        page.add(menu::action(label, [this, i]() {
            mapSelection = static_cast<int>(i);
            startSelectedMap(mapFiles[i]);
        }, "Play this map"));
    }

    static const char* const kModeNames[] = {"DEATHMATCH", "TEAM VS TEAM", "BOMB DEFUSAL"};
    page.add(menu::separator("GAME MODE"));
    page.add(menu::cycle("MODE", {kModeNames[0], kModeNames[1], kModeNames[2]},
                         static_cast<int>(currentGameMode),
                         [this](int index) { currentGameMode = static_cast<GameMode>(index); },
                         "LEFT/RIGHT change the mode"));

    openMenuView(mapMenu_, std::move(page));
    mapMenu_.legend = {{"UP/DOWN", "MAP"}, {"LEFT/RIGHT", "MODE"},
                       {"ENTER", "PLAY"}, {"ESC", "LEAVE"}};
    mapMenu_.onClose = [this]() { closeMenuToGame(); };
}

// ---------------------------------------------------------------------------
// Starting a map
// ---------------------------------------------------------------------------

void Game::startSelectedMap(const std::string& mapFile) {
    static const char* const kModeNames[] = {"Deathmatch", "Team vs Team", "Bomb Defusal"};
    const int modeIndex = static_cast<int>(currentGameMode);
    std::cout << "Loading " << mapFile << " with game mode "
              << kModeNames[(modeIndex >= 0 && modeIndex < 3) ? modeIndex : 0] << "..." << std::endl;

    // Tell the server (if any) and drop remote players from the old map.
    if (networkClient && networkClient->isConnected()) {
        networkClient->sendMapChange(mapFile);
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
        playerCount = 1;
    }

    // Loads on a worker thread when possible; the menu stays responsive and
    // the map is swapped in by pollMapLoad() once ready.
    requestMapLoad(mapFile);

    if (currentGameMode == TEAM_VS_TEAM || currentGameMode == BOMB_DEFUSAL) {
        playerTeam = 0;
    }
    menuJustEntered = true;
    gameInProgress_ = true;
    gameState = PLAYING;
    playUiSound("round_start");
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

// ---------------------------------------------------------------------------
// Settings persistence
// ---------------------------------------------------------------------------

bool Game::saveSettings() {
    settings::SettingsFile file(settings::defaultSettingsPath());
    const bool ok = file.save(settingDefs_);
    if (ok) rememberSettingsText();
    return ok;
}

void Game::rememberSettingsText() {
    settingFileText_.clear();
    for (const settings::SettingDef& def : settingDefs_) {
        settingFileText_.push_back(settings::serializeValue(def));
    }
}

void Game::applySettings() {
    gameSettings_.maxFps = std::max(0, std::min(gameSettings_.maxFps, 1000));
    gameSettings_.performance = std::max(0, std::min(gameSettings_.performance, 2));

    // Mirror into the engine members the rest of the game uses.
    showFps = gameSettings_.showFps;
    maxFps = gameSettings_.maxFps;
    vsyncEnabled = gameSettings_.vsync;
    setPerformanceLevel(gameSettings_.performance);
    if (glfwGetCurrentContext()) glfwSwapInterval(vsyncEnabled ? 1 : 0);

    // Infinite ammo is read from the game member by the gameplay code, and
    // mirrored into the weapons (their own check is what stops ammo loss).
    if (gameSettings_.infiniteAmmo != infiniteAmmoMode) {
        infiniteAmmoMode = gameSettings_.infiniteAmmo;
        if (pistol)    pistol->setInfiniteAmmo(infiniteAmmoMode);
        if (shotgun)   shotgun->setInfiniteAmmo(infiniteAmmoMode);
        if (grenade)   grenade->setInfiniteAmmo(infiniteAmmoMode);
        if (flashbang) flashbang->setInfiniteAmmo(infiniteAmmoMode);
    }

    // Audio: volumes apply live (the menu previews them through the mixer).
    audio.setMasterVolume(gameSettings_.masterVolume / 100.0f);
    audio.setSFXVolume(gameSettings_.effectsVolume / 100.0f);
    audio.setMusicVolume(gameSettings_.musicVolume / 100.0f);

    keyBindings.forward  = gameSettings_.bindForward;
    keyBindings.backward = gameSettings_.bindBackward;
    keyBindings.left     = gameSettings_.bindLeft;
    keyBindings.right    = gameSettings_.bindRight;
    keyBindings.jump     = gameSettings_.bindJump;
    // Bindings are stored as one integer for keys *and* mouse buttons:
    // keyboard codes are 32..348 and mouse buttons are encoded as 1000 + n.
    // glfwGetKeyName/glfwGetKey belong to keyboard codes only, so the engine
    // needs the two split apart here.
    keyBindings.shootButton = settings::mouseButtonOf(gameSettings_.bindShoot);
    keyBindings.shootKey    = keyBindings.shootButton >= 0 ? -1 : gameSettings_.bindShoot;
    keyBindings.shoot       = keyBindings.shootKey;   // kept for compatibility
    keyBindings.grenade  = gameSettings_.bindGrenade;
    keyBindings.menu     = gameSettings_.bindMenu;
}

void Game::loadSettings() {
    settingDefs_ = settings::makeGameSettingDefs(gameSettings_);
    settings::SettingsFile file(settings::defaultSettingsPath());
    file.load(settingDefs_);
    if (!file.lastMessage().empty()) std::cout << file.lastMessage() << std::endl;

    // The file load updated the registry rows; write them into the game's
    // settings and apply them to the engine.
    settings::applySettingDefs(gameSettings_, settingDefs_);
    applySettings();

    rememberSettingsText();

    std::cout << "Settings loaded:";
    for (const settings::SettingDef& def : settingDefs_) {
        std::cout << " " << def.key << "=" << settings::serializeValue(def);
    }
    std::cout << std::endl;
}

void Game::pollSettingsFile() {
    // Once a second is plenty for a hand-edited file and keeps the game free
    // of file I/O in the frame loop.
    const double now = glfwGetTime();
    if (now < nextSettingsPoll_) return;
    nextSettingsPoll_ = now + 1.0;

    namespace fs = std::filesystem;
    const std::string path = settings::defaultSettingsPath();
    std::error_code ec;
    const fs::file_time_type stamp = fs::last_write_time(path, ec);
    if (ec) return;                          // file missing / not readable
    if (stamp == settingsFileTime_) return;  // not touched since last look
    settingsFileTime_ = stamp;

    // The file was changed outside the game (editor, another process). Apply
    // every value whose text differs from what the game last used.
    settings::SettingsFile file(path);
    std::vector<settings::SettingDef>& defs = settingDefs_;
    if (!file.load(defs)) return;

    bool changed = false;
    for (size_t i = 0; i < defs.size(); ++i) {
        const std::string text = settings::serializeValue(defs[i]);
        if (i < settingFileText_.size() && text == settingFileText_[i]) continue;
        if (defs[i].apply) defs[i].apply(*defs[i].value);
        changed = true;
    }
    if (!changed) return;

    // Update the values the menu shows, then apply to the engine.
    settings::syncSettingDefs(gameSettings_, defs);
    applySettings();
    rememberSettingsText();

    // Rebuild the settings screen so its rows show the new values.
    if (gameState == SETTINGS) {
        const menu::Page* page = settingsMenu_.stack.current();
        const bool onControls = page && page->title == "CONTROLS";
        openSettings();
        if (onControls) {
            menu::Page controls("CONTROLS", "KEYBOARD AND MOUSE BINDINGS");
            appendBindingRows(controls);
            controls.add(menu::separator("ACTIONS"));
            controls.add(menu::action("RESET BINDINGS", [this]() { resetBindingsToDefaults(); },
                                      "Restore the default key bindings"));
            menu::push(settingsMenu_, std::move(controls));
            settingsMenu_.legend = {{"ENTER", "REBIND"}, {"R", "DEFAULTS"}, {"ESC", "BACK"}};
        }
        menuJustEntered = true;
    }
    std::cout << "Settings reloaded from " << path << std::endl;
}

// ---------------------------------------------------------------------------
// Key edges for the menu framework
// ---------------------------------------------------------------------------

void Game::syncMenuEdges(GLFWwindow* window) {
    // Called once per frame, before the menu asks about any key. A key that is
    // still physically down counts as "already handled": holding ENTER after a
    // screen change must not activate a row on the new screen, and must not
    // repeat the action every frame.
    inp.menuEdgesNeedSync = false;
    inp.upPressed    = glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS;
    inp.downPressed  = glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS;
    inp.leftPressed  = glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS;
    inp.rightPressed = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;
    inp.enterPressed = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS ||
                       glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS;
    inp.escapePressed = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    inp.upHeld = inp.downHeld = inp.leftHeld = inp.rightHeld = 0.0;
}

bool Game::menuKeyPressed(int key, GLFWwindow* window) {
    // Only ever look at the *physical* state of `key`: the menu asks about many
    // keys per frame, so "this key is not down" must not clear another key's
    // edge (that made the same key press fire twice, or be lost).
    const bool down = glfwGetKey(window, key) == GLFW_PRESS;

    bool* edge = nullptr;
    double* held = nullptr;
    double* last = nullptr;
    switch (key) {
        case GLFW_KEY_UP:    edge = &inp.upPressed;    held = &inp.upHeld;    last = &inp.upLast;    break;
        case GLFW_KEY_DOWN:  edge = &inp.downPressed;  held = &inp.downHeld;  last = &inp.downLast;  break;
        case GLFW_KEY_LEFT:  edge = &inp.leftPressed;  held = &inp.leftHeld;  last = &inp.leftLast;  break;
        case GLFW_KEY_RIGHT: edge = &inp.rightPressed; held = &inp.rightHeld; last = &inp.rightLast; break;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER: edge = &inp.enterPressed; break;
        case GLFW_KEY_ESCAPE:   edge = &inp.escapePressed; break;
        default: break;
    }
    if (!edge) {
        // Letters, digits, function keys... only matter while capturing a key
        // binding, where a held key is fine.
        return down;
    }
    if (!down) {
        *edge = false;
        if (held) { *held = 0.0; *last = 0.0; }
        return false;
    }

    if (edge == &inp.enterPressed || edge == &inp.escapePressed) {
        if (*edge) return false;   // still the same physical press
        *edge = true;
        return true;
    }

    // Navigation keys: fire once on press, then repeat while held (like text
    // cursors), so holding UP/DOWN scrolls through long lists. LEFT/RIGHT
    // adjust a value, where a repeat would race through all options, so they
    // only fire on the initial press.
    const double now = glfwGetTime();
    if (!*edge) {
        *edge = true;
        *held = 0.0;
        *last = now;
        return true;
    }
    if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) {
        *last = now;
        return false;
    }
    const double step = std::min(InputState::kMaxHeldStep, std::max(0.0, now - *last));
    *held += step;
    *last = now;
    if (*held >= InputState::kRepeatDelay) {
        *held = InputState::kRepeatDelay - InputState::kRepeatRate;
        return true;
    }
    return false;
}

void Game::resetMenuKeyEdges(GLFWwindow* window) {
    inp.resetNavKeys();
    // Keys still held stay marked as pressed, so they are not re-triggered on
    // release; keys released in the meantime are cleared by menuKeyPressed().
    if (glfwGetKey(window, GLFW_KEY_ENTER) != GLFW_PRESS &&
        glfwGetKey(window, GLFW_KEY_KP_ENTER) != GLFW_PRESS) {
        inp.enterPressed = false;
    }
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) != GLFW_PRESS) {
        inp.escapePressed = false;
    }
}

// ---------------------------------------------------------------------------
// Developer hook (see game.hpp): drives the menus without GLFW keyboard input
// ---------------------------------------------------------------------------

bool Game::debugMenuKey(const std::string& key) {
    if (gameState != MENU && gameState != SETTINGS &&
        gameState != MAP_SELECTION && gameState != SERVER_CONNECT) {
        return false;
    }
    menu::MenuView& view = activeMenuView();

    // "capture:<code>" finishes a key-binding capture with that key code.
    const bool capturing = key.rfind("capture:", 0) == 0;
    const int captureCode = capturing ? std::atoi(key.c_str() + 8) : -1;

    // Which keys this synthetic press reports. Like a real key edge, each one
    // reports down only once (menu code may ask several times per frame).
    std::set<int> pressedKeys;
    if (capturing) {
        pressedKeys.insert(captureCode);
    } else if (key == "up")          { pressedKeys.insert(GLFW_KEY_UP); }
    else if (key == "down")          { pressedKeys.insert(GLFW_KEY_DOWN); }
    else if (key == "left")          { pressedKeys.insert(GLFW_KEY_LEFT); }
    else if (key == "right")         { pressedKeys.insert(GLFW_KEY_RIGHT); }
    else if (key == "enter")         { pressedKeys.insert(GLFW_KEY_ENTER); }
    else if (key == "space")         { pressedKeys.insert(GLFW_KEY_SPACE); }
    else if (key == "escape")        { pressedKeys.insert(GLFW_KEY_ESCAPE); }

    menu::MenuInput input;
    std::set<int> consumed;
    input.pressed = [pressedKeys, consumed](int k) mutable {
        if (pressedKeys.count(k) == 0 || consumed.count(k)) return false;
        consumed.insert(k);
        return true;
    };
    input.held = [](int) { return false; };
    input.isModifier = [](int) { return false; };

    menu::MouseState mouse;   // no mouse in the hook
    mouse.inside = false;
    mouse.clicked = false;
    return menu::processInput(view, window, mouse, input);
}

bool Game::debugMenuSelect(const std::string& label) {
    menu::Page* page = activeMenuView().stack.current();
    if (!page) return false;
    for (size_t i = 0; i < page->rows.size(); ++i) {
        if (page->rows[i].label == label && page->rows[i].selectable()) {
            activeMenuView().stack.setSelection(static_cast<int>(i));
            return true;
        }
    }
    return false;
}

void Game::debugPumpFrame() {
    // The real per-frame network pump and gameplay step (input + physics +
    // sending), exactly like the game loop's frame, without drawing. A match
    // test never means to be in the menu (ESC would open it).
    if (gameState == MENU) gameState = PLAYING;
    update(1.0f / 60.0f);
}

void Game::debugRunFrames(int frames) {
    for (int i = 0; i < frames; ++i) {
        gameState = PLAYING;
        update(1.0f / 60.0f);
        render();
    }
}

// ── Spawn / movement tests ───────────────────────────────────────────────────
// These call the same code the player does (installMap -> resetGame ->
// updatePlayerPhysics); only the input source is scripted.

bool Game::debugStartMapForTest(const std::string& mapFile) {
    currentGameMode = DEATHMATCH;
    std::unique_ptr<Map> map = std::make_unique<Map>("assets/maps/" + mapFile);
    installMap(std::move(map));      // handles bomb sites + GPU state
    menuJustEntered = true;
    gameInProgress_ = true;
    gameState = PLAYING;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    return currentMap != nullptr;
}

void Game::debugStepForTest(int frames, float dt) {
    // No keys and no mouse are pressed while the test runs.
    for (int i = 0; i < frames; ++i) {
        update(dt);
    }
}

void Game::debugStepWithInputForTest(int frames, float dt, float forward, float strafe) {
    // Same convention as processGameInput: horizontal velocity is set from
    // the keys each frame, then the real physics (and everything else) runs.
    for (int i = 0; i < frames; ++i) {
        // Like a player in a match: never leave the menu state behind.
        if (gameState != PLAYING) gameState = PLAYING;
        const float yaw = glm::radians(camera.Yaw);
        const glm::vec2 fwd(std::cos(yaw), std::sin(yaw));        // same as the keys
        const glm::vec2 side(std::cos(yaw + glm::radians(90.0f)), std::sin(yaw + glm::radians(90.0f)));
        glm::vec2 move = fwd * forward + side * strafe;
        if (glm::length(move) > 1e-4f) {
            move = glm::normalize(move) * 5.0f;   // moveSpeed from processGameInput
            playerVelocity.x = move.x;
            playerVelocity.z = move.y;
        } else {
            playerVelocity.x = 0.0f;
            playerVelocity.z = 0.0f;
        }
        update(dt);   // updateNetwork, physics, gameplay, network send
    }
}

void Game::debugExplodeForTest(const glm::vec3& at) {
    createSimpleExplosion(at);
}

bool Game::debugInstallMapForTest(std::unique_ptr<Map> map) {
    // Same path as a loaded map: reset the match on the new geometry and give
    // the renderer the world batch.
    if (!map) return false;
    installMap(std::move(map));
    if (currentMap) {
        renderer.buildWorld(currentMap);
    }
    return currentMap != nullptr;
}

bool Game::debugKeyDownForTest(int key) const {
    if (!debugKeysHeldForTest.empty()) {
        for (int held : debugKeysHeldForTest) if (held == key) return true;
        return false;
    }
    return window && glfwGetKey(window, key) == GLFW_PRESS;
}

void Game::debugStepWithKeysForTest(float dt, const std::vector<int>& keys) {
    // Same call as the game loop makes; the key reports in debugKeyDownForTest
    // stand in for the keyboard.
    debugKeysHeldForTest = keys;
    processInput(dt);
    update(dt);
    debugKeysHeldForTest.clear();
}

void Game::debugPlayerState(glm::vec3& feet, glm::vec3& velocity, bool& onGround) const {
    feet = playerPos;
    velocity = playerVelocity;
    // Standing still, vertical velocity 0 and a surface right under the feet.
    onGround = std::abs(playerVelocity.y) < 0.5f && playerPos.y > -20.0f;
}

bool Game::debugPlayerStuck() const {
    // The very condition the automatic unstuck uses.
    return bodyBlockedAt(playerPos);
}

void Game::debugShootAndStepForTest() {
    // Fire whatever weapon is held, then let the mixer and the projectile
    // simulation run for a few frames (audio smoke test).
    shoot();
    for (int i = 0; i < 20; ++i) update(1.0f / 60.0f);
}

int Game::debugPlayingSoundCount() const { return audio.getPlayingSourceCount(); }

void Game::debugShowPage(menu::Page page) {
    openMenuView(settingsMenu_, std::move(page));
    gameState = SETTINGS;
    menuJustEntered = false;
}

menu::Page* Game::debugMenuPage() {
    return activeMenuView().stack.current();
}

// Console dump of the active page, for scripts and bug reports.
void Game::debugMenuDump() const {
    const menu::Page* page = activeMenuViewConst().stack.current();
    if (!page) { std::cout << "(no menu)" << std::endl; return; }
    std::cout << "=== " << page->title << " (depth " << activeMenuViewConst().stack.depth()
              << ") ===" << std::endl;
    const int selected = activeMenuViewConst().stack.selection();
    for (size_t i = 0; i < page->rows.size(); ++i) {
        const menu::Row& row = page->rows[i];
        if (row.kind == menu::RowKind::Separator) {
            std::cout << "-- " << row.label << " --" << std::endl;
            continue;
        }
        std::cout << (static_cast<int>(i) == selected ? "> " : "  ") << row.label;
        if (!row.value.empty()) std::cout << "  [" << row.value << "]";
        if (!row.enabled) std::cout << "  (disabled)";
        std::cout << std::endl;
    }
}
// DEBUG-TEMP
#include <cstdio>

// ---------------------------------------------------------------------------
// Developer hooks (see game.hpp): scripted UI checks without keyboard input
// ---------------------------------------------------------------------------

void Game::debugOpenMenu(const std::string& name) {
    if (name == "main") {
        openMainMenu();
        gameState = MENU;
    } else if (name == "settings") {
        openSettings();
        gameState = SETTINGS;
    } else if (name == "maps") {
        openMapSelection();
        gameState = MAP_SELECTION;
    } else if (name == "server") {
        openServerConnect();
        gameState = SERVER_CONNECT;
    }
    menuJustEntered = false;
}

