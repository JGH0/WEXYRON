#ifndef UI_HELPERS_HPP
#define UI_HELPERS_HPP

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <string>

// Forward declare Renderer to avoid circular includes
class Renderer;

namespace UIHelpers {

/// Virtual design canvas size (all UI coordinates are in this space)
static constexpr float DESIGN_W = 800.0f;
static constexpr float DESIGN_H = 600.0f;

/// Axis-aligned rectangle in virtual UI space
struct UIBounds {
    float left, right, top, bottom;
    float width()   const { return right - left; }
    float height()  const { return bottom - top; }
    float centerX() const { return (left + right) * 0.5f; }
    float centerY() const { return (top + bottom) * 0.5f; }
};

/// Compute virtual UI bounds for the current window size.
/// The design canvas (800x600) is always fully visible; extra pixels expand
/// the visible area uniformly without letter-boxing.
inline UIBounds getBounds(GLFWwindow* window) {
    int fbW = (int)DESIGN_W, fbH = (int)DESIGN_H;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = (int)DESIGN_W;
    if (fbH <= 0) fbH = (int)DESIGN_H;

    float scaleX = (float)fbW / DESIGN_W;
    float scaleY = (float)fbH / DESIGN_H;
    float uiScale = std::min(scaleX, scaleY);
    if (uiScale <= 0.0f) uiScale = 1.0f;

    float uiW   = (float)fbW / uiScale;
    float uiH   = (float)fbH / uiScale;
    float offX  = (uiW - DESIGN_W) * 0.5f;
    float offY  = (uiH - DESIGN_H) * 0.5f;

    return { -offX, DESIGN_W + offX, -offY, DESIGN_H + offY };
}

/// Set an orthographic projection on the renderer so all UI draws use
/// virtual 800x600 coordinates regardless of the actual window size.
/// Must include renderer.hpp before calling.
inline void setProjection(Renderer& renderer, GLFWwindow* window);

/// Compute a scale factor that is 1.0 at 800x600 and scales linearly
/// with the minimum of the horizontal / vertical zoom factors.
/// @param base  multiply the raw scale by this (e.g. 1.2 for titles)
/// @param minS  hard minimum (avoids unreadable tiny text)
/// @param maxS  hard maximum
inline float uiScale(GLFWwindow* window, float base = 1.0f,
                     float minS = 0.6f, float maxS = 2.0f) {
    auto b = getBounds(window);
    float s = std::min(b.width() / DESIGN_W, b.height() / DESIGN_H);
    float result = base * s;
    if (result < minS) result = minS;
    if (result > maxS) result = maxS;
    return result;
}

/// Return the scale needed so that `text` at `preferredScale` just fits
/// inside `maxWidth` pixels (design-space). If text is shorter it returns
/// `preferredScale` unchanged.
inline float fitScale(const std::string& text, float maxWidth,
                      float preferredScale, float charWidthPerUnit = 10.0f) {
    if (text.empty() || maxWidth <= 0.0f) return preferredScale;
    float textW = (float)text.size() * charWidthPerUnit * preferredScale;
    if (textW <= maxWidth) return preferredScale;
    return preferredScale * (maxWidth / textW);
}

} // namespace UIHelpers

// --- inline definition that needs Renderer ---
// (placed here so translation units only need one include)
#include "../core/renderer.hpp"
#include <glm/gtc/matrix_transform.hpp>

inline void UIHelpers::setProjection(Renderer& renderer, GLFWwindow* window) {
    const UIBounds b = getBounds(window);
    // Canvas y grows downwards: getBounds() returns top as the negative edge and
    // bottom as the positive one, so passing (bottom, top) to glm::ortho flips y
    // on screen exactly once. One canvas unit is one scale unit in both axes,
    // because `bottom - top` equals `right - left` in the design aspect.
    glm::mat4 proj = glm::ortho(b.left, b.right, b.bottom, b.top, -1.0f, 1.0f);
    // Everything submitted from here on is drawn in screen space, batched
    // separately from the 3D world.
    renderer.setProjection(glm::mat4(1.0f), proj);
}

#endif // UI_HELPERS_HPP