/**
 * @file menu_input.cpp
 * @brief Menu framework: keyboard and mouse input for menu::MenuView
 *
 * Every menu screen uses the same controls:
 *   UP / DOWN          move the selection (wraps, skips disabled rows)
 *   ENTER / SPACE      activate the selected row
 *   LEFT / RIGHT       step a value backwards / forwards
 *   ESC                back one page (the bottom page calls onClose)
 *   mouse              hover highlights, click selects and activates
 *   R                  restore defaults (only when the page provides them)
 *
 * While a key binding is captured, the next key or mouse button becomes the
 * binding; ESC cancels. Mouse buttons inside the menu never fire the binding,
 * so clicking "SHOOT" cannot bind the click that opened the capture.
 */

#include "menu.hpp"
#include "../core/settings.hpp"
#include "../ui/ui_helpers.hpp"
#include <algorithm>
#include <iostream>

namespace menu {

namespace {

/// Start capturing a binding on `row`.
void startCapture(MenuView& view, Row& row,
                  const std::function<void(bool)>& onCaptureChanged) {
    for (Row& other : view.stack.current()->rows) other.capturing = false;
    row.capturing = true;
    if (onCaptureChanged) onCaptureChanged(true);
}

void stopCapture(Row& row, const std::function<void(bool)>& onCaptureChanged) {
    row.capturing = false;
    if (onCaptureChanged) onCaptureChanged(false);
}

/// ENTER / SPACE / mouse click on the selected row.
void activate(MenuView& view, int index, const MenuInput& input) {
    Page* page = view.stack.current();
    if (!page || index < 0 || index >= static_cast<int>(page->rows.size())) return;
    Row& row = page->rows[index];
    if (!row.selectable()) return;

    switch (row.kind) {
        case RowKind::Action: {
            // If the action pushes a sub-page, popping it returns to this row.
            const size_t depthBefore = view.stack.depth();
            if (row.onActivate) row.onActivate();
            view.returnTo = (view.stack.depth() > depthBefore) ? index : -1;
            break;
        }
        case RowKind::Toggle:
            row.toggleValue();
            break;
        case RowKind::Cycle:
            row.cycleValue(+1);
            break;
        case RowKind::Keybind:
            startCapture(view, row, input.onCaptureChanged);
            break;
        case RowKind::Info:
        case RowKind::Separator:
            break;
    }
}

/// LEFT / RIGHT on a value row.
void adjust(MenuView& view, int index, int direction) {
    Page* page = view.stack.current();
    if (!page || index < 0 || index >= static_cast<int>(page->rows.size())) return;
    Row& row = page->rows[index];
    if (!row.selectable() || row.capturing) return;

    switch (row.kind) {
        case RowKind::Toggle:
            row.toggleValue();
            break;
        case RowKind::Cycle:
            row.cycleValue(direction);
            break;
        default:
            break;
    }
}

} // namespace

bool processInput(MenuView& view, GLFWwindow* window,
                  const MouseState& mouse, const MenuInput& input) {
    Page* page = view.stack.current();
    if (!page) return false;

    const bool anyCapture = isCapturing(view);
    bool activated = false;

    // ── Key binding capture ───────────────────────────────────────────────
    if (anyCapture) {
        if (input.pressed(GLFW_KEY_ESCAPE)) {
            for (Row& row : page->rows) {
                if (row.capturing) {
                    stopCapture(row, input.onCaptureChanged);
                    break;
                }
            }
            return false;
        }

        // First key or mouse button that went down becomes the binding.
        for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
            if (!input.pressed(key)) continue;
            if (input.isModifier && input.isModifier(key)) continue;   // wait for the real key
            for (Row& row : page->rows) {
                if (row.capturing) {
                    row.setBinding(key);
                    stopCapture(row, input.onCaptureChanged);
                    break;
                }
            }
            return false;
        }
        if (mouse.clicked) {
            // Only a click outside the menu rows counts as a button binding;
            // clicks on the captured row itself (to cancel/confirm) are ignored.
            const float mx = mouse.x;
            const float my = mouse.y;
            if (hitTest(view, mx, my) < 0 || !mouse.inside) {
                for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
                    // The click edge is only known for the left button; bind it.
                    if (glfwGetMouseButton(window, button) != GLFW_PRESS) continue;
                    for (Row& row : page->rows) {
                        if (row.capturing) {
                            row.setBinding(settings::mouseBinding(button));
                            stopCapture(row, input.onCaptureChanged);
                            break;
                        }
                    }
                    break;
                }
            }
        }
        return false;
    }

    // ── Mouse: hover + click ──────────────────────────────────────────────
    // The hover only follows the selection while the pointer moves: a mouse
    // resting somewhere must not undo keyboard navigation.
    const int hovered = (mouse.inside) ? hitTest(view, mouse.x, mouse.y) : -1;
    view.hoverIndex = hovered;
    const bool mouseMoved = mouse.inside &&
                            (mouse.x != view.lastMouse.x || mouse.y != view.lastMouse.y);
    if (mouse.inside) view.lastMouse = glm::vec2(mouse.x, mouse.y);
    if (hovered >= 0 && page->rows[hovered].selectable() && (mouseMoved || mouse.clicked)) {
        view.stack.setSelection(hovered);
    }
    if (mouse.clicked && hovered >= 0 && page->rows[hovered].selectable()) {
        activate(view, hovered, input);
        activated = true;
    }

    // ── Keyboard ──────────────────────────────────────────────────────────
    // The page may have been replaced by an action above; re-read it before
    // handling keys so a click-then-enter in one frame cannot act twice.
    page = view.stack.current();
    if (!page) return activated;

    if (input.pressed(GLFW_KEY_UP)) {
        view.stack.moveSelection(-1);
        activated = true;
    }
    if (input.pressed(GLFW_KEY_DOWN)) {
        view.stack.moveSelection(+1);
        activated = true;
    }
    if (input.pressed(GLFW_KEY_LEFT)) {
        adjust(view, view.stack.selection(), -1);
        activated = true;
    }
    if (input.pressed(GLFW_KEY_RIGHT)) {
        adjust(view, view.stack.selection(), +1);
        activated = true;
    }
    if (input.pressed(GLFW_KEY_ENTER) || input.pressed(GLFW_KEY_KP_ENTER) ||
        input.pressed(GLFW_KEY_SPACE)) {
        activate(view, view.stack.selection(), input);
        activated = true;
    }
    if (input.pressed(GLFW_KEY_ESCAPE)) {
        if (!pop(view)) {
            if (view.onClose) view.onClose();
        }
        activated = true;
    }
    return activated;
}

} // namespace menu
