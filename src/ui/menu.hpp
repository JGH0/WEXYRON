/**
 * @file menu.hpp
 * @brief In-game menu framework: draws a menu::MenuStack and routes input
 *
 * Usage from a game state (see game_menus.cpp):
 *
 *     // when the screen opens (main menu = bottom page of the stack)
 *     menus.setPersistence(true);          // keep selection between visits
 *     menus.begin(buildMainMenuPage());
 *
 *     // once per frame, before drawing
 *     menus.processInput(window, mouseState, inp);
 *
 *     // when drawing (with the UI projection active)
 *     menus.draw(renderer, window);
 *
 * Sub-screens are pages pushed on the stack:
 *     menus.push(buildSettingsPage());
 *     menus.pop();                          // from a Back row
 *
 * Adding a menu screen: build a menu::Page (title + rows) and push() it.
 * Adding an option to the settings screen: add one member to
 * settings::GameSettings and one row in settings.cpp - the settings page is
 * generated from that registry, including its entry in the settings file.
 */

#ifndef MENU_HPP
#define MENU_HPP

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <functional>
#include <string>
#include <vector>
#include "menu_core.hpp"

class Renderer;

namespace menu {

/// Mouse state in canvas coordinates (y grows downwards, like UIHelpers).
struct MouseState {
    float x = 0.0f;
    float y = 0.0f;
    bool  inside = false;   ///< pointer is over the window
    bool  clicked = false;  ///< left button pressed this frame
};

/// Keyboard/mouse input the framework needs. The game fills this in every
/// frame, so the framework never reaches into game state itself.
struct MenuInput {
    /// True on the frame a key went down (used for navigation).
    std::function<bool(int key)> pressed;
    /// True while any key is down (used while waiting for a new binding).
    std::function<bool(int key)> held;
    /// True for Shift/Ctrl/Alt/Super, which are not bound on their own.
    std::function<bool(int key)> isModifier;
    /// Called when the framework starts/stops waiting for a binding, so the
    /// game can reset its key edges (a held ENTER/ESC must not leak into the
    /// state after the capture ends).
    std::function<void(bool capturing)> onCaptureChanged;
};

/// One rectangle drawn by the last draw call, for mouse hit testing.
struct RowHit {
    int   rowIndex = -1;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};

/**
 * @brief A menu stack plus everything needed to draw and drive it.
 *
 * The game owns one view per screen (main menu, in-game pause menu, ...).
 */
struct MenuView {
    MenuStack stack;                  ///< the pages
    Theme theme;                      ///< colours
    std::vector<ScrollState> scroll;  ///< per stack page
    int hoverIndex = -1;              ///< row under the mouse (-1: none)
    /// Last mouse position seen. The hover only moves the selection when the
    /// mouse actually moves, so a mouse resting over a row cannot steal the
    /// keyboard selection (keyboard and mouse can both drive the menu).
    glm::vec2 lastMouse{-1.0e9f, -1.0e9f};
    std::vector<RowHit> hits;         ///< rects of the rows drawn last frame
    std::function<void()> onClose;    ///< ESC on the bottom page
    int returnTo = -1;                ///< selection restored after pop()
    std::vector<KeyHint> legend;      ///< controls shown at the bottom
    float lastTime = -1.0f;           ///< animation clock
};

/// Reset the stack to `page` as its only page (the main menu).
void begin(MenuView& view, Page page);

/// Push a sub-screen.
void push(MenuView& view, Page page);

/// Pop the current page. Returns false when already on the bottom page.
bool pop(MenuView& view);

/// Forget selection/scroll/animation but keep the stack and settings.
void resetView(MenuView& view);

/// Replace the current page in place (same stack depth), keeping the
/// selection on the row with the same label when possible. Used by screens
/// whose rows change while open (server list, map list).
void replaceCurrent(MenuView& view, Page page);

/// True while a key binding is being captured.
bool isCapturing(const MenuView& view);

/// Route one frame of input (keys + mouse) to the menu. Returns true when a
/// row was activated this frame.
bool processInput(MenuView& view, GLFWwindow* window,
                  const MouseState& mouse, const MenuInput& input);

/// Draw the current page (call with the UI projection active).
void draw(MenuView& view, Renderer& renderer, GLFWwindow* window);

/// Advance scroll animations (called by draw(); exposed for tests).
void tick(MenuView& view, float deltaTime);

/// Hit test the current page against a canvas position; -1 when no row.
int hitTest(const MenuView& view, float x, float y);

} // namespace menu

#endif // MENU_HPP
