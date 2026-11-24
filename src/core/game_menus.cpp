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
 *   openSettings()    – settings overview; one sub-page per group, controls
 *                       (rebinding) and frame generation (inside VIDEO)
 *   openMapSelection()– map list + game mode
 *   menu()            – draw the active menu
 *   processMenuInput() (game_input.cpp) – route input to the active menu
 *
 * The connect-to-server screen lives in game_server_menu.cpp, the settings
 * file and value model in core/settings.cpp.
 */

#define GLFW_INCLUDE_NONE
#include "game.hpp"
#include "brand.hpp"
#include "discord_presence.hpp"
#include "engine/graphics_capabilities.hpp"
#include "../ui/ui_helpers.hpp"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>

namespace {
/// "0", "1", ... "max" for bot-count cycle rows.
std::vector<std::string> buildCountOptions(int max) {
    if (max < 0) max = 0;
    std::vector<std::string> options;
    options.reserve(static_cast<size_t>(max + 1));
    for (int i = 0; i <= max; ++i) options.push_back(std::to_string(i));
    return options;
}

/// "10%" ... "max%" in 10% steps (used by the bot-strength rows).
std::vector<std::string> buildAccuracyOptions(int max) {
    if (max < 10) max = 10;
    std::vector<std::string> options;
    for (int value = 10; value <= max; value += 10) {
        options.push_back(std::to_string(value) + "%");
    }
    if (options.empty()) options.push_back("10%");
    return options;
}

std::string lobbyPlayerLabel(const LobbyPlayerView& player) {
    return player.name + (player.ready ? "  READY" : "  WAITING");
}

/// Append the two-column team roster (Team A on the left, Team B on the
/// right). Human players come from the server lobby so every client sees the
/// same names and ready flags; bots fill the remaining slots of each side.
void appendTeamRoster(menu::Page& page, const LobbyView* lobby,
                      const std::string& localName, uint32_t selfId,
                      bool localReady, bool showReady, int playerTeam,
                      int botsTeamA, int botsTeamB) {
    std::vector<std::string> teamA;
    std::vector<std::string> teamB;

    bool localListed = false;
    if (lobby) {
        for (const LobbyPlayerView& player : lobby->players) {
            if (player.id == selfId) localListed = true;
            (player.team == 0 ? teamA : teamB).push_back(lobbyPlayerLabel(player));
        }
    }
    if (!localListed) {
        const std::string label = localName +
            (showReady ? (localReady ? "  READY" : "  WAITING") : std::string());
        if (playerTeam == 0) teamA.insert(teamA.begin(), label);
        else                 teamB.insert(teamB.begin(), label);
    }

    for (int i = 0; i < botsTeamA; ++i) {
        teamA.push_back("BOT " + std::to_string(i + 1));
    }
    for (int i = 0; i < botsTeamB; ++i) {
        teamB.push_back("BOT " + std::to_string(i + 1));
    }

    page.add(menu::separator("TEAMS"));
    page.add(menu::info("TEAM A", "TEAM B"));
    const size_t rows = std::max(teamA.size(), teamB.size());
    for (size_t slot = 0; slot < rows; ++slot) {
        page.add(menu::info(slot < teamA.size() ? teamA[slot] : std::string(),
                            slot < teamB.size() ? teamB[slot] : std::string()));
    }
}

/// Deathmatch has no sides: show every human in the setup in one list.
void appendLobbyPlayers(menu::Page& page, const LobbyView* lobby,
                        const std::string& localName, uint32_t selfId,
                        bool localReady, bool showReady) {
    page.add(menu::separator("PLAYERS"));
    bool localListed = false;
    if (lobby) {
        for (const LobbyPlayerView& player : lobby->players) {
            if (player.id == selfId) localListed = true;
            page.add(menu::info(player.name, player.ready ? "READY" : "WAITING"));
        }
    }
    if (!localListed) {
        page.add(menu::info(localName,
                            showReady ? (localReady ? "READY" : "WAITING")
                                      : std::string()));
    }
}
} // namespace

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
        case TEAM_SETUP:      return teamMenu_;
        case MENU:
        default:              return mainMenu_;
    }
}

const menu::MenuView& Game::activeMenuViewConst() const {
    switch (gameState) {
        case SETTINGS:        return settingsMenu_;
        case MAP_SELECTION:   return mapMenu_;
        case SERVER_CONNECT:  return serverMenu_;
        case TEAM_SETUP:      return teamMenu_;
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

    menu::Page page(brand::kShortName, brand::kTagline);
    if (isConnectedToServer()) {
        page.add(menu::info("CONNECTION", std::to_string(playerCount) + " PLAYERS ONLINE",
                            "Connected to a LAN server"));
    }

    // The menu can be opened before the first game starts (nothing to
    // resume) and from the game itself (the first row resumes it).
    const bool resumable = gameInProgress_;
    page.add(menu::action(resumable ? "RESUME GAME" : "PLAY GAME", [this]() {
        resumeGameFromMenu();
    }, resumable ? "Back to the match" : "Start a match on the selected map"));

    page.add(menu::action("MAP SELECTION", [this]() {
        openMapSelection();
        gameState = MAP_SELECTION;
    }, "Choose the map and game mode"));

    if (!isConnectedToServer()) {
        page.add(menu::action("MAP EDITOR", [this]() {
            enterEditor();
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
// Buy menu
// ---------------------------------------------------------------------------

bool Game::canOpenBuyMenu() const {
    if (!window || !gameInProgress_ || isDead || spectatorMode || matchOver_) return false;
    if (isPlanting || isDefusing) return false;   // finish the objective first
    if (!isEconomyMode()) return true;   // free loadout menu in every other mode
    // Economic mode: only while the round is live and the freeze + grace
    // window has not run out. Moving after the freeze closes it early.
    return roundActive && buyTimeLeft_ > 0.0f && !buyWindowClosed_;
}

void Game::openBuyMenu() {
    inp.resetNavKeys();
    inp.gameplayBuyPressed = bindingDown(keyBindings.buy);
    menuJustEntered = true;
    gameState = BUY_MENU;

    buildBuyMenuEntries();
    // Start on the first thing that can actually be bought.
    buySelection_ = 0;
    for (size_t i = 0; i < buyEntries_.size(); ++i) {
        if (buyEntries_[i].available) { buySelection_ = static_cast<int>(i); break; }
    }
    playUiSound("ui_select");
}

void Game::closeBuyMenu() {
    if (gameState != BUY_MENU) return;
    gameState = PLAYING;
    inp.resetNavKeys();
    // The B press that closed the menu is still physically held; do not let
    // the gameplay input see it as a fresh edge and reopen the menu.
    inp.gameplayBuyPressed = bindingDown(keyBindings.buy);
    menuJustEntered = true;
    capturePlayCursor(true);
}

void Game::buildBuyMenuEntries() {
    if (!window) return;
    const bool economic = isEconomyMode();
    const bool buyOpen = canOpenBuyMenu();
    buyEntries_.clear();

    auto add = [&](economy::Item item, const char* label, int column, int row,
                   bool owned) {
        BuyEntry entry;
        entry.item = item;
        entry.label = label;
        entry.owned = owned;
        entry.column = column;
        entry.row = row;
        const int cost = economy::price(item);
        if (!economic) {
            entry.value = owned ? "EQUIPPED" : (cost > 0 ? "FREE" : "ALWAYS");
            entry.available = true;
        } else if (owned) {
            entry.value = "OWNED";
            entry.available = false;
        } else {
            entry.value = "$" + std::to_string(cost);
            entry.available = buyOpen && money_ >= cost;
        }
        buyEntries_.push_back(std::move(entry));
    };

    // Column 0: gear. Column 1: primary. Column 2: sidearm. Column 3: melee
    // (one at a time). Column 4: grenades.
    add(economy::ITEM_KEVLAR, "KEVLAR VEST", 0, 0, armor_ >= 100);
    add(economy::ITEM_DEFUSE_KIT, "DEFUSE KIT", 0, 1, defuseKit_);

    add(economy::ITEM_PRIMARY_SHOTGUN, "PUMP SHOTGUN", 1, 0, hasPrimaryShotgun_);
    add(economy::ITEM_PRIMARY_SNIPER, "SNIPER RIFLE", 1, 1, hasPrimarySniper_);

    add(economy::ITEM_SIDEARM_PISTOL, "PISTOL", 2, 0, lastSidearm_ == PISTOL);
    add(economy::ITEM_SIDEARM_PRO_PISTOL, "PRO PISTOL", 2, 1, hasProPistol_);

    add(economy::ITEM_MELEE_KNIFE, "KNIFE", 3, 0, meleeWeapon_ == KNIFE);
    add(economy::ITEM_MELEE_AXE, "BATTLE AXE", 3, 1, meleeWeapon_ == BATTLE_AXE);
    add(economy::ITEM_MELEE_MACE, "MACE", 3, 2, meleeWeapon_ == MACE);
    add(economy::ITEM_MELEE_LUNGE, "LUNGE MINE", 3, 3, meleeWeapon_ == LUNGE_MINE);

    add(economy::ITEM_HE_GRENADE, "HE GRENADE", 4, 0, hasHEGrenade_);
    add(economy::ITEM_FLASHBANG, "FLASHBANG", 4, 1, hasFlashbang_);

    // Layout on the virtual canvas: five category columns, all on screen.
    const UIHelpers::UIBounds b = UIHelpers::getBounds(window);
    const float margin = 26.0f;
    const int columns = 5;
    const float gapX = 12.0f;
    const float colW = (b.width() - 2.0f * margin - (columns - 1) * gapX) / columns;
    const float gridTop = b.top + 92.0f;
    const float gridBottom = b.bottom - 44.0f;
    const float headerH = 22.0f;
    const float gapY = 8.0f;
    const int maxRows = 4;
    const float tileH = std::min(96.0f,
        ((gridBottom - gridTop) - headerH - (maxRows - 1) * gapY) / maxRows);
    for (BuyEntry& entry : buyEntries_) {
        entry.x = b.left + margin + static_cast<float>(entry.column) * (colW + gapX);
        entry.y = gridTop + headerH + gapY +
                  static_cast<float>(entry.row) * (tileH + gapY);
        entry.w = colW;
        entry.h = tileH;
    }
}

void Game::moveBuySelection(int deltaColumn, int deltaRow) {
    if (buyEntries_.empty()) return;
    if (buySelection_ < 0 || buySelection_ >= static_cast<int>(buyEntries_.size())) {
        buySelection_ = 0;
        return;
    }
    const BuyEntry current = buyEntries_[static_cast<size_t>(buySelection_)];

    if (deltaRow != 0) {
        const int targetRow = current.row + deltaRow;
        int best = -1;
        int bestRow = -1;
        for (size_t i = 0; i < buyEntries_.size(); ++i) {
            const BuyEntry& entry = buyEntries_[i];
            if (entry.column != current.column) continue;
            const bool forward = deltaRow > 0;
            const bool inRange = forward ? entry.row >= targetRow : entry.row <= targetRow;
            if (!inRange) continue;
            const bool better = best < 0 || (forward ? entry.row < bestRow
                                                     : entry.row > bestRow);
            if (better) { best = static_cast<int>(i); bestRow = entry.row; }
        }
        if (best >= 0) buySelection_ = best;
        return;
    }

    if (deltaColumn != 0) {
        int searchColumn = current.column + deltaColumn;
        while (searchColumn >= 0 && searchColumn <= 4) {
            int best = -1;
            int bestDistance = 1 << 30;
            for (size_t i = 0; i < buyEntries_.size(); ++i) {
                const BuyEntry& entry = buyEntries_[i];
                if (entry.column != searchColumn) continue;
                const int distance = std::abs(entry.row - current.row);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = static_cast<int>(i);
                }
            }
            if (best >= 0) { buySelection_ = best; return; }
            searchColumn += deltaColumn > 0 ? 1 : -1;
        }
    }
}

void Game::renderBuyMenu() {
    if (!window) return;
    buildBuyMenuEntries();
    const UIHelpers::UIBounds b = UIHelpers::getBounds(window);

    auto quad = [&](float cx, float cy, float w, float h, const glm::vec3& color) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(cx, cy, 0.0f));
        m = glm::scale(m, glm::vec3(w, h, 1.0f));
        renderer.drawColoredQuad(m, color);
    };
    auto textRight = [&](const std::string& text, float rightX, float y,
                         float scale, const glm::vec3& color) {
        renderer.drawTextColored(text, rightX - renderer.getTextWidth(text, scale),
                                 y, scale, color);
    };

    const glm::vec3 kBg(0.045f, 0.055f, 0.070f);
    const glm::vec3 kPanel(0.070f, 0.085f, 0.110f);
    const glm::vec3 kHeader(0.11f, 0.13f, 0.17f);
    const glm::vec3 kTile(0.13f, 0.16f, 0.21f);
    const glm::vec3 kTileSel(0.30f, 0.24f, 0.07f);
    const glm::vec3 kTileOwned(0.10f, 0.20f, 0.13f);
    const glm::vec3 kTileLocked(0.085f, 0.095f, 0.115f);
    const glm::vec3 kGold(1.0f, 0.82f, 0.30f);
    const glm::vec3 kGreen(0.35f, 0.90f, 0.42f);
    const glm::vec3 kMuted(0.58f, 0.63f, 0.70f);

    // Full-screen opaque backdrop (the menu is its own screen, not a pause
    // overlay over the 3D scene).
    quad(b.centerX(), b.centerY(), b.width(), b.height(), kBg);

    const bool economic = isEconomyMode();
    renderer.drawTextColored("BUY MENU", b.left + 26.0f, b.top + 12.0f, 0.80f, kGold);
    textRight(economic ? ("$" + std::to_string(money_)) : "FREE LOADOUT",
              b.right - 26.0f, b.top + 16.0f, 0.62f, kGreen);

    std::string info;
    if (economic) {
        const bool buyOpen = canOpenBuyMenu();
        const float shownLeft = isRoundFrozen() ? (freezeTime_ - roundAge_)
                                                : buyTimeLeft_;
        const int seconds = static_cast<int>(std::max(0.0f, shownLeft) + 0.999f);
        info = isRoundFrozen() ? "FREEZE " : "BUY WINDOW ";
        info += buyOpen ? (std::to_string(seconds) + "S") : "CLOSED";
    } else {
        info = "EVERYTHING IS FREE";
    }
    renderer.drawTextColored(info, b.left + 26.0f, b.top + 50.0f, 0.36f, kMuted);

    const float margin = 26.0f;
    const int columns = 5;
    const float gapX = 12.0f;
    const float colW = (b.width() - 2.0f * margin - (columns - 1) * gapX) / columns;
    const float gridTop = b.top + 92.0f;
    const float gridBottom = b.bottom - 44.0f;

    static const char* const kHeaders[5] = {"GEAR", "PRIMARY", "SIDEARM",
                                            "MELEE", "GRENADES"};
    for (int c = 0; c < columns; ++c) {
        const float x = b.left + margin + static_cast<float>(c) * (colW + gapX);
        quad(x + colW * 0.5f, (gridTop + gridBottom) * 0.5f,
             colW, gridBottom - gridTop, kPanel);
        quad(x + colW * 0.5f, gridTop + 11.0f, colW, 22.0f, kHeader);
        renderer.drawTextCentered(kHeaders[c], x + colW * 0.5f, gridTop + 2.0f, 0.40f);
    }

    for (size_t i = 0; i < buyEntries_.size(); ++i) {
        const BuyEntry& entry = buyEntries_[i];
        const bool selected = static_cast<int>(i) == buySelection_;
        glm::vec3 fill = entry.owned ? kTileOwned : (entry.available ? kTile : kTileLocked);
        if (selected) fill = kTileSel;

        quad(entry.x + entry.w * 0.5f, entry.y + entry.h * 0.5f,
             entry.w - 2.0f, entry.h - 2.0f, fill);
        if (selected) {
            quad(entry.x + 2.0f, entry.y + entry.h * 0.5f, 3.0f, entry.h - 6.0f, kGold);
        }

        renderer.drawTextFit(entry.label, entry.x + 9.0f, entry.y + 8.0f,
                             entry.w - 18.0f, selected ? 0.46f : 0.42f);
        const glm::vec3 valueColor = entry.owned ? kGreen
                                                : (entry.available ? kGold : kMuted);
        textRight(entry.value, entry.x + entry.w - 9.0f, entry.y + entry.h - 20.0f,
                  0.38f, valueColor);
    }

    renderer.drawTextCentered("ARROWS / MOUSE  SELECT      ENTER  BUY      B / ESC  CLOSE",
                              b.centerX(), b.bottom - 28.0f, 0.36f);
}

void Game::processBuyMenuInput(float deltaTime) {
    (void)deltaTime;
    if (!window) return;
    if (menuJustEntered) {
        menuJustEntered = false;
        return;
    }

    buildBuyMenuEntries();
    if (buyEntries_.empty()) return;
    if (buySelection_ < 0 || buySelection_ >= static_cast<int>(buyEntries_.size())) {
        buySelection_ = 0;
    }

    // B / ESC close the screen.
    if (bindingDown(keyBindings.buy)) {
        if (!inp.gameplayBuyPressed) {
            inp.gameplayBuyPressed = true;
            closeBuyMenu();
            return;
        }
    } else {
        inp.gameplayBuyPressed = false;
    }
    if (menuKeyPressed(GLFW_KEY_ESCAPE, window)) {
        closeBuyMenu();
        return;
    }

    if (menuKeyPressed(GLFW_KEY_UP, window))    moveBuySelection(0, -1);
    if (menuKeyPressed(GLFW_KEY_DOWN, window))  moveBuySelection(0, 1);
    if (menuKeyPressed(GLFW_KEY_LEFT, window))  moveBuySelection(-1, 0);
    if (menuKeyPressed(GLFW_KEY_RIGHT, window)) moveBuySelection(1, 0);

    // Mouse hover selects; a click buys the hovered tile.
    int hovered = -1;
    for (size_t i = 0; i < buyEntries_.size(); ++i) {
        const BuyEntry& entry = buyEntries_[i];
        if (mouseGUIPosX >= entry.x && mouseGUIPosX <= entry.x + entry.w &&
            mouseGUIPosY >= entry.y && mouseGUIPosY <= entry.y + entry.h) {
            hovered = static_cast<int>(i);
            break;
        }
    }
    if (hovered >= 0) buySelection_ = hovered;

    const bool enter = menuKeyPressed(GLFW_KEY_ENTER, window) ||
                       menuKeyPressed(GLFW_KEY_KP_ENTER, window);
    const bool click = mouseClicked && hovered >= 0;
    if (enter || click) {
        const BuyEntry& entry = buyEntries_[static_cast<size_t>(buySelection_)];
        if (entry.available) {
            buyItem(entry.item);
            buildBuyMenuEntries();
        } else {
            playDryFire();
        }
    }
}

// ---------------------------------------------------------------------------
// Settings + controls
// ---------------------------------------------------------------------------

namespace {
/// The graphics API the game is actually running on, upper case for the menu
/// font ("VULKAN", "OPENGL"). It can differ from the RENDERER setting: that
/// applies after a restart, and Vulkan falls back to OpenGL without a driver.
std::string activeRenderApiName(const Renderer& renderer) {
    std::string api = "UNKNOWN";
    if (const IRenderBackend* backend = renderer.backend()) {
        api = backend->describe().api;
        for (char& c : api) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return api;
}
} // namespace

namespace {
/// Frame-generation technology AUTO would pick right now (OFF if none).
int effectiveFrameGenMode(const settings::GameSettings& s) {
    if (s.frameGen != settings::FRAMEGEN_AUTO) return s.frameGen;
    const GraphicsCapabilities& caps = graphicsCapabilities();
    if (caps.frameGenDlss) return settings::FRAMEGEN_DLSS;
    if (caps.frameGenFsr) return settings::FRAMEGEN_FSR;
    if (caps.frameGenXess) return settings::FRAMEGEN_XESS;
    return settings::FRAMEGEN_OFF;
}

bool frameGenModeSupported(int mode) {
    const GraphicsCapabilities& caps = graphicsCapabilities();
    switch (mode) {
        case settings::FRAMEGEN_OFF:  return true;
        case settings::FRAMEGEN_AUTO: return caps.frameGenAuto();
        case settings::FRAMEGEN_FSR:  return caps.frameGenFsr;
        case settings::FRAMEGEN_DLSS: return caps.frameGenDlss;
        case settings::FRAMEGEN_XESS: return caps.frameGenXess;
        default:                      return false;
    }
}

/// Generated frames per rendered frame this technology can reach (1 = 2X).
int maxGeneratedFrames(int mode) {
    const GraphicsCapabilities& caps = graphicsCapabilities();
    switch (mode) {
        case settings::FRAMEGEN_DLSS: return caps.frameGenDlss ? (caps.dlssMultiFrame ? 3 : 1) : 0;
        case settings::FRAMEGEN_FSR:  return caps.frameGenFsr ? 1 : 0;
        case settings::FRAMEGEN_XESS: return caps.frameGenXess ? 2 : 0;
        default:                      return 0;   // OFF / AUTO without a technology
    }
}

bool lowLatencySupported(int mode) {
    const GraphicsCapabilities& caps = graphicsCapabilities();
    switch (mode) {
        case settings::FRAMEGEN_DLSS: return caps.frameGenDlss;   // Reflex is part of DLSS-G
        case settings::FRAMEGEN_FSR:  return caps.frameGenFsr && caps.hasAmd;  // Anti-Lag 2
        default:                      return false;
    }
}

std::string frameGenModeNote(int mode) {
    const GraphicsCapabilities& caps = graphicsCapabilities();
    switch (mode) {
        case settings::FRAMEGEN_FSR:  return caps.fsrNote;
        case settings::FRAMEGEN_DLSS: return caps.dlssNote;
        case settings::FRAMEGEN_XESS: return caps.xessNote;
        case settings::FRAMEGEN_AUTO:
            return caps.frameGenAvailable() ? std::string() : "No frame generation technology is supported here";
        default: return std::string();
    }
}

bool settingRowEnabled(const settings::SettingDef& def, const settings::GameSettings& s) {
    const int tech = effectiveFrameGenMode(s);
    if (def.key == "frame_gen") {
        // With nothing to choose (OFF only), grey the row out. A saved mode
        // that this machine cannot run stays selectable so it can be turned off.
        return graphicsCapabilities().frameGenAvailable() ||
               !frameGenModeSupported(s.frameGen);
    }
    if (def.key == "frame_gen_output")
        return tech != settings::FRAMEGEN_OFF && frameGenModeSupported(tech);
    if (def.key == "frame_gen_low_latency")
        return tech != settings::FRAMEGEN_OFF && frameGenModeSupported(tech) && lowLatencySupported(tech);
    return true;
}

bool settingOptionEnabled(const settings::SettingDef& def, int index,
                          const settings::GameSettings& s) {
    if (def.key == "render_api") {
        if (index == settings::RENDER_API_VULKAN) return graphicsCapabilities().vulkanAvailable;
        return true;
    }
    if (def.key == "frame_gen") return frameGenModeSupported(index);
    if (def.key == "frame_gen_output") {
        return index >= 0 && index + 1 <= maxGeneratedFrames(effectiveFrameGenMode(s));
    }
    return true;
}

std::string settingRowHelp(const settings::SettingDef& def, const settings::GameSettings& s) {
    if (def.key == "render_api" && def.value->i == settings::RENDER_API_VULKAN &&
        !graphicsCapabilities().vulkanAvailable) {
        return "Vulkan is unavailable here: " + graphicsCapabilities().vulkanNote +
               ". The game runs on OPENGL.";
    }
    if (def.key == "frame_gen") {
        const std::string note = frameGenModeNote(def.value->i);
        if (!note.empty()) return note;
    }
    return std::string();
}

/// Filters that grey out options this OS / GPU / build cannot run.
menu::SettingRowsOptions settingRowsOptions(const settings::GameSettings& s) {
    menu::SettingRowsOptions options;
    options.rowEnabled = [&s](const settings::SettingDef& def) { return settingRowEnabled(def, s); };
    options.optionEnabled = [&s](const settings::SettingDef& def, int index) {
        return settingOptionEnabled(def, index, s);
    };
    options.rowHelp = [&s](const settings::SettingDef& def) { return settingRowHelp(def, s); };
    return options;
}

/// Re-evaluate the FRAME GENERATION rows after MODE changed (the page is kept
/// alive while the menu is open, so its enabled flags must follow the values).
void refreshFrameGenRows(menu::Page& page, const std::vector<settings::SettingDef>& defs,
                        const settings::GameSettings& s) {
    for (menu::Row& row : page.rows) {
        if (row.kind == menu::RowKind::Separator || row.kind == menu::RowKind::Info) continue;
        if (row.id < 0 || row.id >= static_cast<int>(defs.size())) continue;
        const settings::SettingDef& def = defs[static_cast<size_t>(row.id)];
        if (def.group != settings::kFrameGenGroup) continue;
        row.enabled = settingRowEnabled(def, s);
        if (row.kind == menu::RowKind::Cycle) {
            row.optionEnabled.assign(row.options.size(), true);
            for (size_t option = 0; option < row.options.size(); ++option) {
                row.optionEnabled[option] =
                    settingOptionEnabled(def, static_cast<int>(option), s);
            }
        }
        row.help = def.help;
        const std::string help = settingRowHelp(def, s);
        if (!help.empty()) row.help = help;
    }
}
} // namespace

namespace {
/// One line of help per settings group on the SETTINGS overview.
const char* groupHelp(const std::string& group) {
    if (group == "GAMEPLAY") return "Ammo, lying down and the FPS counter";
    if (group == "VIDEO") return "Frame rate, renderer and frame generation";
    if (group == "AUDIO") return "Master, music and effects volume";
    if (group == "INTERFACE") return "Discord status";
    return "";
}
} // namespace

void Game::onSettingRowChanged() {
    // Apply right away and play a feedback blip, so a new volume can be heard.
    applySettings();
    saveSettings();
    playUiSound("ui_select");
}

void Game::openSettings() {
    // SETTINGS is an overview: one entry per group, each a sub-screen pushed
    // on the same stack, so ESC returns here with that entry still selected.
    // The groups and their order come from the settings registry; key
    // bindings (CONTROLS) and FRAME GENERATION (inside VIDEO) have own pages.
    menu::Page page("SETTINGS", "CHANGES ARE SAVED AUTOMATICALLY");
    std::vector<std::string> groups;
    for (const settings::SettingDef& def : settingDefs_) {
        if (def.value->type == settings::ValueType::Key) continue;
        if (def.group == settings::kFrameGenGroup) continue;
        if (std::find(groups.begin(), groups.end(), def.group) == groups.end()) groups.push_back(def.group);
    }
    for (const std::string& group : groups) {
        page.add(menu::action(group, [this, group]() { openSettingsGroup(group); }, groupHelp(group)));
    }
    page.add(menu::action("CONTROLS", [this]() { openControlsSettings(); },
                          "Change the keyboard and mouse bindings"));

    openMenuView(settingsMenu_, std::move(page));
    settingsMenu_.legend = {{"UP/DOWN", "SELECT"}, {"ENTER", "OPEN"}, {"ESC", "LEAVE"}};
    settingsMenu_.onClose = [this]() { closeMenuToGame(); };
}

void Game::openSettingsGroup(const std::string& group) {
    menu::SettingRowsOptions options = settingRowsOptions(gameSettings_);
    options.onlyGroup = group;
    options.afterGroup = [this](menu::Page& page, const std::string& done) {
        if (done != "VIDEO") return;
        const GraphicsCapabilities& caps = graphicsCapabilities();
        page.add(menu::info("ACTIVE RENDERER", activeRenderApiName(renderer),
                            "Running now; a RENDERER change applies after a restart"));
        const std::string activeApi = activeRenderApiName(renderer);
        if (!caps.vulkanAvailable) {
            page.add(menu::info("VULKAN", "UNAVAILABLE", caps.vulkanNote));
        } else if (vulkanFallback_) {
            page.add(menu::info("VULKAN", "FAILED",
                                "Vulkan is available but could not start; the game is running on OPENGL"));
        } else if (!renderApiForcedByEnv_) {
            const bool selectedVulkan = gameSettings_.renderApi == settings::RENDER_API_VULKAN;
            if (selectedVulkan != (activeApi == "VULKAN")) {
                page.add(menu::info("VULKAN", "RESTART",
                                    "The RENDERER change applies after restarting the game"));
            }
        }
        page.add(menu::action(settings::kFrameGenGroup, [this]() { openFrameGenSettings(); },
                              "DLSS, FSR and XESS frame generation options"));
        if (!caps.frameGenAvailable()) {
            page.rows.back().value = "NOT SUPPORTED";
            page.rows.back().help = "No frame generation technology runs on this OS and GPU";
        }
    };
    menu::Page page(group, "CHANGES ARE SAVED AUTOMATICALLY");
    menu::appendSettingRows(page, settingDefs_, [this]() { onSettingRowChanged(); }, options);
    menu::push(settingsMenu_, std::move(page));
    settingsMenu_.legend = {{"UP/DOWN", "SELECT"}, {"ENTER", "CHANGE"},
                            {"LEFT/RIGHT", "ADJUST"}, {"ESC", "BACK"}};
}

void Game::refreshFrameGenStatus() {
    // A MODE change takes effect with the next frame (FSR swaps the
    // swapchain), so the ACTIVE row and the enabled options are refreshed
    // every frame while visible.
    menu::Page* page = settingsMenu_.stack.current();
    if (!page || page->title != settings::kFrameGenGroup) return;
    refreshFrameGenRows(*page, settingDefs_, gameSettings_);
    const FrameGenStatus status = renderer.frameGenerationStatus();
    std::string active = status.active.empty() ? std::string("OFF") : status.active;
    for (char& c : active) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (menu::Row& row : page->rows) {
        if (row.kind != menu::RowKind::Info || row.label != "ACTIVE") continue;
        row.value = active;
        row.help = !status.note.empty() ? status.note
                 : (active == "OFF" ? std::string("Frame generation is off")
                                    : std::string("Frame generation running now"));
    }
}

void Game::openControlsSettings() {
    // Built fresh on every open: bindings may have changed since.
    menu::Page controls = menu::buildControlsPage(settingDefs_, [this]() { saveSettings(); });
    controls.add(menu::separator("ACTIONS"));
    controls.add(menu::action("RESET BINDINGS", [this]() { resetBindingsToDefaults(); },
                              "Restore the default key bindings"));
    menu::push(settingsMenu_, std::move(controls));
    settingsMenu_.legend = {{"ENTER", "REBIND"}, {"R", "DEFAULTS"}, {"ESC", "BACK"}};
}

void Game::openFrameGenSettings() {
    const GraphicsCapabilities& caps = graphicsCapabilities();
    menu::Page page = menu::buildFrameGenPage(settingDefs_,
                                              [this]() { onSettingRowChanged(); },
                                              settingRowsOptions(gameSettings_));

    // What runs right now and, when nothing does, why (renderer, GPU, files).
    const std::string api = activeRenderApiName(renderer);
    std::string supported;
    auto addSupported = [&supported](const char* name) {
        if (!supported.empty()) supported += " + ";
        supported += name;
    };
    if (caps.frameGenDlss) addSupported("DLSS");
    if (caps.frameGenFsr) addSupported("FSR");
    if (caps.frameGenXess) addSupported("XESS");
    if (supported.empty()) supported = "NONE";

    page.add(menu::separator("STATUS"));
    page.add(menu::info("RENDERER", api, "Graphics API the game is running on"));
    page.add(menu::info("PLATFORM", caps.os,
                        "Frame generation support depends on the OS and GPU"));
    page.add(menu::info("SUPPORT", supported,
                        supported == "NONE"
                            ? "No frame generation technology is available on this system"
                            : "Technologies this OS and GPU can run"));
    page.add(menu::info("VULKAN", caps.vulkanAvailable ? "AVAILABLE" : "UNAVAILABLE",
                        caps.vulkanNote));
    page.add(menu::info("ACTIVE", "", ""));   // filled by refreshFrameGenStatus()

    menu::push(settingsMenu_, std::move(page));
    refreshFrameGenStatus();
    settingsMenu_.legend = {{"UP/DOWN", "SELECT"}, {"LEFT/RIGHT", "ADJUST"}, {"ESC", "BACK"}};
}

void Game::resetBindingsToDefaults() {
    const settings::GameSettings defaults;
    gameSettings_.bindForward  = defaults.bindForward;
    gameSettings_.bindBackward = defaults.bindBackward;
    gameSettings_.bindLeft     = defaults.bindLeft;
    gameSettings_.bindRight    = defaults.bindRight;
    gameSettings_.bindJump     = defaults.bindJump;
    gameSettings_.bindSprint   = defaults.bindSprint;
    gameSettings_.bindSneak    = defaults.bindSneak;
    gameSettings_.bindProne    = defaults.bindProne;
    gameSettings_.bindShoot    = defaults.bindShoot;
    gameSettings_.bindScope    = defaults.bindScope;
    gameSettings_.bindInspect  = defaults.bindInspect;
    gameSettings_.bindWeapon1  = defaults.bindWeapon1;
    gameSettings_.bindWeapon2  = defaults.bindWeapon2;
    gameSettings_.bindWeapon3  = defaults.bindWeapon3;
    gameSettings_.bindWeapon4  = defaults.bindWeapon4;
    gameSettings_.bindWeapon5  = defaults.bindWeapon5;
    gameSettings_.bindGrenade  = defaults.bindGrenade;
    gameSettings_.bindUse      = defaults.bindUse;
    gameSettings_.bindUnstuck  = defaults.bindUnstuck;
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

std::string Game::playerDisplayName() const {
    const std::string discordName = discord_presence::username();
    return discordName.empty() ? std::string("playerX") : discordName;
}

void Game::openMapSelection() {
    // Leaving the setup screen ends our lobby membership; the map list then
    // shows the setups other players are waiting in.
    leaveLobby();
    menu::Page blank("SELECT MAP", "CHOOSE A MAP AND A GAME MODE");
    openMenuView(mapMenu_, std::move(blank));
    mapMenu_.legend = {{"UP/DOWN", "MAP"}, {"LEFT/RIGHT", "CHANGE"},
                       {"ENTER", "PLAY"}, {"ESC", "LEAVE"}};
    mapMenu_.onClose = [this]() { closeMenuToGame(); };
    refreshMapSelectionRows();
}

void Game::refreshMapSelectionRows() {
    static const char* const kModeNames[] = {"DEATHMATCH", "TEAM DEATHMATCH", "BOMB DEFUSAL"};
    static const char* const kFormatNames[] = {"FREE PLAY", "NORMAL", "SWIFT"};

    menu::Page page("SELECT MAP", "CHOOSE A MAP AND A GAME MODE");
    const int previousSelection = mapMenu_.stack.selection();

    // Open match setups come first: they are the "join a friend" entries.
    page.add(menu::separator("OPEN MATCH SETUPS"));
    bool anyLobby = false;
    for (size_t i = 0; i < mapFiles.size(); ++i) {
        const LobbyView* lobby = findLobby(mapFiles[i]);
        if (!lobby || lobby->empty()) continue;
        anyLobby = true;
        const int mode = std::max(0, std::min(2, lobby->gameMode));
        const int format = std::max(0, std::min(2, lobby->matchFormat));
        const int botsA = lobby->botsTeamA;
        const int botsB = lobby->botsTeamB;
        const int ready = lobby->readyCount;
        const int total = static_cast<int>(lobby->players.size());
        const std::string mapFile = mapFiles[i];
        page.add(menu::action(mapDisplayNames[i], [this, i, mapFile, mode, format,
                                                  botsA, botsB]() {
            currentGameMode = static_cast<GameMode>(mode);
            matchFormat_ = static_cast<MatchFormat>(format);
            botSetupTeamA_ = botsA;
            botSetupTeamB_ = botsB;
            botSetupBots_ = botsA + botsB;
            mapSelection = static_cast<int>(i);
            startSelectedMap(mapFile);
        }, "Join this open match setup"));
        page.rows.back().value = std::string(kModeNames[mode]) + "   " +
                                 std::to_string(ready) + "/" + std::to_string(total) +
                                 " READY";
        std::string teamA;
        std::string teamB;
        std::string everyone;
        for (const LobbyPlayerView& player : lobby->players) {
            const std::string item = player.name + (player.ready ? "*" : "");
            if (lobby->gameMode >= 1) {
                std::string& target = (player.team == 0) ? teamA : teamB;
                if (!target.empty()) target += "  ";
                target += item;
            }
            everyone += (everyone.empty() ? "" : ", ") + item;
        }
        if (lobby->gameMode >= 1) {
            page.rows.back().help = "Team A: " + (teamA.empty() ? std::string("-") : teamA) +
                                    "   |   Team B: " + (teamB.empty() ? std::string("-") : teamB) +
                                    "   (* = ready)";
        } else {
            page.rows.back().help = "Players: " + everyone + "   (* = ready)";
        }
    }
    if (!anyLobby) {
        page.add(menu::info("NO OPEN MATCH SETUPS", "",
                            "Pick a map below to create one for other players"));
    }

    page.add(menu::separator("MAPS"));
    for (size_t i = 0; i < mapDisplayNames.size(); ++i) {
        const LobbyView* lobby = findLobby(mapFiles[i]);
        const bool hasLobby = lobby && !lobby->empty();
        const int mode = hasLobby ? std::max(0, std::min(2, lobby->gameMode))
                                  : static_cast<int>(currentGameMode);
        const int format = hasLobby ? std::max(0, std::min(2, lobby->matchFormat))
                                    : static_cast<int>(matchFormat_);
        const int botsA = hasLobby ? lobby->botsTeamA : botSetupTeamA_;
        const int botsB = hasLobby ? lobby->botsTeamB : botSetupTeamB_;
        const std::string mapFile = mapFiles[i];
        page.add(menu::action(mapDisplayNames[i], [this, i, mapFile, hasLobby,
                                                  mode, format, botsA, botsB]() {
            if (hasLobby) {
                currentGameMode = static_cast<GameMode>(mode);
                matchFormat_ = static_cast<MatchFormat>(format);
                botSetupTeamA_ = botsA;
                botSetupTeamB_ = botsB;
                botSetupBots_ = botsA + botsB;
            }
            mapSelection = static_cast<int>(i);
            startSelectedMap(mapFile);
        }, hasLobby ? "Join the open setup on this map"
                    : "Create a match setup on this map"));
        if (hasLobby) {
            page.rows.back().value = "SETUP " + std::to_string(lobby->readyCount) + "/" +
                                     std::to_string(lobby->players.size()) + " READY";
        } else if (isConnectedToServer() && i < mapPlayerCounts.size() &&
                   mapPlayerCounts[i] > 0) {
            page.rows.back().value = std::to_string(mapPlayerCounts[i]) + " PLAYING";
        }
    }

    page.add(menu::separator("GAME MODE"));
    page.add(menu::cycle("MODE", {kModeNames[0], kModeNames[1], kModeNames[2]},
                         static_cast<int>(currentGameMode),
                         [this](int index) {
                             currentGameMode = static_cast<GameMode>(index);
                             // Dropping out of a round mode must clear the
                             // bomb sites/planted bomb immediately, even if
                             // the current map is not reloaded.
                             refreshBombSitesForMode();
                         },
                         "LEFT/RIGHT change the mode"));
    page.add(menu::cycle("MATCH", {kFormatNames[0], kFormatNames[1], kFormatNames[2]},
                         static_cast<int>(matchFormat_),
                         [this](int index) {
                             matchFormat_ = static_cast<MatchFormat>(index);
                         },
                         "FREE PLAY, NORMAL or SWIFT match length"));

    menu::replaceCurrent(mapMenu_, std::move(page));
    if (previousSelection >= 0) mapMenu_.stack.setSelection(previousSelection);
}

// ---------------------------------------------------------------------------
// Starting a map
// ---------------------------------------------------------------------------

void Game::startSelectedMap(const std::string& mapFile) {
    // Choosing a map opens the pre-match team / bot setup. The map is only
    // loaded and the match started from that screen (see beginMatch()).
    pendingMapFile_ = mapFile;
    lobbyReady_ = false;
    openTeamSetup();
    gameState = TEAM_SETUP;
}

void Game::startMatchNow(const std::string& mapFile) {
    static const char* const kModeNames[] = {"Deathmatch", "Team Deathmatch", "Bomb Defusal"};
    const int modeIndex = static_cast<int>(currentGameMode);
    std::cout << "Loading " << mapFile << " with game mode "
              << kModeNames[(modeIndex >= 0 && modeIndex < 3) ? modeIndex : 0] << "..." << std::endl;

    // Tell the server (if any) and drop remote players from the old map.
    if (networkClient && networkClient->isConnected()) {
        networkClient->sendMapChange(mapFile);
        // After the map change the server sees the new map name, so a fresh
        // lobby request resets that map's scoreboard (not the previous one).
        // publishLobby is false: the server already started this match.
        sendLobbySetup(true, false);
        // A fresh bomb match needs an explicit first round on the server; an
        // ongoing match would otherwise still be waiting for a round start.
        if (currentGameMode == BOMB_DEFUSAL) {
            networkClient->sendRoundStart(static_cast<uint8_t>(currentGameMode),
                                          static_cast<uint8_t>(attackingTeam & 1),
                                          static_cast<uint8_t>(matchFormat_), true);
        }
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
            remotePlayers[i].stance         = 0;
            remotePlayers[i].stateSequence  = 0;
        }
        playerCount = 1;
    }

    // Loads on a worker thread when possible; the menu stays responsive and
    // the map is swapped in by pollMapLoad() once ready.
    requestMapLoad(mapFile);

    menuJustEntered = true;
    gameInProgress_ = true;
    gameState = PLAYING;
    playUiSound("round_start");
    capturePlayCursor(true);
}

// ---------------------------------------------------------------------------
// Pre-match team / bot setup
// ---------------------------------------------------------------------------

void Game::openTeamSetup() {
    menuJustEntered = true;   // ignore the ENTER that selected the map
    sendLobbySetup();         // ask the server for its current pool / limits
    refreshTeamSetupRows();
    teamMenu_.legend = {{"UP/DOWN", "SELECT"}, {"LEFT/RIGHT", "CHANGE"},
                        {"ENTER", "START"}, {"ESC", "BACK"}};
    teamMenu_.onClose = [this]() {
        openMapSelection();
        gameState = MAP_SELECTION;
    };
}

void Game::refreshTeamSetupRows() {
    static const char* const kModeNames[] = {"DEATHMATCH", "TEAM DEATHMATCH", "BOMB DEFUSAL"};
    static const char* const kFormatNames[] = {"FREE PLAY", "NORMAL", "SWIFT"};

    const bool online = isConnectedToServer();
    const bool teamMode = matchUsesTeamScore();
    const std::string mapLabel = pendingMapFile_.empty()
        ? std::string("SELECT A MAP")
        : formatMapDisplayName(pendingMapFile_);
    const std::string subtitle = mapLabel + "   " +
        kModeNames[static_cast<int>(currentGameMode) % 3] + "   " +
        kFormatNames[static_cast<int>(matchFormat_) % 3];
    menu::Page page("MATCH SETUP", subtitle);
    const int previousSelection = teamMenu_.stack.selection();

    const LobbyView* lobby = online ? findLobby(pendingMapFile_) : nullptr;
    const uint32_t selfId = networkClient ? networkClient->getClientId() : 0;
    const std::string localName = playerDisplayName();

    // The ready control lives at the top so it never scrolls out of reach.
    page.add(menu::separator("LOBBY"));
    if (online) {
        const int humans = lobby ? static_cast<int>(lobby->players.size()) : 1;
        const int ready = lobby ? lobby->readyCount : (lobbyReady_ ? 1 : 0);
        page.add(menu::info("PLAYERS", std::to_string(ready) + "/" +
                                        std::to_string(humans) + " READY",
                            "Every player in this setup must press READY"));
        menu::Row readyRow = menu::toggle("READY", lobbyReady_,
            [this](bool on) {
                lobbyReady_ = on;
                sendLobbySetup();
                refreshTeamSetupRows();
            },
            "Press READY when the setup is final");
        readyRow.onText = "READY";
        readyRow.offText = "WAITING";
        readyRow.value = lobbyReady_ ? "READY" : "WAITING";
        page.add(std::move(readyRow));
    } else {
        page.add(menu::action("START MATCH", [this]() { beginMatch(); },
                              "Load the map and start playing"));
    }

    if (teamMode) {
        // Keep the displayed roster and the counters in sync before building
        // either. Deathmatch has no sides, so only the team modes show this.
        if (online) {
            const int pool = std::max(0, serverBotPool_);
            botSetupTeamA_ = std::max(0, std::min(botSetupTeamA_, pool));
            botSetupTeamB_ = std::max(0, std::min(botSetupTeamB_, pool - botSetupTeamA_));
        } else {
            constexpr int kMaxPerTeam = 8;
            botSetupTeamA_ = std::max(0, std::min(botSetupTeamA_, kMaxPerTeam));
            botSetupTeamB_ = std::max(0, std::min(botSetupTeamB_, kMaxPerTeam));
        }
        botSetupBots_ = botSetupTeamA_ + botSetupTeamB_;

        appendTeamRoster(page, lobby, localName, selfId, lobbyReady_, online,
                         playerTeam & 1, botSetupTeamA_, botSetupTeamB_);

        page.add(menu::separator("YOUR TEAM"));
        page.add(menu::cycle("TEAM", {"TEAM A", "TEAM B"}, playerTeam & 1,
                             [this](int index) {
                                 playerTeam = index & 1;
                                 lobbyReady_ = false;
                                 sendLobbySetup();
                                 refreshTeamSetupRows();
                             },
                             "Switch sides before the match starts"));
    } else {
        appendLobbyPlayers(page, lobby, localName, selfId, lobbyReady_, online);
    }

    page.add(menu::separator("BOTS"));
    if (online) {
        const int pool = std::max(0, serverBotPool_);
        page.add(menu::info("SERVER BOT POOL", std::to_string(pool)));
        if (teamMode) {
            page.add(menu::cycle("TEAM A BOTS", buildCountOptions(pool), botSetupTeamA_,
                                 [this, pool](int index) {
                                     botSetupTeamA_ = index;
                                     // Never allocate more than the server owns.
                                     botSetupTeamB_ = std::min(botSetupTeamB_, pool - index);
                                     botSetupBots_ = botSetupTeamA_ + botSetupTeamB_;
                                     lobbyReady_ = false;
                                     sendLobbySetup();
                                     refreshTeamSetupRows();
                                 },
                                 "Bots assigned to Team A"));
            page.add(menu::cycle("TEAM B BOTS", buildCountOptions(pool), botSetupTeamB_,
                                 [this, pool](int index) {
                                     botSetupTeamB_ = index;
                                     botSetupTeamA_ = std::min(botSetupTeamA_, pool - index);
                                     botSetupBots_ = botSetupTeamA_ + botSetupTeamB_;
                                     lobbyReady_ = false;
                                     sendLobbySetup();
                                     refreshTeamSetupRows();
                                 },
                                 "Bots assigned to Team B"));
        } else {
            botSetupBots_ = std::max(0, std::min(botSetupBots_, pool));
            page.add(menu::cycle("BOTS", buildCountOptions(pool), botSetupBots_,
                                 [this](int index) {
                                     botSetupBots_ = index;
                                     lobbyReady_ = false;
                                     sendLobbySetup();
                                 },
                                 "Bots in this deathmatch"));
        }
        int maxAccuracy = serverBotMaxAccuracy_ > 0 ? serverBotMaxAccuracy_ : 100;
        maxAccuracy = (maxAccuracy / 10) * 10;
        if (maxAccuracy < 10) maxAccuracy = 10;
        if (botSetupAccuracy_ > maxAccuracy) botSetupAccuracy_ = maxAccuracy;
        page.add(menu::cycle("BOT STRENGTH", buildAccuracyOptions(maxAccuracy),
                             std::max(0, botSetupAccuracy_ / 10 - 1),
                             [this, maxAccuracy](int index) {
                                 botSetupAccuracy_ = (index + 1) * 10;
                                 (void)maxAccuracy;
                                 lobbyReady_ = false;
                                 sendLobbySetup();
                             },
                             "Bots cannot exceed the server's accuracy setting"));
    } else {
        constexpr int kMaxPerTeam = 8;
        constexpr int kMaxBots = 16;
        if (teamMode) {
            page.add(menu::cycle("TEAM A BOTS", buildCountOptions(kMaxPerTeam), botSetupTeamA_,
                                 [this](int index) {
                                     botSetupTeamA_ = index;
                                     botSetupBots_ = botSetupTeamA_ + botSetupTeamB_;
                                 },
                                 "Single-player bots on Team A"));
            page.add(menu::cycle("TEAM B BOTS", buildCountOptions(kMaxPerTeam), botSetupTeamB_,
                                 [this](int index) {
                                     botSetupTeamB_ = index;
                                     botSetupBots_ = botSetupTeamA_ + botSetupTeamB_;
                                 },
                                 "Single-player bots on Team B"));
        } else {
            botSetupBots_ = std::max(0, std::min(botSetupBots_, kMaxBots));
            page.add(menu::cycle("BOTS", buildCountOptions(kMaxBots), botSetupBots_,
                                 [this](int index) { botSetupBots_ = index; },
                                 "Single-player bots in this deathmatch"));
        }
        page.add(menu::cycle("BOT STRENGTH", buildAccuracyOptions(100),
                             std::max(0, botSetupAccuracy_ / 10 - 1),
                             [this](int index) { botSetupAccuracy_ = (index + 1) * 10; },
                             "Per-bot aim accuracy (single player has no server cap)"));
    }

    page.add(menu::separator("ACTIONS"));
    page.add(menu::action("BACK", [this]() {
        openMapSelection();
        gameState = MAP_SELECTION;
    }, "Return to map selection"));

    teamMenu_.legend.clear();
    teamMenu_.legend.push_back({"UP/DOWN", "SELECT"});
    teamMenu_.legend.push_back({"LEFT/RIGHT", "CHANGE"});
    teamMenu_.legend.push_back({"ENTER", online ? "READY" : "START"});
    teamMenu_.legend.push_back({"ESC", "BACK"});

    menu::replaceCurrent(teamMenu_, std::move(page));
    if (previousSelection >= 0) teamMenu_.stack.setSelection(previousSelection);
}

void Game::sendLobbySetup(bool freshMatch, bool publishLobby) {
    if (!networkClient || !networkClient->isConnected()) return;

    // Keep the roster name current even if Discord connected after the session
    // was established.
    networkClient->setPlayerName(playerDisplayName());
    networkClient->sendLobbySetup(
        static_cast<uint8_t>(playerTeam & 1),
        static_cast<uint8_t>(setupBotsTeamA()),
        static_cast<uint8_t>(setupBotsTeamB()),
        static_cast<uint8_t>(std::max(0, std::min(100, botSetupAccuracy_))),
        static_cast<uint8_t>(matchFormat_), freshMatch, pendingMapFile_,
        static_cast<uint8_t>(currentGameMode),
        publishLobby && lobbyReady_, publishLobby);
}

void Game::leaveLobby() {
    if (lobbyReady_) lobbyReady_ = false;
    if (!networkClient || !networkClient->isConnected()) return;
    sendLobbySetup(false, false);
}

void Game::beginMatch() {
    if (isConnectedToServer()) {
        // Online matches start through the lobby: every player must press
        // READY, then the server tells all of them to load the map together.
        lobbyReady_ = !lobbyReady_;
        sendLobbySetup();
        refreshTeamSetupRows();
        return;
    }

    sendLobbySetup();
    localBotsPending_ = (setupBotsTeamA() > 0 || setupBotsTeamB() > 0);
    if (!localBotsPending_) stopLocalBots();
    startMatchNow(pendingMapFile_);
}

void Game::applyLobbyState(uint8_t /*team*/, uint8_t pool, uint8_t botsA,
                           uint8_t botsB, uint8_t accuracy, uint8_t maxAccuracy,
                           uint8_t matchFormat) {
    serverBotPool_ = pool;
    serverBotMaxAccuracy_ = maxAccuracy > 0 ? maxAccuracy : 100;
    if (matchUsesTeamScore()) {
        botSetupTeamA_ = std::min<int>(botsA, pool);
        botSetupTeamB_ = std::min<int>(botsB, std::max(0, pool - botSetupTeamA_));
        botSetupBots_ = botSetupTeamA_ + botSetupTeamB_;
    } else {
        // Deathmatch has a single pool. Keep the remembered team split for a
        // later team match, but show the server's total here.
        botSetupBots_ = std::min<int>(botsA + botsB, pool);
    }
    if (accuracy > 0) botSetupAccuracy_ = std::min<int>(accuracy, serverBotMaxAccuracy_);
    (void)matchFormat;   // the local map-menu choice drives this client's match
    lobbyDirty_ = true;
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
    renderer.setVsync(vsyncEnabled);

    // Frame generation (FRAME GENERATION page); the renderer decides what can
    // run and reports it back (see openFrameGenSettings).
    {
        FrameGenSettings fg;
        switch (gameSettings_.frameGen) {
            case settings::FRAMEGEN_AUTO: fg.tech = FrameGenTech::Auto; break;
            case settings::FRAMEGEN_FSR:  fg.tech = FrameGenTech::FSR;  break;
            case settings::FRAMEGEN_DLSS: fg.tech = FrameGenTech::DLSS; break;
            case settings::FRAMEGEN_XESS: fg.tech = FrameGenTech::XeSS; break;
            default:                      fg.tech = FrameGenTech::Off;  break;
        }
        fg.generatedFrames = gameSettings_.frameGenOutput + 1;
        fg.lowLatency = gameSettings_.frameGenLowLatency;
        renderer.setFrameGeneration(fg);
    }

    // Infinite ammo is read from the game member by the gameplay code, and
    // mirrored into every weapon (their own canShoot()/shoot() check is what
    // stops ammo loss). Do not guard this with "value changed": weapons are
    // created after loadSettings(), so grenades/flashbangs used to stay on
    // their finite count while the HUD showed INF. Economic modes force it off.
    refreshWeaponInfiniteAmmo();

    // Audio: volumes apply live (the menu previews them through the mixer).
    audio.setMasterVolume(gameSettings_.masterVolume / 100.0f);
    audio.setSFXVolume(gameSettings_.effectsVolume / 100.0f);
    audio.setMusicVolume(gameSettings_.musicVolume / 100.0f);

    keyBindings.forward  = gameSettings_.bindForward;
    keyBindings.backward = gameSettings_.bindBackward;
    keyBindings.left     = gameSettings_.bindLeft;
    keyBindings.right    = gameSettings_.bindRight;
    keyBindings.jump     = gameSettings_.bindJump;
    keyBindings.sprint   = gameSettings_.bindSprint;
    keyBindings.sneak    = gameSettings_.bindSneak;
    keyBindings.prone    = gameSettings_.bindProne;
    // Bindings are stored as one integer for keys *and* mouse buttons:
    // keyboard codes are 32..348 and mouse buttons are encoded as 1000 + n.
    // glfwGetKeyName/glfwGetKey belong to keyboard codes only, so the engine
    // needs the two split apart here.
    keyBindings.shootButton = settings::mouseButtonOf(gameSettings_.bindShoot);
    keyBindings.shootKey    = keyBindings.shootButton >= 0 ? -1 : gameSettings_.bindShoot;
    keyBindings.shoot       = keyBindings.shootKey;   // kept for compatibility
    keyBindings.scopeButton = settings::mouseButtonOf(gameSettings_.bindScope);
    keyBindings.scopeKey    = keyBindings.scopeButton >= 0 ? -1 : gameSettings_.bindScope;
    keyBindings.weaponSlot[0] = gameSettings_.bindWeapon1;
    keyBindings.weaponSlot[1] = gameSettings_.bindWeapon2;
    keyBindings.weaponSlot[2] = gameSettings_.bindWeapon3;
    keyBindings.weaponSlot[3] = gameSettings_.bindWeapon4;
    keyBindings.weaponSlot[4] = gameSettings_.bindWeapon5;
    keyBindings.grenade  = gameSettings_.bindGrenade;
    keyBindings.inspect  = gameSettings_.bindInspect;
    keyBindings.buy      = gameSettings_.bindBuy;
    keyBindings.use      = gameSettings_.bindUse;
    keyBindings.unstuck  = gameSettings_.bindUnstuck;
    keyBindings.menu     = gameSettings_.bindMenu;

    // Discord Rich Presence is optional and can be toggled from Settings.
    // The integration no-ops when Discord is not running.
    discord_presence::setEnabled(gameSettings_.discordRichPresence);
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
        // Reopen the same path (e.g. SETTINGS > VIDEO > FRAME GENERATION).
        std::vector<std::string> path;
        for (size_t i = 1; i < settingsMenu_.stack.depth(); ++i)
            path.push_back(settingsMenu_.stack.pageAt(i).title);
        openSettings();
        for (const std::string& title : path) {
            if (title == "CONTROLS") openControlsSettings();
            else if (title == settings::kFrameGenGroup) openFrameGenSettings();
            else openSettingsGroup(title);
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
    capturePlayCursor(true);
    return currentMap != nullptr;
}

bool Game::debugStartMapWithModeForTest(const std::string& mapFile, int mode) {
    currentGameMode = static_cast<GameMode>(mode);
    std::unique_ptr<Map> map = std::make_unique<Map>("assets/maps/" + mapFile);
    installMap(std::move(map));
    menuJustEntered = true;
    gameInProgress_ = true;
    gameState = PLAYING;
    capturePlayCursor(true);
    return currentMap != nullptr;
}

void Game::debugStepForTest(int frames, float dt) {
    // No keys and no mouse are pressed while the test runs.
    for (int i = 0; i < frames; ++i) {
        update(dt);
    }
}

void Game::debugSetViewForTest(const glm::vec3& eye, float yawDeg, float pitchDeg) {
    camera.Position = eye;
    camera.Yaw = yawDeg;
    camera.Pitch = pitchDeg;
    camera.updateCameraVectors();
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
    if (key < 0) return false;
    if (settings::isMouseBinding(key)) {
        // Movement / stance bindings may be mouse buttons too. The scripted
        // test keys only stand in for keyboard codes, so use the real button.
        const int button = settings::mouseButtonOf(key);
        return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST &&
               window && glfwGetMouseButton(window, button) == GLFW_PRESS;
    }
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
    } else if (name == "video" || name == "audio" || name == "gameplay") {
        openSettings();
        std::string group = name;
        for (char& c : group) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        openSettingsGroup(group);
        gameState = SETTINGS;
    } else if (name == "framegen") {
        openSettings();
        openSettingsGroup("VIDEO");
        openFrameGenSettings();
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

