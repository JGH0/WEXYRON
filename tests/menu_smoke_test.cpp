#include "core/game.hpp"
#include "ui/ui_helpers.hpp"
#include "ui/menu.hpp"
#include "core/settings.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Deterministic smoke test for the menu framework. Runs with a real GL
// context (Xvfb or a desktop) and drives the menu with synthetic input.

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

/// Build input where exactly one key is down (edge + held).
static menu::MenuInput oneKey(int key, menu::MouseState* mouse = nullptr) {
    (void)mouse;
    menu::MenuInput in;
    in.pressed = [key](int k) { return k == key; };
    in.held = [key](int k) { return k == key; };
    in.isModifier = [](int) { return false; };
    return in;
}

static menu::MenuInput noKey() {
    menu::MenuInput in;
    in.pressed = [](int) { return false; };
    in.held = [](int) { return false; };
    in.isModifier = [](int) { return false; };
    return in;
}

int main() {
    Game game(800, 600);
    game.init();

    // Regression: the main menu must exist as soon as init() is done. A menu
    // state without a page draws an empty screen (the "blank screen" bug).
    CHECK(game.debugMainMenuReady());

    // ── Settings overview: one entry per group, then CONTROLS ──────────────
    // (Every group must stay reachable; the single long page used to push
    // AUDIO off the screen.)
    game.debugOpenMenu("settings");
    std::printf("--- settings overview ---\n");
    game.debugMenuDump();
    {
        const menu::Page* page = game.debugMenuPage();
        CHECK(page != nullptr);
        std::vector<std::string> labels;
        if (page)
            for (const menu::Row& row : page->rows) labels.push_back(row.label);
        const std::vector<std::string> expected = {"GAMEPLAY", "VIDEO", "AUDIO", "INTERFACE", "CONTROLS"};
        CHECK(labels == expected);
    }

    // ── GAMEPLAY page: one row per setting of the group ────────────────────
    game.debugOpenMenu("gameplay");
    std::printf("--- gameplay page ---\n");
    game.debugMenuDump();
    std::vector<menu::Row> rows;
    {
        menu::Page* page = game.debugMenuPage();
        CHECK(page != nullptr);
        if (page) rows = page->rows;
    }

    // Toggle the first setting with ENTER and check the game member changed.
    const bool before = game.gameSettings().infiniteAmmo;
    menu::MouseState mouse;
    menu::processInput(*game.debugMenuView(), game.windowHandle(), mouse,
                       oneKey(GLFW_KEY_ENTER));
    CHECK(game.gameSettings().infiniteAmmo != before);

    // One frame sees one activation, even though the framework asks "ENTER?"
    // and "KP ENTER?" separately for the same press (the bug where a held or
    // repeated ENTER toggled the setting again and again): count activations.
    {
        int activations = 0;
        menu::Page* page = game.debugMenuPage();
        const bool now = game.gameSettings().infiniteAmmo;
        for (menu::Row& row : page->rows) {
            if (row.kind == menu::RowKind::Toggle) {
                row.onChanged = [&activations, now](bool on) {
                    ++activations;
                    CHECK(on != now);   // every activation is a real change
                };
                break;
            }
        }
        menu::processInput(*game.debugMenuView(), game.windowHandle(), mouse,
                           oneKey(GLFW_KEY_ENTER));
        CHECK(activations == 1);
    }

    std::printf("--- settings page after toggle ---\n");
    game.debugMenuDump();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].kind == menu::RowKind::Toggle) std::printf("  toggle row %zu: %s\n", i, rows[i].value.c_str());
    }

    // ── Key capture: rebind with a synthetic key press ─────────────────────
    game.debugOpenMenu("settings");
    // Select the CONTROLS row and open it.
    CHECK(game.debugMenuSelect("CONTROLS"));
    menu::processInput(*game.debugMenuView(), game.windowHandle(), mouse, oneKey(GLFW_KEY_ENTER));
    std::printf("--- controls page ---\n");
    game.debugMenuDump();
    CHECK(game.debugMenuDepth() == 2);
    CHECK(game.debugMenuSelect("JUMP"));
    menu::processInput(*game.debugMenuView(), game.windowHandle(), mouse, oneKey(GLFW_KEY_ENTER));
    CHECK(menu::isCapturing(*game.debugMenuView()));
    const int oldJump = game.gameSettings().bindJump;
    // Bind a key that is not the current one (the settings file may already
    // have JUMP on F from an earlier run).
    const int newJump = (oldJump == GLFW_KEY_F) ? GLFW_KEY_G : GLFW_KEY_F;
    menu::processInput(*game.debugMenuView(), game.windowHandle(), mouse, oneKey(newJump));
    CHECK(!menu::isCapturing(*game.debugMenuView()));
    CHECK(game.gameSettings().bindJump == newJump);
    CHECK(game.gameSettings().bindJump != oldJump);

    // ESC on the controls page pops back to settings (and to that row).
    menu::processInput(*game.debugMenuView(), game.windowHandle(), mouse, oneKey(GLFW_KEY_ESCAPE));
    CHECK(game.debugMenuDepth() == 1);
    CHECK(game.debugMenuSelection() >= 0);

    // ── Map page: cycle the mode with RIGHT ────────────────────────────────
    game.debugOpenMenu("maps");
    CHECK(game.debugMenuSelect("MODE"));
    menu::processInput(*game.debugMenuView(), game.windowHandle(), mouse, oneKey(GLFW_KEY_RIGHT));
    CHECK(game.debugGameMode() == Game::TEAM_VS_TEAM);
    menu::processInput(*game.debugMenuView(), game.windowHandle(), mouse, oneKey(GLFW_KEY_LEFT));
    CHECK(game.debugGameMode() == Game::DEATHMATCH);

    // ── Scrolling: a long page keeps the selection inside the window ───────
    menu::Page longPage("SCROLL", "TEST");
    for (int i = 0; i < 30; ++i) longPage.add(menu::action("ROW " + std::to_string(i), nullptr));
    game.debugShowPage(std::move(longPage));
    for (int i = 0; i < 20; ++i) {
        menu::processInput(*game.debugMenuView(), game.windowHandle(), mouse, oneKey(GLFW_KEY_DOWN));
    }
    CHECK(game.debugMenuSelection() == 20);
    // MenuView::draw would compute the window; the dump must not crash and the
    // selection must still be the visible one.
    game.debugMenuDump();

    // Shooting: the binding is stored as "mouse:0" and must reach the engine as
    // a *mouse button*, not as a key code (that made shooting do nothing).
    if (game.gameSettings().bindShoot == settings::mouseBinding(GLFW_MOUSE_BUTTON_LEFT)) {
        CHECK(game.debugShootButton() == GLFW_MOUSE_BUTTON_LEFT);
        CHECK(game.debugShootKey() < 0);
    }

    // A key binding for shooting must work too: fire on the key, not the mouse.
    game.gameSettings().bindShoot = GLFW_KEY_F;
    game.applySettingsForTest();
    CHECK(game.debugShootButton() < 0);
    CHECK(game.debugShootKey() == GLFW_KEY_F);

    std::printf(failures ? "MENU TEST: %d FAILURES\n" : "MENU TEST: ALL OK\n", failures);
    return failures ? 1 : 0;
}
