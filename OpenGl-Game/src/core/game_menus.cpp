#include "game.hpp"
#include "../ui/ui_helpers.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

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
    if (menuSelection < 0) {
        menuSelection = 0;
    }

    auto _b = UIHelpers::getBounds(window);
    float left = _b.left, right = _b.right, top = _b.top, bottom = _b.bottom;
    float uiWidth  = _b.width();
    float uiHeight = _b.height();
    float uiScale = std::max(0.8f, std::min(uiWidth / 800.0f, uiHeight / 600.0f));
    float menuTextScale = std::max(0.7f, std::min(1.05f, 0.9f * uiScale));

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

        // Draw text label — use drawTextFit so labels never overflow the button box
        float textX = menuX + 30.0f; // Left padding after indicator
        float textY = y + menuHeight/2.0f - 6.0f;
        renderer.drawTextFit(menuItems[i], textX, textY, menuWidth - 40.0f, menuTextScale);
    }

    // Show multiplayer status if connected
    if (isConnectedToServer()) {
        glm::mat4 statusBg = glm::mat4(1.0f);
        statusBg = glm::translate(statusBg, glm::vec3(400.0f, 520.0f, 0.0f));
        statusBg = glm::scale(statusBg, glm::vec3(300.0f, 30.0f, 1.0f));
        renderer.drawColoredQuad(statusBg, glm::vec3(0.0f, 0.8f, 0.0f)); // Green background

        std::string statusText = "CONNECTED - " + std::to_string(playerCount) + " PLAYERS";
        float textWidth = statusText.length() * 8.0f;
        renderer.drawTextFit(statusText.c_str(), 400.0f - textWidth/2.0f, 515.0f, 300.0f - 40.0f, 1.0f);
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

    // suppress unused-variable warning for editorIndex
    (void)editorIndex;
    (void)left; (void)right; (void)top; (void)bottom;
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
    keyNames[0] = glfwGetKeyName(keyBindings.forward,  0) ? glfwGetKeyName(keyBindings.forward,  0) : "Unknown";
    keyNames[1] = glfwGetKeyName(keyBindings.backward, 0) ? glfwGetKeyName(keyBindings.backward, 0) : "Unknown";
    keyNames[2] = glfwGetKeyName(keyBindings.left,     0) ? glfwGetKeyName(keyBindings.left,     0) : "Unknown";
    keyNames[3] = glfwGetKeyName(keyBindings.right,    0) ? glfwGetKeyName(keyBindings.right,    0) : "Unknown";
    keyNames[4] = glfwGetKeyName(keyBindings.jump,     0) ? glfwGetKeyName(keyBindings.jump,     0) : "Unknown";
    keyNames[5] = glfwGetKeyName(keyBindings.menu,     0) ? glfwGetKeyName(keyBindings.menu,     0) : "Unknown";
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

        // Calculate text layout similar to map selection menu
        float pad = 6.0f;
        float boxL = menuX + pad;
        float boxR = menuX + menuWidth - pad;
        float rowTextScale = std::max(0.65f, std::min(0.82f, 1.0f));
        float textH = 5.0f * 8.0f * rowTextScale;
        float textY = y + (menuHeight - textH) * 0.5f;

        // Draw key assignment on the right (right-aligned)
        const char* keyText = keyNames[i];
        float keyScale = rowTextScale * 0.85f;
        float keyW = renderer.getTextWidth(keyText, keyScale);
        float keyX = boxR - keyW;
        renderer.drawText(keyText, keyX, textY, keyScale);

        // Draw setting name on the left, using remaining space
        float nameL = boxL + 12.0f; // Extra space for indicator
        float nameMaxW = keyX - nameL - 8.0f; // Gap before key text
        renderer.drawTextFit(settingItems[i], nameL, textY, nameMaxW, rowTextScale);

        // Draw selection indicator on the left edge
        if (i == settingsSelection) {
            glm::mat4 indicatorModel = glm::mat4(1.0f);
            indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 4.0f, y + menuHeight/2.0f, 0.0f));
            indicatorModel = glm::scale(indicatorModel, glm::vec3(6.0f, 6.0f, 1.0f));
            renderer.drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
        }
    }

    // Only print to console when selection actually changes (to reduce spam)
    static int lastSettingsSelection = -1;
    if (settingsSelection != lastSettingsSelection) {
        std::cout << "\n=== SETTINGS ===" << std::endl;
        std::cout << (settingsSelection == 0 ? "> " : "  ") << "Forward: "  << keyNames[0] << std::endl;
        std::cout << (settingsSelection == 1 ? "> " : "  ") << "Backward: " << keyNames[1] << std::endl;
        std::cout << (settingsSelection == 2 ? "> " : "  ") << "Left: "     << keyNames[2] << std::endl;
        std::cout << (settingsSelection == 3 ? "> " : "  ") << "Right: "    << keyNames[3] << std::endl;
        std::cout << (settingsSelection == 4 ? "> " : "  ") << "Jump: "     << keyNames[4] << std::endl;
        std::cout << (settingsSelection == 5 ? "> " : "  ") << "Menu: "     << keyNames[5] << std::endl;
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
    // Note: static bool key-state variables have been moved to inp (InputState member)

    // Handle UP key
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !inp.upPressed) {
        inp.upPressed = true;
        settingsSelection = (settingsSelection - 1 + 7) % 7;
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        inp.upPressed = false;
    }

    // Handle DOWN key
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !inp.downPressed) {
        inp.downPressed = true;
        settingsSelection = (settingsSelection + 1) % 7;
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        inp.downPressed = false;
    }

    // Handle ENTER key
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !inp.enterPressed) {
        inp.enterPressed = true;

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
                    case 0: keyBindings.forward  = newKey; break;
                    case 1: keyBindings.backward = newKey; break;
                    case 2: keyBindings.left     = newKey; break;
                    case 3: keyBindings.right    = newKey; break;
                    case 4: keyBindings.jump     = newKey; break;
                    case 5: keyBindings.menu     = newKey; break;
                }
                saveKeyBindings();
            }
        }
    } else if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
        inp.enterPressed = false;
    }

    // Handle ESC key
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !inp.escapePressed) {
        inp.escapePressed = true;
        gameState = MENU;
        menuSelection = 0;
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
        inp.escapePressed = false;
    }
}

void Game::renderMapSelection() {
    // Clear screen with dark background
    renderer.clear();

    UIHelpers::setProjection(renderer, window);

    // Title bar - orange like settings
    glm::mat4 titleBg = glm::mat4(1.0f);
    titleBg = glm::translate(titleBg, glm::vec3(400.0f, 80.0f, 0.0f));
    titleBg = glm::scale(titleBg, glm::vec3(600.0f, 60.0f, 1.0f));
    renderer.drawColoredQuad(titleBg, glm::vec3(0.8f, 0.4f, 0.1f));

    auto _b = UIHelpers::getBounds(window);
    float left = _b.left, right = _b.right, top = _b.top, bottom = _b.bottom;
    float uiWidth  = _b.width();
    float uiHeight = _b.height();
    float titleScale = std::max(1.0f, std::min(1.5f, std::min(uiWidth / 800.0f, uiHeight / 600.0f) * 1.2f));
    renderer.drawTextCentered("SELECT MAP", 400.0f, 75.0f, titleScale);

    // Map data - loaded dynamically from assets/maps folder
    const int numMaps = mapDisplayNames.size();

    // Ensure mapSelection is within bounds
    if (numMaps > 0 && mapSelection >= numMaps) {
        mapSelection = numMaps - 1;
    }

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

        // --- Text layout ---
        // Scale: 0.8 gives text height = 5*8*0.8 = 32 px, leaving 4px margin top+bottom in a 40px box.
        // Clamp slightly smaller on tiny viewports.
        float rowTextScale = std::max(0.65f, std::min(0.82f,
                                std::min(uiWidth / 800.0f, uiHeight / 600.0f) * 0.82f));

        // Vertical centre of text inside the box
        float textH = 5.0f * 8.0f * rowTextScale;          // actual rendered text height
        float textY = y + (menuHeight - textH) * 0.5f;     // top-of-text that centres it

        // Horizontal zones (leave 6 px padding each side)
        float pad   = 6.0f;
        float boxL  = menuX + pad;
        float boxR  = menuX + menuWidth - pad;

        // Optional player-count badge on the far left (only when online with players)
        float nameL = boxL;                                 // map name starts here
        if (isConnectedToServer() && i < (int)mapPlayerCounts.size() && mapPlayerCounts[i] > 0) {
            char countStr[8];
            snprintf(countStr, sizeof(countStr), "[%d]", mapPlayerCounts[i]);
            float cw = renderer.getTextWidth(countStr, rowTextScale * 0.85f);
            renderer.drawText(countStr, boxL, textY, rowTextScale * 0.85f);
            nameL = boxL + cw + 6.0f;
        }

        // Map name fills the remaining space, shrunk to fit if needed
        float nameMaxW  = boxR - nameL;                     // use full remaining width
        renderer.drawTextFit(mapDisplayNames[i].c_str(), nameL, textY, nameMaxW, rowTextScale);

        // Selection indicator square on the left edge
        if (isSelected) {
            glm::mat4 indicatorModel = glm::mat4(1.0f);
            indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 4.0f, y + menuHeight/2.0f, 0.0f));
            indicatorModel = glm::scale(indicatorModel, glm::vec3(6.0f, 6.0f, 1.0f));
            renderer.drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
        }
    }

    // Game Mode selection row
    float gameModeY = startY + numMaps * spacing + 20.0f;
    glm::mat4 gmBg = glm::mat4(1.0f);
    gmBg = glm::translate(gmBg, glm::vec3(menuX + menuWidth/2.0f, gameModeY + menuHeight/2.0f, 0.0f));
    gmBg = glm::scale(gmBg, glm::vec3(menuWidth, menuHeight, 1.0f));
    renderer.drawColoredQuad(gmBg, glm::vec3(0.7f, 0.3f, 0.3f));

    const char* gameModeStr = (currentGameMode == DEATHMATCH) ? "Deathmatch" : 
                            (currentGameMode == TEAM_VS_TEAM) ? "Team vs Team" : "Bomb Defusal";
    char gmText[64];
    snprintf(gmText, sizeof(gmText), "Game Mode: %s", gameModeStr);
    {
        float gmScale = std::max(0.65f, std::min(0.85f, std::min(uiWidth / 800.0f, uiHeight / 600.0f) * 0.85f));
        float gmTextH = 5.0f * 8.0f * gmScale;
        float gmTextY = gameModeY + (menuHeight - gmTextH) * 0.5f;
        renderer.drawTextFit(gmText, menuX + 10.0f, gmTextY, menuWidth - 20.0f, gmScale);
    }

    // Instructions at bottom — use drawTextFit so it never overflows on narrow screens
    renderer.drawTextFit("UP/DOWN=map LEFT/RIGHT=mode ENTER=load ESC=back",
                         left + 20.0f, bottom - 18.0f, _b.width() - 40.0f, 0.9f);

    // Only print to console when selection actually changes (to reduce spam)
    static int lastMapSelection = -1;
    if (mapSelection != lastMapSelection) {
        std::cout << "\n=== MAP SELECTION ===" << std::endl;
        for (int i = 0; i < numMaps; i++) {
            std::cout << (i == mapSelection ? "> " : "  ") << mapDisplayNames[i] << std::endl;
        }
        std::cout << "Game Mode: " << gameModeStr << std::endl;
        std::cout << "Use UP/DOWN to select, LEFT/RIGHT for game mode, ENTER to load, ESC to go back" << std::endl;
        lastMapSelection = mapSelection;
    }

    // Suppress unused-variable warnings for bounds variables not referenced above
    (void)right; (void)top;
}

void Game::processMapSelectionInput() {
    // Note: static bool key-state variables have been moved to inp (InputState member)

    // Reset key states when entering menu to prevent stuck keys.
    // Return early so a still-held ENTER from the main menu doesn't
    // instantly load the currently highlighted map on the first frame.
    if (menuJustEntered) {
        inp.resetNavKeys();
        menuJustEntered = false;
        return;
    }

    const int numMaps = mapFiles.size();

    // Ensure mapSelection is within bounds
    if (numMaps == 0) {
        mapSelection = 0;
    } else if (mapSelection >= numMaps) {
        mapSelection = numMaps - 1;
    }

    // Handle UP key
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !inp.upPressed) {
        inp.upPressed = true;
        if (numMaps > 0) {
            mapSelection = (mapSelection - 1 + numMaps) % numMaps;
        }
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        inp.upPressed = false;
    }

    // Handle DOWN key
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !inp.downPressed) {
        inp.downPressed = true;
        if (numMaps > 0) {
            mapSelection = (mapSelection + 1) % numMaps;
        }
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        inp.downPressed = false;
    }

    // Handle LEFT/RIGHT for game mode selection
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS && !inp.leftPressed) {
        inp.leftPressed = true;
        int newMode = (static_cast<int>(currentGameMode) + 2) % 3; // Cycle backwards
        currentGameMode = static_cast<GameMode>(newMode);
        const char* modeName = (currentGameMode == DEATHMATCH) ? "Deathmatch" : 
                              (currentGameMode == TEAM_VS_TEAM) ? "Team vs Team" : "Bomb Defusal";
        std::cout << "Game Mode: " << modeName << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_RELEASE) {
        inp.leftPressed = false;
    }

    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS && !inp.rightPressed) {
        inp.rightPressed = true;
        int newMode = (static_cast<int>(currentGameMode) + 1) % 3; // Cycle forwards
        currentGameMode = static_cast<GameMode>(newMode);
        const char* modeName = (currentGameMode == DEATHMATCH) ? "Deathmatch" : 
                              (currentGameMode == TEAM_VS_TEAM) ? "Team vs Team" : "Bomb Defusal";
        std::cout << "Game Mode: " << modeName << std::endl;
    } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_RELEASE) {
        inp.rightPressed = false;
    }

    // Handle ENTER key
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !inp.enterPressed) {
        inp.enterPressed = true;
        if (mapSelection < (int)mapFiles.size()) {
            const char* modeName = (currentGameMode == DEATHMATCH) ? "Deathmatch" : 
                                  (currentGameMode == TEAM_VS_TEAM) ? "Team vs Team" : "Bomb Defusal";
            std::cout << "Loading " << mapFiles[mapSelection] << " with game mode " << modeName << "..." << std::endl;

            // Notify server of map change if connected
            if (isConnectedToServer() && networkClient) {
                networkClient->sendMapChange(mapFiles[mapSelection]);

            // Local cleanup: when switching maps online, clear remote players from previous map
            for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                remotePlayers[i].active   = false;
                remotePlayers[i].id       = 0;
                remotePlayers[i].position = glm::vec3(0.0f);
                remotePlayers[i].renderPosition = glm::vec3(0.0f);
                remotePlayers[i].velocity = glm::vec3(0.0f);
                remotePlayers[i].yaw      = 0.0f;
                remotePlayers[i].pitch    = 0.0f;
                remotePlayers[i].health   = 0.0f;
                remotePlayers[i].isDead   = true;
            }
            playerCount = 1; // self only until same-map players stream in
            }

            loadMap(mapFiles[mapSelection]);
            // In Team vs Team mode, assign player to team
            if (currentGameMode == TEAM_VS_TEAM || currentGameMode == BOMB_DEFUSAL) {
                playerTeam = 0; // Team A (Red) by default
            }
            resetGame(); // Spawn at appropriate spawn point
            
            // Start round if in bomb defusal mode
            if (bombDefusalMode) {
                startRound();
            }

            // Stabilize transition to PLAYING to avoid accidental second-trigger behavior.
            menuJustEntered = true;
            gameState = PLAYING;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    } else if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
        inp.enterPressed = false;
    }

    // Handle ESC key - go back to menu
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !inp.escapePressed) {
        inp.escapePressed = true;
        gameState = MENU;
        menuSelection = 1;
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
        inp.escapePressed = false;
    }
}