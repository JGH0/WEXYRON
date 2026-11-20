/**
 * @file game_editor.cpp
 * @brief Map editor: modern panel UI, mouse placement, undo/redo, save/load
 *
 * The editor is a small 3D level tool, not a second game mode:
 *   - the world is rendered exactly like a match (map + spawn markers)
 *   - panels on the left/right/top/bottom hold every tool and setting, so no
 *     control has to be discovered from a wall of text
 *   - the mouse aims into the scene (ray cast from the cursor), the left button
 *     applies the active tool, the right button drags the camera
 *   - every destructive action can be undone; saving writes a real .map file
 *     and rescans the map list, so players can build as many maps as they want
 */

#include "game.hpp"
#include "../ui/ui_helpers.hpp"
#include "engine/map_loader.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {

// ── Palette (matches ui/menu_core.hpp, the rest of the game's UI) ──────────
const glm::vec3 kPanel(0.06f, 0.08f, 0.11f);
const glm::vec3 kPanelInner(0.09f, 0.11f, 0.15f);
const glm::vec3 kRow(0.16f, 0.19f, 0.25f);
const glm::vec3 kRowHover(0.21f, 0.25f, 0.32f);
const glm::vec3 kEdge(0.28f, 0.33f, 0.42f);
const glm::vec3 kAccent(0.95f, 0.78f, 0.20f);
const glm::vec3 kAccentSoft(0.98f, 0.62f, 0.20f);
const glm::vec3 kInk(0.10f, 0.09f, 0.05f);
const glm::vec3 kLabel(0.85f, 0.88f, 0.92f);
const glm::vec3 kMuted(0.62f, 0.68f, 0.76f);
const glm::vec3 kOn(0.45f, 0.78f, 0.32f);
const glm::vec3 kDanger(0.95f, 0.40f, 0.35f);
const glm::vec3 kBuild(0.35f, 0.95f, 0.45f);
const glm::vec3 kPaint(0.35f, 0.65f, 1.00f);
const glm::vec3 kPick(1.00f, 0.85f, 0.25f);

constexpr float kGlyphHeight = 40.0f;  ///< 5 glyph rows * 8px

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
};

Rect centred(const UIHelpers::UIBounds& b, float w, float h) {
    return Rect{b.centerX() - w * 0.5f, b.centerY() - h * 0.5f, w, h};
}

void quad(Renderer& r, const Rect& rc, const glm::vec3& color) {
    glm::mat4 m(1.0f);
    m = glm::translate(m, glm::vec3(rc.cx(), rc.cy(), 0.0f));
    m = glm::scale(m, glm::vec3(rc.w, rc.h, 1.0f));
    r.drawColoredQuad(m, color);
}

void border(Renderer& r, const Rect& rc, const glm::vec3& color, float thickness = 1.0f) {
    quad(r, {rc.x, rc.y, rc.w, thickness}, color);
    quad(r, {rc.x, rc.y + rc.h - thickness, rc.w, thickness}, color);
    quad(r, {rc.x, rc.y, thickness, rc.h}, color);
    quad(r, {rc.x + rc.w - thickness, rc.y, thickness, rc.h}, color);
}

/// Vertical position that centres one text line in `rc`.
float textY(const Rect& rc, float scale) {
    return rc.y + (rc.h - kGlyphHeight * scale) * 0.5f;
}

void textCentred(Renderer& r, const std::string& text, const Rect& rc,
                 float scale, const glm::vec3& color) {
    if (text.empty()) return;
    float s = scale;
    const float width = r.getTextWidth(text, s);
    if (width > rc.w - 10.0f && width > 0.0f) s *= (rc.w - 10.0f) / width;
    r.drawTextCenteredColored(text, rc.cx(), textY(rc, s), s, color, rc.w - 8.0f);
}

void textRight(Renderer& r, const std::string& text, const Rect& rc,
               float scale, const glm::vec3& color) {
    if (text.empty()) return;
    float s = scale;
    const float width = r.getTextWidth(text, s);
    if (width > rc.w && width > 0.0f) s *= rc.w / width;
    r.drawTextColored(text, rc.x + rc.w - r.getTextWidth(text, s),
                      textY(rc, s), s, color);
}

void button(Renderer& r, const Rect& rc, const std::string& label,
            bool active, bool hovered, bool danger = false, bool enabled = true) {
    glm::vec3 fill = kRow;
    if (active) fill = danger ? kDanger : kAccent;
    else if (hovered && enabled) fill = kRowHover;
    if (!enabled) fill = kPanelInner;

    quad(r, rc, fill);
    border(r, rc, active ? glm::vec3(1.0f) : kEdge, 1.0f);
    glm::vec3 ink = active ? kInk : (enabled ? kLabel : kMuted);
    textCentred(r, label, rc, 0.40f, ink);
}

void sectionHeader(Renderer& r, const std::string& text, float x, float y,
                   float width, const glm::vec3& color) {
    r.drawTextColored(text, x, y, 0.34f, color);
    quad(r, {x, y + 17.0f, width, 1.0f}, kEdge);
}

/// 4x2 swatch colours of the eight stock materials (layer ids 0..7).
const glm::vec3 kTextureColors[8] = {
    {0.42f, 0.78f, 0.36f},  // grass
    {0.62f, 0.64f, 0.68f},  // stone
    {0.80f, 0.38f, 0.30f},  // brick
    {0.48f, 0.53f, 0.62f},  // metal
    {0.70f, 0.50f, 0.30f},  // wood
    {0.86f, 0.78f, 0.52f},  // sand
    {0.60f, 0.86f, 0.94f},  // ice
    {0.44f, 0.34f, 0.25f},  // dirt
};
const char* const kTextureNames[8] = {
    "GRASS", "STONE", "BRICK", "METAL", "WOOD", "SAND", "ICE", "DIRT"
};

// ── Layout ─────────────────────────────────────────────────────────────────

struct EditorLayout {
    Rect top, left, right, bottom;
    Rect newBtn, openBtn, saveBtn, helpBtn, exitBtn;
    Rect tool[5];
    Rect undoBtn, redoBtn;
    Rect sizeMinus[3], sizePlus[3];
    Rect yawMinus, yawPlus, pitchMinus, pitchPlus;
    Rect texture[8];
    Rect spawn[5];
    Rect gridBtn, spawnsBtn, snapBtn;
    Rect speedMinus, speedPlus;
};

EditorLayout computeLayout(GLFWwindow* window, bool spawnPanel) {
    const UIHelpers::UIBounds b = UIHelpers::getBounds(window);
    EditorLayout l;

    const float margin = 10.0f;
    const float gap = 8.0f;
    l.top = {b.left + margin, b.top + margin, b.width() - 2.0f * margin, 52.0f};
    l.bottom = {b.left + margin, b.bottom - margin - 48.0f,
                b.width() - 2.0f * margin, 48.0f};

    const float middleTop = l.top.y + l.top.h + gap;
    const float middleBottom = l.bottom.y - gap;
    l.left = {b.left + margin, middleTop, 152.0f, middleBottom - middleTop};
    l.right = {b.right - margin - 246.0f, middleTop, 246.0f,
               middleBottom - middleTop};

    // Top bar: action buttons are right-aligned.
    const float buttonH = 32.0f;
    const float buttonY = l.top.y + 10.0f;
    float cursor = l.top.x + l.top.w - 10.0f;
    auto rightButton = [&](float w) {
        cursor -= w;
        Rect r{cursor, buttonY, w, buttonH};
        cursor -= 6.0f;
        return r;
    };
    l.exitBtn = rightButton(56.0f);
    l.helpBtn = rightButton(56.0f);
    l.saveBtn = rightButton(62.0f);
    l.openBtn = rightButton(62.0f);
    l.newBtn  = rightButton(56.0f);

    // Left rail: tools.
    float y = l.left.y + 34.0f;
    const float toolX = l.left.x + 12.0f;
    const float toolW = l.left.w - 24.0f;
    for (int i = 0; i < 5; ++i) {
        l.tool[i] = {toolX, y, toolW, 36.0f};
        y += 42.0f;
    }

    // Undo / redo at the bottom of the rail.
    const float undoY = l.left.y + l.left.h - 40.0f;
    const float undoW = (toolW - 6.0f) * 0.5f;
    l.undoBtn = {toolX, undoY, undoW, 28.0f};
    l.redoBtn = {toolX + undoW + 6.0f, undoY, undoW, 28.0f};

    // Right inspector.
    const float rx = l.right.x + 12.0f;
    const float rw = l.right.w - 24.0f;
    float ry = l.right.y + 10.0f;

    if (spawnPanel) {
        ry += 22.0f;  // "SPAWN POINTS" header
        for (int i = 0; i < 5; ++i) {
            l.spawn[i] = {rx, ry, rw, 30.0f};
            ry += 36.0f;
        }
    } else {
        ry += 22.0f;  // "BRUSH" header
        for (int i = 0; i < 3; ++i) {
            l.sizeMinus[i] = {rx + rw - 56.0f, ry + 1.0f, 24.0f, 22.0f};
            l.sizePlus[i]  = {rx + rw - 28.0f, ry + 1.0f, 24.0f, 22.0f};
            ry += 28.0f;
        }
        l.yawMinus   = {rx + rw - 56.0f, ry + 1.0f, 24.0f, 22.0f};
        l.yawPlus    = {rx + rw - 28.0f, ry + 1.0f, 24.0f, 22.0f};
        ry += 28.0f;
        l.pitchMinus = {rx + rw - 56.0f, ry + 1.0f, 24.0f, 22.0f};
        l.pitchPlus  = {rx + rw - 28.0f, ry + 1.0f, 24.0f, 22.0f};
        ry += 28.0f;

        // Texture palette.
        ry += 22.0f;
        const float cellW = (rw - 18.0f) / 4.0f;
        for (int i = 0; i < 8; ++i) {
            const int col = i % 4;
            const int row = i / 4;
            l.texture[i] = {rx + col * (cellW + 6.0f),
                            ry + row * 40.0f, cellW, 34.0f};
        }
        ry += 2.0f * 40.0f + 12.0f;

        // View options.
        ry += 22.0f;
        const float bw = (rw - 12.0f) / 3.0f;
        l.gridBtn   = {rx, ry, bw, 26.0f};
        l.spawnsBtn = {rx + bw + 6.0f, ry, bw, 26.0f};
        l.snapBtn   = {rx + 2.0f * (bw + 6.0f), ry, bw, 26.0f};
        ry += 32.0f;
        l.speedMinus = {rx + rw - 54.0f, ry + 1.0f, 24.0f, 22.0f};
        l.speedPlus  = {rx + rw - 28.0f, ry + 1.0f, 24.0f, 22.0f};
    }

    return l;
}

/// Dialog layout kinds (kept as plain ints so the helpers stay free functions).
enum DialogKind { DIALOG_SAVE = 0, DIALOG_NEW = 1, DIALOG_OPEN = 2,
                  DIALOG_CONFIRM = 3, DIALOG_HELP = 4 };

struct DialogLayout {
    Rect card;
    Rect input;
    Rect primary, secondary, close;
    Rect options[10];
    Rect toggle;
    int optionCount = 0;
};

DialogLayout dialogLayout(const UIHelpers::UIBounds& b, int kind, int optionCount) {
    DialogLayout d;
    float w = 440.0f, h = 250.0f;
    switch (kind) {
        case DIALOG_NEW:     w = 480.0f; h = 372.0f; break;
        case DIALOG_OPEN: {
            d.optionCount = std::max(0, std::min(optionCount, 10));
            w = 480.0f;
            h = 172.0f + static_cast<float>(std::max(1, d.optionCount)) * 36.0f;
            break;
        }
        case DIALOG_CONFIRM: w = 480.0f; h = 226.0f; break;
        case DIALOG_HELP:    w = 620.0f; h = 474.0f; break;
        default:             w = 440.0f; h = 250.0f; break;
    }
    d.card = centred(b, w, h);
    const float pad = 30.0f;
    d.input = {d.card.x + pad, d.card.y + 82.0f, w - 2.0f * pad, 38.0f};

    if (kind == DIALOG_OPEN) {
        const float rowH = 30.0f;
        float y = d.card.y + 82.0f;
        for (int i = 0; i < d.optionCount; ++i) {
            d.options[i] = {d.card.x + pad, y, w - 2.0f * pad, rowH};
            y += rowH + 6.0f;
        }
        d.secondary = {d.card.x + w - pad - 130.0f,
                       d.card.y + h - 56.0f, 130.0f, 34.0f};
    } else if (kind == DIALOG_CONFIRM) {
        d.primary = {d.card.x + pad, d.card.y + h - 58.0f, 130.0f, 34.0f};
        d.secondary = {d.primary.x + 138.0f, d.primary.y, 130.0f, 34.0f};
        d.close = {d.secondary.x + 138.0f, d.primary.y, 120.0f, 34.0f};
    } else if (kind == DIALOG_HELP) {
        d.close = {d.card.cx() - 70.0f, d.card.y + h - 54.0f, 140.0f, 34.0f};
    } else {
        d.primary = {d.card.x + w - pad - 140.0f,
                     d.card.y + h - 56.0f, 140.0f, 34.0f};
        d.secondary = {d.primary.x - 152.0f, d.primary.y, 140.0f, 34.0f};
        if (kind == DIALOG_NEW) {
            const float bw = (w - 2.0f * pad - 16.0f) / 3.0f;
            for (int i = 0; i < 3; ++i) {
                d.options[i] = {d.card.x + pad + i * (bw + 8.0f),
                                d.card.y + 146.0f, bw, 34.0f};
            }
            d.toggle = {d.card.x + pad, d.card.y + 200.0f, 180.0f, 30.0f};
        }
    }
    return d;
}

// ── World helpers ──────────────────────────────────────────────────────────

/// Draw the 12 edges of an oriented box (the editor's ghost / selection).
void drawBoxEdges(Renderer& r, const glm::vec3& centre, const glm::vec3& size,
                  float yaw, float pitch, const glm::vec3& color,
                  float thickness = 0.035f) {
    glm::mat4 rot(1.0f);
    rot = glm::rotate(rot, glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
    rot = glm::rotate(rot, glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::vec3 half = size * 0.5f;
    const float xLen = std::max(thickness, size.x);
    const float yLen = std::max(thickness, size.y);
    const float zLen = std::max(thickness, size.z);

    auto edge = [&](const glm::vec3& localOffset, const glm::vec3& edgeSize) {
        glm::mat4 m(1.0f);
        m = glm::translate(m, centre);
        m = m * rot;
        m = glm::translate(m, localOffset);
        m = glm::scale(m, edgeSize);
        r.drawCube(m, color);
    };

    for (float sx = -1.0f; sx <= 1.0f; sx += 2.0f) {
        for (float sz = -1.0f; sz <= 1.0f; sz += 2.0f) {
            edge(glm::vec3(sx * half.x, 0.0f, sz * half.z),
                 glm::vec3(thickness, yLen, thickness));
        }
    }
    for (float sy = -1.0f; sy <= 1.0f; sy += 2.0f) {
        for (float sz = -1.0f; sz <= 1.0f; sz += 2.0f) {
            edge(glm::vec3(0.0f, sy * half.y, sz * half.z),
                 glm::vec3(xLen, thickness, thickness));
        }
        for (float sx = -1.0f; sx <= 1.0f; sx += 2.0f) {
            edge(glm::vec3(sx * half.x, sy * half.y, 0.0f),
                 glm::vec3(thickness, thickness, zLen));
        }
    }
}

struct AimResult {
    bool hit = false;
    int index = -1;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 place{0.0f};
};

/// Ray cast against every block's cached AABB (a few thousand blocks at most).
AimResult castAim(Map* map, const glm::vec3& origin, const glm::vec3& dir,
                  float maxDistance, float grid, float freeDistance,
                  const glm::vec3& brush, bool snap) {
    AimResult best;
    float bestT = maxDistance;

    if (map) {
        const auto& blocks = map->getBlocks();
        for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
            const MapBlock& block = blocks[i];
            glm::vec3 lo = block.boundsMin_;
            glm::vec3 hi = block.boundsMax_;
            // spawn markers are tiny; ignore degenerate boxes
            if (hi.x < lo.x || hi.y < lo.y || hi.z < lo.z) continue;

            float tMin = 0.0f;
            float tMax = bestT;
            int axis = -1;
            float sign = 0.0f;
            bool miss = false;

            for (int a = 0; a < 3; ++a) {
                if (std::abs(dir[a]) < 1.0e-6f) {
                    if (origin[a] < lo[a] || origin[a] > hi[a]) { miss = true; break; }
                    continue;
                }
                float t1 = (lo[a] - origin[a]) / dir[a];
                float t2 = (hi[a] - origin[a]) / dir[a];
                float s = -1.0f;
                if (t1 > t2) { std::swap(t1, t2); s = 1.0f; }
                if (t1 > tMin) { tMin = t1; axis = a; sign = s; }
                if (t2 < tMax) tMax = t2;
                if (tMin > tMax) { miss = true; break; }
            }
            if (miss || axis < 0 || tMin <= 0.001f || tMin >= bestT) continue;

            bestT = tMin;
            best.hit = true;
            best.index = i;
            best.point = origin + dir * tMin;
            best.normal = glm::vec3(0.0f);
            best.normal[axis] = sign;

            const glm::vec3 half = block.scale * 0.5f;
            glm::vec3 place = best.point;
            if (snap && grid > 0.001f) {
                // Keep the brush on the world lattice tangentially, but place
                // its face exactly against the surface that was hit.
                place = glm::round(place / grid) * grid;
            }
            place[axis] = block.position[axis] +
                          sign * (half[axis] + brush[axis] * 0.5f);
            best.place = place;
        }
    }

    if (!best.hit) {
        best.point = origin + dir * freeDistance;
        best.place = best.point;
        if (snap && grid > 0.001f) {
            best.place = glm::round(best.place / grid) * grid;
        }
    }
    return best;
}

/// World ray through a canvas-space cursor position.
glm::vec3 rayThroughCursor(float mx, float my, const UIHelpers::UIBounds& b,
                           const glm::mat4& view, const glm::mat4& projection) {
    const float ndcX = ((mx - b.left) / b.width()) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ((my - b.top) / b.height()) * 2.0f;
    const glm::mat4 inv = glm::inverse(projection * view);
    glm::vec4 nearPoint = inv * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPoint  = inv * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    if (std::abs(nearPoint.w) < 1e-6f || std::abs(farPoint.w) < 1e-6f) {
        return glm::normalize(glm::vec3(0.0f, 0.0f, -1.0f));
    }
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    return glm::normalize(glm::vec3(farPoint - nearPoint));
}

/// Strip directories / unsafe characters from a map name.
std::string sanitizeStem(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool lastUnderscore = false;
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) {
            out.push_back(static_cast<char>(std::tolower(u)));
            lastUnderscore = false;
        } else if (!lastUnderscore && !out.empty()) {
            out.push_back('_');
            lastUnderscore = true;
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.size() > 32) out.resize(32);
    if (out.empty()) out = "untitled";
    return out;
}

std::string trimName(const std::string& text) {
    size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos) return std::string();
    size_t last = text.find_last_not_of(' ');
    return text.substr(first, last - first + 1);
}

std::string displayMapName(const std::string& file) {
    std::string stem = std::filesystem::path(file).stem().string();
    std::replace(stem.begin(), stem.end(), '_', ' ');
    for (char& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return stem;
}

} // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================

void Game::enterEditor() {
    if (!window) return;

    inp.resetNavKeys();
    menuJustEntered = true;
    gameState = MAP_EDITOR;

    // A map load queued from the menu must not replace the map under the editor.
    mapLoader_.cancel();

    editorTool = EditorTool::Build;
    editorModal = EditorModal::None;
    editorPending = EditorPending::None;
    editorSpawnMode = SPAWN_NONE;
    editorDirty = false;
    editorStrokeActive_ = false;
    editorUiCapture_ = false;
    editorLookActive_ = false;
    editorAimValid_ = false;
    editorAimHit_ = false;
    editorAimBlock_ = -1;
    editorUndo_.clear();
    editorRedo_.clear();
    editorShowGrid = true;
    editorShowSpawns = true;
    editorSnapToGrid = true;
    editorBlockSize = glm::vec3(2.0f, 2.0f, 2.0f);
    editorSelectedTexture = 1;
    editorBlockRotation = 0.0f;
    editorBlockPitch = 0.0f;
    editorBlockOffset = 6.0f;
    editorGridSize = 1.0f;
    editorSpeed = 15.0f;

    if (currentMap) {
        const std::string stored = currentMap->getMapName();
        editorMapName = stored.empty() ? "UNTITLED"
                                       : displayMapName(stored);
        editorMapName.erase(std::remove(editorMapName.begin(), editorMapName.end(), '\n'),
                            editorMapName.end());
        if (!stored.empty()) {
            editorMapName = stored;
            for (char& c : editorMapName) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        editorFileName = activeMapFile_.empty()
                             ? sanitizeStem(editorMapName)
                             : sanitizeStem(std::filesystem::path(activeMapFile_).stem().string());
    } else {
        editorMapName = "UNTITLED";
        editorFileName = "untitled";
    }
    editorNameInput = editorMapName;
    editorNewName = "NEW MAP";
    editorNewSize = 1;
    editorNewStarter = true;

    // Put the camera above one edge of the map, looking at its centre.
    glm::vec3 dims = currentMap ? currentMap->getDimensions() : glm::vec3(32.0f, 16.0f, 32.0f);
    const float span = std::max(dims.x, dims.z);
    camera.Position = glm::vec3(0.0f, std::max(5.0f, dims.y * 0.6f + 4.0f),
                                std::max(10.0f, span * 0.45f));
    camera.Yaw = -90.0f;
    camera.Pitch = -22.0f;
    camera.updateCameraVectors();
    editorGridPos = camera.Position + camera.Front * editorBlockOffset;

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }
    firstMouse = true;

    editorShowToast("EDITOR READY - F1 FOR CONTROLS", 3.5);
}

void Game::leaveEditor() {
    editorLookActive_ = false;
    editorModal = EditorModal::None;
    editorPending = EditorPending::None;
    inp.resetNavKeys();
    menuJustEntered = true;
    openMainMenu();
    gameState = MENU;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

// ===========================================================================
// Undo / redo and map replacement
// ===========================================================================

Game::EditorSnapshot Game::editorCaptureState() const {
    EditorSnapshot s;
    if (!currentMap) return s;
    s.blocks = currentMap->getBlocks();
    s.spawnPoints = currentMap->getSpawnPoints();
    s.teamSpawns = currentMap->getTeamSpawns();
    s.bombSites = currentMap->getBombSites();
    s.dimensions = currentMap->getDimensions();
    return s;
}

void Game::editorRestoreState(const EditorSnapshot& s) {
    if (!currentMap) return;
    currentMap->setBlocks(s.blocks);
    currentMap->setSpawnPoints(s.spawnPoints);
    currentMap->setTeamSpawns(s.teamSpawns);
    currentMap->setBombSites(s.bombSites);
    currentMap->setDimensions(s.dimensions);
    editorDirty = true;
    editorRebuildWorld();
}

void Game::editorPushUndo() {
    if (!currentMap) return;
    if (editorUndo_.size() >= kEditorUndoLimit) {
        editorUndo_.erase(editorUndo_.begin());
    }
    editorUndo_.push_back(editorCaptureState());
    editorRedo_.clear();
}

void Game::editorUndo() {
    if (!currentMap) return;
    if (editorUndo_.empty()) {
        editorShowToast("NOTHING TO UNDO");
        return;
    }
    editorRedo_.push_back(editorCaptureState());
    const EditorSnapshot snapshot = editorUndo_.back();
    editorUndo_.pop_back();
    editorRestoreState(snapshot);
    editorShowToast("UNDO");
}

void Game::editorRedo() {
    if (!currentMap) return;
    if (editorRedo_.empty()) {
        editorShowToast("NOTHING TO REDO");
        return;
    }
    editorUndo_.push_back(editorCaptureState());
    const EditorSnapshot snapshot = editorRedo_.back();
    editorRedo_.pop_back();
    editorRestoreState(snapshot);
    editorShowToast("REDO");
}

void Game::editorRebuildWorld() {
    if (currentMap) renderer.buildWorld(currentMap);
}

void Game::editorShowToast(const std::string& text, double seconds) {
    editorToast = text;
    editorToastUntil = glfwGetTime() + seconds;
}

void Game::editorRequestNew() {
    if (!currentMap) {
        editorModal = EditorModal::NewMap;
        editorNameInput = "NEW MAP";
        editorNewName = "NEW MAP";
        return;
    }
    if (editorDirty) {
        editorPending = EditorPending::NewMap;
        editorModal = EditorModal::Confirm;
    } else {
        editorModal = EditorModal::NewMap;
        editorNameInput = "NEW MAP";
        editorNewName = "NEW MAP";
        editorNewSize = 1;
        editorNewStarter = true;
    }
}

void Game::editorRequestOpen() {
    if (editorDirty) {
        editorPending = EditorPending::OpenMap;
        editorModal = EditorModal::Confirm;
    } else {
        editorOpenSelection = std::max(0, std::min(editorOpenSelection,
                                                  static_cast<int>(mapFiles.size()) - 1));
        editorModal = EditorModal::OpenMap;
    }
}

void Game::editorRequestExit() {
    if (editorDirty) {
        editorPending = EditorPending::Exit;
        editorModal = EditorModal::Confirm;
    } else {
        leaveEditor();
    }
}

void Game::editorResolvePending(bool discard) {
    const EditorPending pending = editorPending;
    editorPending = EditorPending::None;
    editorModal = EditorModal::None;
    if (!discard) return;

    switch (pending) {
        case EditorPending::NewMap:
            editorModal = EditorModal::NewMap;
            editorNameInput = "NEW MAP";
            editorNewName = "NEW MAP";
            editorNewSize = 1;
            editorNewStarter = true;
            break;
        case EditorPending::OpenMap:
            editorOpenSelection = std::max(0, std::min(editorOpenSelection,
                                                      static_cast<int>(mapFiles.size()) - 1));
            editorModal = EditorModal::OpenMap;
            break;
        case EditorPending::Exit:
            leaveEditor();
            break;
        default:
            break;
    }
}

void Game::editorCreateNewMap() {
    if (window) {
        // Snapshot the current map first: NEW is undoable like any other edit.
        editorPushUndo();

        const float sizes[3] = {32.0f, 48.0f, 64.0f};
        const int sizeIndex = std::max(0, std::min(editorNewSize, 2));
        const float span = sizes[sizeIndex];
        const float height = std::max(18.0f, span * 0.45f);

        std::string name = trimName(editorNewName);
        if (name.empty()) name = "NEW MAP";

        delete currentMap;
        currentMap = new Map("");
        currentMap->clear();  // remove the generated test map (and its textures)
        MapLoader::ensureTextures(*currentMap);
        currentMap->setMapName(sanitizeStem(name));
        currentMap->setDimensions(glm::vec3(span, height, span));

        const float h = 4.0f;  // wall height / 2
        auto add = [&](const glm::vec3& pos, const glm::vec3& size,
                       int texture, const glm::vec3& tint) {
            currentMap->addBlock(MapBlock(pos, size, texture, 1, true, tint));
        };

        if (editorNewStarter) {
            // Floor (top at y = 0), four walls and a little cover.
            add(glm::vec3(0.0f, -1.0f, 0.0f),
                glm::vec3(span, 2.0f, span), 0, glm::vec3(1.0f));
            add(glm::vec3(0.0f, h, span * 0.5f),
                glm::vec3(span + 2.0f, h * 2.0f, 2.0f), 1, glm::vec3(0.72f, 0.74f, 0.78f));
            add(glm::vec3(0.0f, h, -span * 0.5f),
                glm::vec3(span + 2.0f, h * 2.0f, 2.0f), 1, glm::vec3(0.72f, 0.74f, 0.78f));
            add(glm::vec3(span * 0.5f, h, 0.0f),
                glm::vec3(2.0f, h * 2.0f, span + 2.0f), 1, glm::vec3(0.72f, 0.74f, 0.78f));
            add(glm::vec3(-span * 0.5f, h, 0.0f),
                glm::vec3(2.0f, h * 2.0f, span + 2.0f), 1, glm::vec3(0.72f, 0.74f, 0.78f));

            add(glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec3(8.0f, 2.0f, 8.0f), 2, glm::vec3(1.0f));
            add(glm::vec3(span * 0.22f, 0.75f, -span * 0.18f),
                glm::vec3(3.0f, 1.5f, 3.0f), 4, glm::vec3(1.0f));
            add(glm::vec3(-span * 0.22f, 0.75f, span * 0.18f),
                glm::vec3(3.0f, 1.5f, 3.0f), 4, glm::vec3(1.0f));

            // Spawn points and sites: the result is playable immediately.
            const float spread = span * 0.32f;
            currentMap->addSpawnPoint(SpawnPoint(glm::vec3(-spread, 0.05f, -spread), -45.0f, -10.0f));
            currentMap->addSpawnPoint(SpawnPoint(glm::vec3( spread, 0.05f,  spread), 135.0f, -10.0f));
            currentMap->addSpawnPoint(SpawnPoint(glm::vec3( spread, 0.05f, -spread), -135.0f, -10.0f));
            currentMap->addSpawnPoint(SpawnPoint(glm::vec3(-spread, 0.05f,  spread),  45.0f, -10.0f));
            currentMap->addTeamSpawn(TeamSpawn(glm::vec3(-span * 0.38f, 0.05f, 0.0f), 0, -90.0f, 0.0f));
            currentMap->addTeamSpawn(TeamSpawn(glm::vec3( span * 0.38f, 0.05f, 0.0f), 1,  90.0f, 0.0f));
            currentMap->addBombSite(BombSiteData(glm::vec3( span * 0.28f, 0.05f,  span * 0.28f), 'A'));
            currentMap->addBombSite(BombSiteData(glm::vec3(-span * 0.28f, 0.05f, -span * 0.28f), 'B'));
        }

        editorMapName = name;
        editorFileName = sanitizeStem(name);
        editorDirty = true;

        camera.Position = glm::vec3(0.0f, std::max(8.0f, height * 0.7f), span * 0.52f);
        camera.Yaw = -90.0f;
        camera.Pitch = -25.0f;
        camera.updateCameraVectors();

        renderer.refreshTextures(currentMap);
        editorRebuildWorld();
        editorModal = EditorModal::None;
        editorShowToast("NEW MAP CREATED - SAVE IT TO KEEP IT", 3.0);
    }
}

bool Game::editorOpenMapFile(const std::string& file) {
    if (file.empty()) return false;
    // Remember the current state first so OPEN is undoable.
    editorPushUndo();

    std::unique_ptr<Map> loaded = MapLoader::loadBlocking(file);
    if (!loaded) {
        editorShowToast("COULD NOT LOAD " + file, 3.0);
        return false;
    }

    delete currentMap;
    currentMap = loaded.release();
    activeMapFile_ = file;

    std::string display = currentMap->getMapName();
    if (display.empty()) display = displayMapName(file);
    editorMapName = display;
    for (char& c : editorMapName) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    editorFileName = sanitizeStem(std::filesystem::path(file).stem().string());
    editorNameInput = editorMapName;
    editorDirty = false;

    glm::vec3 dims = currentMap->getDimensions();
    const float span = std::max(dims.x, dims.z);
    camera.Position = glm::vec3(0.0f, std::max(5.0f, dims.y * 0.6f + 4.0f),
                                std::max(10.0f, span * 0.45f));
    camera.Yaw = -90.0f;
    camera.Pitch = -22.0f;
    camera.updateCameraVectors();

    renderer.refreshTextures(currentMap);
    editorRebuildWorld();
    editorModal = EditorModal::None;
    editorShowToast("OPENED " + file, 2.5);
    return true;
}

// ===========================================================================
// Input
// ===========================================================================

void Game::syncEditorKeyEdges() {
    std::memcpy(editorKeyPrev_, editorKeyCur_, sizeof(editorKeyCur_));
    if (!window) {
        std::memset(editorKeyCur_, 0, sizeof(editorKeyCur_));
        return;
    }
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
        editorKeyCur_[key] = glfwGetKey(window, key) == GLFW_PRESS;
    }
}

bool Game::editorKeyPressed(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return editorKeyCur_[key] && !editorKeyPrev_[key];
}

bool Game::editorKeyDown(int key) const {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return editorKeyCur_[key];
}

void Game::editorApplyTool() {
    if (!currentMap || !editorAimValid_) return;

    auto blockAtAim = [&]() -> const MapBlock* {
        if (editorAimBlock_ < 0) return nullptr;
        const auto& blocks = currentMap->getBlocks();
        if (editorAimBlock_ >= static_cast<int>(blocks.size())) return nullptr;
        return &blocks[editorAimBlock_];
    };

    switch (editorTool) {
        case EditorTool::Build: {
            const glm::vec3 half = editorBlockSize * 0.5f;
            const glm::vec3 blockMin = editorGridPos - half;
            const glm::vec3 blockMax = editorGridPos + half;
            glm::vec3 dims = currentMap->getDimensions();
            float w = dims.x, hgt = dims.y, d = dims.z;
            if (blockMin.x < -dims.x * 0.5f || blockMax.x > dims.x * 0.5f)
                w = std::min(1000.0f, std::max(dims.x, std::abs(blockMax.x) * 2.0f + 2.0f));
            if (blockMin.y < -dims.y * 0.5f || blockMax.y > dims.y * 0.5f)
                hgt = std::min(1000.0f, std::max(dims.y, std::abs(blockMax.y) * 2.0f + 2.0f));
            if (blockMin.z < -dims.z * 0.5f || blockMax.z > dims.z * 0.5f)
                d = std::min(1000.0f, std::max(dims.z, std::abs(blockMax.z) * 2.0f + 2.0f));
            if (w != dims.x || hgt != dims.y || d != dims.z)
                currentMap->setDimensions(glm::vec3(w, hgt, d));

            currentMap->deleteBlockExact(editorGridPos, editorBlockSize,
                                         editorBlockRotation, editorBlockPitch, 0.05f);
            MapBlock block(editorGridPos, editorBlockSize, editorSelectedTexture,
                           1, true, glm::vec3(1.0f),
                           editorBlockRotation, editorBlockPitch);
            currentMap->addBlock(block);
            editorDirty = true;
            editorRebuildWorld();
            break;
        }
        case EditorTool::Paint: {
            const MapBlock* b = blockAtAim();
            if (!b) return;
            if (b->textureId != editorSelectedTexture) {
                currentMap->replaceTextureAt(b->position, editorSelectedTexture);
                editorDirty = true;
                editorRebuildWorld();
            }
            break;
        }
        case EditorTool::Erase: {
            bool removed = false;
            const glm::vec3 point = editorAimPoint_;

            // Markers are small and sit on top of the geometry, so they are
            // erased first: aiming at a spawn must not delete the floor below.
            float bestDist = 0.85f;
            int bestType = -1, bestIndex = -1;
            const auto& dm = currentMap->getSpawnPoints();
            for (int i = 0; i < static_cast<int>(dm.size()); ++i) {
                const float d = glm::length(dm[i].position - point);
                if (d < bestDist) { bestDist = d; bestType = 0; bestIndex = i; }
            }
            const auto& teams = currentMap->getTeamSpawns();
            for (int i = 0; i < static_cast<int>(teams.size()); ++i) {
                const float d = glm::length(teams[i].position - point);
                if (d < bestDist) { bestDist = d; bestType = 1; bestIndex = i; }
            }
            const auto& sites = currentMap->getBombSites();
            for (int i = 0; i < static_cast<int>(sites.size()); ++i) {
                const float d = glm::length(sites[i].position - point);
                if (d < bestDist) { bestDist = d; bestType = 2; bestIndex = i; }
            }
            if (bestType == 0) { currentMap->removeSpawnPoint(bestIndex); removed = true; }
            else if (bestType == 1) { currentMap->removeTeamSpawn(bestIndex); removed = true; }
            else if (bestType == 2) { currentMap->removeBombSite(bestIndex); removed = true; }

            if (!removed) {
                const MapBlock* b = blockAtAim();
                if (b) {
                    removed = currentMap->deleteBlockExact(b->position, b->scale,
                                                           b->rotation, b->pitch, 0.05f);
                    if (!removed) removed = currentMap->deleteBlockAt(editorAimPoint_, 0.25f);
                }
            }
            if (removed) {
                editorDirty = true;
                editorRebuildWorld();
            }
            break;
        }
        case EditorTool::Pick: {
            const MapBlock* b = blockAtAim();
            if (!b) return;
            editorBlockSize = b->scale;
            editorBlockRotation = b->rotation;
            editorBlockPitch = b->pitch;
            editorSelectedTexture = std::max(0, std::min(7, b->textureId));
            editorShowToast("PICKED BLOCK - TEXTURE " +
                            std::string(kTextureNames[editorSelectedTexture]));
            break;
        }
        case EditorTool::Spawn: {
            const glm::vec3 position = editorAimHit_
                                           ? editorAimPoint_ + editorAimNormal_ * 0.3f
                                           : editorGridPos;
            auto closeTo = [&](const glm::vec3& other, float radius) {
                return glm::length(other - position) < radius;
            };
            switch (editorSpawnMode) {
                case SPAWN_DEATHMATCH: {
                    for (const auto& s : currentMap->getSpawnPoints()) {
                        if (closeTo(s.position, 0.5f)) return;
                    }
                    currentMap->addSpawnPoint(SpawnPoint(position, camera.Yaw, camera.Pitch));
                    break;
                }
                case SPAWN_TEAM_A:
                case SPAWN_TEAM_B: {
                    const int team = editorSpawnMode == SPAWN_TEAM_A ? 0 : 1;
                    for (const auto& s : currentMap->getTeamSpawns()) {
                        if (s.teamId == team && closeTo(s.position, 0.5f)) return;
                    }
                    currentMap->addTeamSpawn(TeamSpawn(position, team, camera.Yaw, camera.Pitch));
                    break;
                }
                case SPAWN_BOMB_SITE_A:
                case SPAWN_BOMB_SITE_B: {
                    const char label = editorSpawnMode == SPAWN_BOMB_SITE_A ? 'A' : 'B';
                    for (const auto& s : currentMap->getBombSites()) {
                        if (s.label == label && closeTo(s.position, 1.0f)) return;
                    }
                    currentMap->addBombSite(BombSiteData(position, label));
                    break;
                }
                default:
                    for (const auto& s : currentMap->getSpawnPoints()) {
                        if (closeTo(s.position, 0.5f)) return;
                    }
                    currentMap->addSpawnPoint(SpawnPoint(position, camera.Yaw, camera.Pitch));
                    break;
            }
            editorDirty = true;
            break;
        }
    }
}

void Game::processEditorInput(float deltaTime) {
    if (!window) return;
    syncEditorKeyEdges();
    editorBlink += deltaTime;

    const UIHelpers::UIBounds b = UIHelpers::getBounds(window);
    const EditorLayout layout = computeLayout(window, editorTool == EditorTool::Spawn);
    const float mx = mouseGUIPosX;
    const float my = mouseGUIPosY;

    const bool lmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool rmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool ctrl = editorKeyDown(GLFW_KEY_LEFT_CONTROL) || editorKeyDown(GLFW_KEY_RIGHT_CONTROL);
    const bool shift = editorKeyDown(GLFW_KEY_LEFT_SHIFT) || editorKeyDown(GLFW_KEY_RIGHT_SHIFT);

    const bool overTop = layout.top.contains(mx, my);
    const bool overLeft = layout.left.contains(mx, my);
    const bool overRight = layout.right.contains(mx, my);
    const bool overBottom = layout.bottom.contains(mx, my);
    const bool overUi = overTop || overLeft || overRight || overBottom;

    // ── Modal dialogs own all input ───────────────────────────────────────
    if (editorModal != EditorModal::None) {
        editorAimValid_ = false;
        if (editorLookActive_) {
            editorLookActive_ = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        inp.rmbPressed = rmbDown;

        auto close = [&]() {
            editorModal = EditorModal::None;
            editorPending = EditorPending::None;
            editorShowToast("CANCELLED");
        };
        auto confirmSave = [&]() {
            std::string name = trimName(editorNameInput);
            if (name.empty()) name = "UNTITLED";
            if (saveEditorMap(name)) editorModal = EditorModal::None;
        };
        auto confirmNew = [&]() {
            editorNewName = trimName(editorNameInput);
            if (editorNewName.empty()) editorNewName = "NEW MAP";
            editorCreateNewMap();
        };

        // ESC closes the dialog; ENTER confirms it.
        if (editorKeyPressed(GLFW_KEY_ESCAPE)) {
            close();
            return;
        }

        if (editorModal == EditorModal::Help) {
            const DialogLayout d = dialogLayout(b, DIALOG_HELP, 0);
            if (editorKeyPressed(GLFW_KEY_F1) || editorKeyPressed(GLFW_KEY_ENTER) ||
                (mouseClicked && (d.close.contains(mx, my) || !d.card.contains(mx, my)))) {
                editorModal = EditorModal::None;
            }
            return;
        }

        if (editorModal == EditorModal::OpenMap) {
            const int count = static_cast<int>(mapFiles.size());
            const DialogLayout d = dialogLayout(b, DIALOG_OPEN, count);
            if (editorKeyPressed(GLFW_KEY_DOWN) && count > 0)
                editorOpenSelection = (editorOpenSelection + 1) % count;
            if (editorKeyPressed(GLFW_KEY_UP) && count > 0)
                editorOpenSelection = (editorOpenSelection + count - 1) % count;
            if (editorKeyPressed(GLFW_KEY_ENTER) && count > 0) {
                editorOpenMapFile(mapFiles[editorOpenSelection]);
                return;
            }
            if (mouseClicked) {
                const int visible = std::min(count, 10);
                const int first = std::max(0, std::min(editorOpenSelection - visible / 2,
                                                       count - visible));
                for (int i = 0; i < visible; ++i) {
                    if (d.options[i].contains(mx, my)) {
                        editorOpenSelection = first + i;
                        editorOpenMapFile(mapFiles[editorOpenSelection]);
                        return;
                    }
                }
                if (d.secondary.contains(mx, my) || !d.card.contains(mx, my)) close();
            }
            return;
        }

        if (editorModal == EditorModal::Confirm) {
            const DialogLayout d = dialogLayout(b, DIALOG_CONFIRM, 0);
            if (editorKeyPressed(GLFW_KEY_ENTER)) {
                // ENTER keeps the work: save it, then continue.
                if (saveEditorMap(editorMapName)) {
                    if (editorPending == EditorPending::Exit) leaveEditor();
                    else editorResolvePending(true);
                }
                return;
            }
            if (mouseClicked) {
                if (d.primary.contains(mx, my)) {            // SAVE FIRST
                    if (saveEditorMap(editorMapName)) {
                        if (editorPending == EditorPending::Exit) leaveEditor();
                        else editorResolvePending(true);
                    }
                    return;
                }
                if (d.secondary.contains(mx, my)) {          // DISCARD
                    editorResolvePending(true);
                    return;
                }
                if (d.close.contains(mx, my) || !d.card.contains(mx, my)) close();
            }
            return;
        }

        // Save / New: text entry.
        std::string& target = editorNameInput;
        for (int c = 0; c < 26; ++c) {
            if (editorKeyPressed(GLFW_KEY_A + c) && target.size() < 24)
                target.push_back(static_cast<char>('A' + c));
        }
        for (int c = 0; c < 10; ++c) {
            if (editorKeyPressed(GLFW_KEY_0 + c) && target.size() < 24)
                target.push_back(static_cast<char>('0' + c));
        }
        if (editorKeyPressed(GLFW_KEY_SPACE) && target.size() < 24) target.push_back(' ');
        if (editorKeyPressed(GLFW_KEY_MINUS) && target.size() < 24) target.push_back('-');
        if (editorKeyPressed(GLFW_KEY_BACKSPACE) && !target.empty()) target.pop_back();

        if (editorKeyPressed(GLFW_KEY_ENTER)) {
            if (editorModal == EditorModal::Save) confirmSave();
            else confirmNew();
            return;
        }

        if (mouseClicked) {
            if (editorModal == EditorModal::Save) {
                const DialogLayout d = dialogLayout(b, DIALOG_SAVE, 0);
                if (d.primary.contains(mx, my)) confirmSave();
                else if (d.secondary.contains(mx, my) || !d.card.contains(mx, my)) close();
            } else {
                const DialogLayout d = dialogLayout(b, DIALOG_NEW, 0);
                for (int i = 0; i < 3; ++i) {
                    if (d.options[i].contains(mx, my)) editorNewSize = i;
                }
                if (d.toggle.contains(mx, my)) editorNewStarter = !editorNewStarter;
                if (d.primary.contains(mx, my)) confirmNew();
                else if (d.secondary.contains(mx, my) || !d.card.contains(mx, my)) close();
            }
        }
        return;
    }

    // ── Plane-wide shortcuts ──────────────────────────────────────────────
    if (editorKeyPressed(GLFW_KEY_ESCAPE)) {
        editorRequestExit();
        return;
    }
    if (editorKeyPressed(GLFW_KEY_F1)) {
        editorModal = EditorModal::Help;
        editorAimValid_ = false;
        return;
    }
    if (ctrl && editorKeyPressed(GLFW_KEY_S)) {
        if (saveEditorMap(editorMapName)) editorShowToast("SAVED " + editorMapName);
        return;
    }
    if (ctrl && editorKeyPressed(GLFW_KEY_N)) { editorRequestNew(); return; }
    if (ctrl && editorKeyPressed(GLFW_KEY_O)) { editorRequestOpen(); return; }
    if (ctrl && editorKeyPressed(GLFW_KEY_Z)) {
        if (shift) editorRedo(); else editorUndo();
        return;
    }
    if (ctrl && editorKeyPressed(GLFW_KEY_Y)) { editorRedo(); return; }

    if (editorKeyPressed(GLFW_KEY_B)) editorTool = EditorTool::Build;
    if (editorKeyPressed(GLFW_KEY_P)) editorTool = EditorTool::Paint;
    if (editorKeyPressed(GLFW_KEY_X)) editorTool = EditorTool::Erase;
    if (editorKeyPressed(GLFW_KEY_I)) editorTool = EditorTool::Pick;
    if (editorKeyPressed(GLFW_KEY_M)) {
        editorTool = EditorTool::Spawn;
        if (editorSpawnMode == SPAWN_NONE) editorSpawnMode = SPAWN_DEATHMATCH;
    }
    for (int i = 0; i < 8; ++i) {
        if (editorKeyPressed(GLFW_KEY_1 + i)) editorSelectedTexture = i;
    }
    if (editorKeyPressed(GLFW_KEY_LEFT_BRACKET)) {
        editorBlockSize = glm::max(editorBlockSize - glm::vec3(0.5f), glm::vec3(0.5f));
    }
    if (editorKeyPressed(GLFW_KEY_RIGHT_BRACKET)) {
        editorBlockSize = glm::min(editorBlockSize + glm::vec3(0.5f), glm::vec3(32.0f));
    }
    if (editorKeyPressed(GLFW_KEY_R)) {
        editorBlockRotation = 0.0f;
        editorBlockPitch = 0.0f;
    }

    // ── Camera (free fly, like the rest of the editor tools) ──────────────
    glm::vec3 move(0.0f);
    if (editorKeyDown(GLFW_KEY_W)) move += camera.Front;
    if (editorKeyDown(GLFW_KEY_S) && !ctrl) move -= camera.Front;
    if (editorKeyDown(GLFW_KEY_A)) move -= camera.Right;
    if (editorKeyDown(GLFW_KEY_D)) move += camera.Right;
    if (editorKeyDown(GLFW_KEY_E) || editorKeyDown(GLFW_KEY_SPACE)) move.y += 1.0f;
    if (editorKeyDown(GLFW_KEY_Q)) move.y -= 1.0f;
    if (glm::length(move) > 0.0f) {
        move = glm::normalize(move);
        camera.Position += move * editorSpeed * (shift ? 3.0f : 1.0f) * deltaTime;
    }

    // Wheel: distance of the free placement; CTRL cycles the texture.
    while (pendingEditorScroll_ != 0) {
        const int dir = pendingEditorScroll_ > 0 ? 1 : -1;
        pendingEditorScroll_ -= dir;
        if (ctrl) {
            editorSelectedTexture = (editorSelectedTexture + dir + 8) % 8;
        } else {
            editorBlockOffset = glm::clamp(editorBlockOffset + 0.5f * dir, 1.0f, 50.0f);
        }
    }
    if (editorKeyPressed(GLFW_KEY_UP)) {
        editorBlockOffset = glm::clamp(editorBlockOffset + editorGridSize, 1.0f, 50.0f);
    }
    if (editorKeyPressed(GLFW_KEY_DOWN)) {
        editorBlockOffset = glm::clamp(editorBlockOffset - editorGridSize, 1.0f, 50.0f);
    }

    // ── Aim ray from the cursor ───────────────────────────────────────────
    int fbW = 800, fbH = 600;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = 800;
    if (fbH <= 0) fbH = 600;
    const glm::mat4 view = camera.GetViewMatrix();
    const glm::mat4 projection = glm::perspective(
        glm::radians(camera.Zoom), static_cast<float>(fbW) / static_cast<float>(fbH),
        0.1f, 1000.0f);

    const bool cursorInWorld = !overUi && !editorLookActive_ && mx >= b.left &&
                               mx <= b.right && my >= b.top && my <= b.bottom;
    if (cursorInWorld) {
        const glm::vec3 dir = rayThroughCursor(mx, my, b, view, projection);
        const AimResult aim = castAim(currentMap, camera.Position, dir, 120.0f,
                                      editorGridSize, editorBlockOffset,
                                      editorBlockSize, editorSnapToGrid);
        editorAimValid_ = true;
        editorAimHit_ = aim.hit;
        editorAimBlock_ = aim.index;
        editorAimPoint_ = aim.point;
        editorAimNormal_ = aim.normal;
        editorGridPos = aim.place;
    } else {
        editorAimValid_ = false;
    }

    // ── Panel buttons ─────────────────────────────────────────────────────
    auto clickSize = [&](int axis, float delta) {
        editorBlockSize[axis] = glm::clamp(editorBlockSize[axis] + delta, 0.5f, 32.0f);
    };
    auto adjustRotation = [&](float deltaYaw, float deltaPitch) {
        editorBlockRotation = std::fmod(editorBlockRotation + deltaYaw + 360.0f, 360.0f);
        editorBlockPitch = glm::clamp(editorBlockPitch + deltaPitch, -90.0f, 90.0f);
    };

    if (mouseClicked) {
        if (overUi) {
            editorUiCapture_ = true;
            bool handled = true;

            if (layout.top.contains(mx, my)) {
                if (layout.newBtn.contains(mx, my)) editorRequestNew();
                else if (layout.openBtn.contains(mx, my)) editorRequestOpen();
                else if (layout.saveBtn.contains(mx, my)) {
                    editorModal = EditorModal::Save;
                    editorNameInput = editorMapName;
                } else if (layout.helpBtn.contains(mx, my)) {
                    editorModal = EditorModal::Help;
                } else if (layout.exitBtn.contains(mx, my)) {
                    editorRequestExit();
                } else handled = false;
            } else if (layout.left.contains(mx, my)) {
                bool hit = false;
                for (int i = 0; i < 5; ++i) {
                    if (!layout.tool[i].contains(mx, my)) continue;
                    hit = true;
                    switch (i) {
                        case 0: editorTool = EditorTool::Build; break;
                        case 1: editorTool = EditorTool::Paint; break;
                        case 2: editorTool = EditorTool::Erase; break;
                        case 3: editorTool = EditorTool::Pick; break;
                        default:
                            editorTool = EditorTool::Spawn;
                            if (editorSpawnMode == SPAWN_NONE)
                                editorSpawnMode = SPAWN_DEATHMATCH;
                            break;
                    }
                }
                if (layout.undoBtn.contains(mx, my)) { editorUndo(); hit = true; }
                if (layout.redoBtn.contains(mx, my)) { editorRedo(); hit = true; }
                handled = hit;
            } else if (layout.right.contains(mx, my)) {
                bool hit = false;
                if (editorTool == EditorTool::Spawn) {
                    static const EditorSpawnMode modes[5] = {
                        SPAWN_DEATHMATCH, SPAWN_TEAM_A, SPAWN_TEAM_B,
                        SPAWN_BOMB_SITE_A, SPAWN_BOMB_SITE_B
                    };
                    for (int i = 0; i < 5; ++i) {
                        if (layout.spawn[i].contains(mx, my)) {
                            editorSpawnMode = modes[i];
                            hit = true;
                        }
                    }
                } else {
                    for (int i = 0; i < 3; ++i) {
                        if (layout.sizeMinus[i].contains(mx, my)) { clickSize(i, -0.5f); hit = true; }
                        if (layout.sizePlus[i].contains(mx, my))  { clickSize(i,  0.5f); hit = true; }
                    }
                    if (layout.yawMinus.contains(mx, my))   { adjustRotation(-15.0f, 0.0f); hit = true; }
                    if (layout.yawPlus.contains(mx, my))    { adjustRotation( 15.0f, 0.0f); hit = true; }
                    if (layout.pitchMinus.contains(mx, my)) { adjustRotation(0.0f, -15.0f); hit = true; }
                    if (layout.pitchPlus.contains(mx, my))  { adjustRotation(0.0f,  15.0f); hit = true; }
                    for (int i = 0; i < 8; ++i) {
                        if (layout.texture[i].contains(mx, my)) {
                            editorSelectedTexture = i;
                            hit = true;
                        }
                    }
                    if (layout.gridBtn.contains(mx, my))   { editorShowGrid = !editorShowGrid; hit = true; }
                    if (layout.spawnsBtn.contains(mx, my)) { editorShowSpawns = !editorShowSpawns; hit = true; }
                    if (layout.snapBtn.contains(mx, my))   { editorSnapToGrid = !editorSnapToGrid; hit = true; }
                    if (layout.speedMinus.contains(mx, my)) {
                        editorSpeed = glm::clamp(editorSpeed - 5.0f, 5.0f, 80.0f); hit = true;
                    }
                    if (layout.speedPlus.contains(mx, my)) {
                        editorSpeed = glm::clamp(editorSpeed + 5.0f, 5.0f, 80.0f); hit = true;
                    }
                }
                handled = hit;
            } else {
                handled = false;
            }

            if (handled) return;
        } else {
            // Left click in the 3D view: start a brush stroke.
            editorUiCapture_ = false;
            editorStrokeActive_ = true;
            if (editorTool != EditorTool::Pick) editorPushUndo();
            editorLastBrushTime_ = 0.0;
            editorApplyTool();
        }
    }

    if (!lmbDown) {
        editorUiCapture_ = false;
        editorStrokeActive_ = false;
    }

    // Held left button paints repeatedly, throttled like a pencil.
    if (lmbDown && !editorUiCapture_ && cursorInWorld) {
        const double now = glfwGetTime();
        const double interval = (editorTool == EditorTool::Erase) ? 0.12 : 0.16;
        if (now - editorLastBrushTime_ >= interval) {
            editorLastBrushTime_ = now;
            if (!editorStrokeActive_) {
                editorStrokeActive_ = true;
                if (editorTool != EditorTool::Pick) editorPushUndo();
            }
            editorApplyTool();
        }
    }

    // ── Right mouse drags the camera ──────────────────────────────────────
    if (rmbDown) {
        if (!inp.rmbPressed) {
            inp.rmbPressed = true;
            if (!overUi && !overBottom) {
                editorLookActive_ = true;
                firstMouse = true;
                // Capture the pointer while looking so the camera does not stop
                // when the cursor reaches the window edge.
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
        }
    } else if (inp.rmbPressed || editorLookActive_) {
        inp.rmbPressed = false;
        if (editorLookActive_) {
            editorLookActive_ = false;
            firstMouse = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }
}

// ===========================================================================
// Saving
// ===========================================================================

bool Game::saveEditorMap(const std::string& name) {
    if (!currentMap) return false;
    std::string display = trimName(name);
    if (display.empty()) display = "UNTITLED";
    const std::string stem = sanitizeStem(display);
    const std::string file = stem + ".map";

    if (!saveMap(file, display)) return false;
    editorMapName = display;
    editorFileName = stem;
    editorNameInput = display;
    currentMap->setMapName(stem);
    editorDirty = false;
    activeMapFile_ = file;
    scanMapsFolder();
    editorShowToast("SAVED ASSETS/MAPS/" + stem + ".MAP", 3.0);
    return true;
}

bool Game::saveMap(const std::string& filename, const std::string& mapName) {
    if (!currentMap) {
        std::cerr << "No map to save!" << std::endl;
        return false;
    }

    // Accept both "name" and "name.map", never a path outside assets/maps.
    std::string stem = std::filesystem::path(filename).stem().string();
    stem = sanitizeStem(stem);
    const std::string filepath = "assets/maps/" + stem + ".map";
    std::ofstream file(filepath);

    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << filepath << std::endl;
        editorShowToast("WRITE FAILED: " + filepath, 3.0);
        return false;
    }

    const glm::vec3 dims = currentMap->getDimensions();
    const std::string token = sanitizeStem(mapName);

    file << "# Map created in the WEXYRON editor\n";
    file << "name " << token << "\n";
    file << "dimensions " << dims.x << " " << dims.y << " " << dims.z << "\n\n";

    const auto& blocks = currentMap->getBlocks();
    for (const auto& block : blocks) {
        file << "block "
             << block.position.x << " "
             << block.position.y << " "
             << block.position.z << " "
             << block.scale.x    << " "
             << block.scale.y    << " "
             << block.scale.z    << " "
             << block.textureId  << " "
             << block.blockType  << " "
             << (block.solid ? "1" : "0") << " "
             << block.color.r    << " "
             << block.color.g    << " "
             << block.color.b    << " "
             << block.rotation   << " "
             << block.pitch      << "\n";
    }

    const auto& spawns = currentMap->getSpawnPoints();
    if (!spawns.empty()) {
        file << "\n# Deathmatch spawn points\n";
        for (const auto& spawn : spawns) {
            file << "spawn " << spawn.position.x << " " << spawn.position.y << " "
                 << spawn.position.z << " " << spawn.yaw << " " << spawn.pitch << "\n";
        }
    }

    const auto& teamSpawns = currentMap->getTeamSpawns();
    if (!teamSpawns.empty()) {
        file << "\n# Team spawn points\n";
        for (const auto& spawn : teamSpawns) {
            file << "team_spawn " << spawn.position.x << " " << spawn.position.y << " "
                 << spawn.position.z << " " << spawn.teamId << " "
                 << spawn.yaw << " " << spawn.pitch << "\n";
        }
    }

    const auto& bombSites = currentMap->getBombSites();
    if (!bombSites.empty()) {
        file << "\n# Bomb sites (for Bomb Defusal mode)\n";
        for (const auto& site : bombSites) {
            file << "bomb_site " << site.position.x << " " << site.position.y << " "
                 << site.position.z << " " << site.label << "\n";
        }
    }

    file.close();
    std::cout << "Saved map: " << filepath << std::endl;
    return true;
}

// ===========================================================================
// Rendering
// ===========================================================================

void Game::renderEditor() {
    int fbW = 800, fbH = 600;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = 800;
    if (fbH <= 0) fbH = 600;
    renderer.setViewport(0, 0, fbW, fbH);

    const UIHelpers::UIBounds b = UIHelpers::getBounds(window);
    const glm::mat4 view = camera.GetViewMatrix();
    const glm::mat4 projection = glm::perspective(
        glm::radians(camera.Zoom), static_cast<float>(fbW) / static_cast<float>(fbH),
        0.1f, 1000.0f);
    renderer.setCamera(view, projection);

    // ── World ─────────────────────────────────────────────────────────────
    if (currentMap) {
        renderer.renderMap(currentMap);
        if (editorShowSpawns) renderer.renderSpawnPoints(currentMap, true);
    }

    if (editorShowGrid) {
        const float cell = std::max(0.5f, editorGridSize);
        float radius = 32.0f;
        if (currentMap) {
            const glm::vec3 dims = currentMap->getDimensions();
            radius = std::max(dims.x, dims.z) * 0.5f;
        }
        const int lines = std::min(80, static_cast<int>(radius / cell) + 1);
        const float extent = radius + 4.0f;
        for (int i = -lines; i <= lines; ++i) {
            const bool major = (i % 8) == 0;
            const glm::vec3 color = major ? glm::vec3(0.42f, 0.46f, 0.52f)
                                          : glm::vec3(0.24f, 0.27f, 0.32f);
            glm::mat4 lineX(1.0f);
            lineX = glm::translate(lineX, glm::vec3(i * cell, 0.015f, 0.0f));
            lineX = glm::scale(lineX, glm::vec3(major ? 0.04f : 0.02f, 0.01f,
                                                extent * 2.0f));
            renderer.drawCube(lineX, color);

            glm::mat4 lineZ(1.0f);
            lineZ = glm::translate(lineZ, glm::vec3(0.0f, 0.015f, i * cell));
            lineZ = glm::scale(lineZ, glm::vec3(extent * 2.0f, 0.01f,
                                                major ? 0.04f : 0.02f));
            renderer.drawCube(lineZ, color);
        }
    }

    // Aim preview: green ghost for the brush, highlighted target for the
    // paint / erase / pick tools, and a marker for spawn placement.
    if (editorAimValid_ && currentMap) {
        const auto& blocks = currentMap->getBlocks();
        const MapBlock* aimed = nullptr;
        if (editorAimBlock_ >= 0 && editorAimBlock_ < static_cast<int>(blocks.size()))
            aimed = &blocks[editorAimBlock_];

        if (editorTool == EditorTool::Build) {
            const glm::vec3 color = editorAimHit_ ? kBuild : glm::vec3(0.35f, 0.85f, 1.0f);
            drawBoxEdges(renderer, editorGridPos, editorBlockSize,
                         editorBlockRotation, editorBlockPitch, color);
        } else if (editorTool == EditorTool::Erase) {
            // Spawn markers are erased before geometry: show the same target.
            glm::vec3 marker{0.0f};
            float bestDist = 0.85f;
            bool hasMarker = false;
            for (const auto& s : currentMap->getSpawnPoints()) {
                const float d = glm::length(s.position - editorAimPoint_);
                if (d < bestDist) { bestDist = d; marker = s.position; hasMarker = true; }
            }
            for (const auto& s : currentMap->getTeamSpawns()) {
                const float d = glm::length(s.position - editorAimPoint_);
                if (d < bestDist) { bestDist = d; marker = s.position; hasMarker = true; }
            }
            for (const auto& s : currentMap->getBombSites()) {
                const float d = glm::length(s.position - editorAimPoint_);
                if (d < bestDist) { bestDist = d; marker = s.position; hasMarker = true; }
            }
            if (hasMarker) {
                drawBoxEdges(renderer, marker + glm::vec3(0.0f, 0.35f, 0.0f),
                             glm::vec3(1.0f, 0.9f, 1.0f), 0.0f, 0.0f, kDanger, 0.05f);
            } else if (aimed) {
                drawBoxEdges(renderer, aimed->position, aimed->scale,
                             aimed->rotation, aimed->pitch, kDanger, 0.045f);
            }
        } else if ((editorTool == EditorTool::Paint || editorTool == EditorTool::Pick) && aimed) {
            const glm::vec3 color = editorTool == EditorTool::Paint ? kPaint : kPick;
            drawBoxEdges(renderer, aimed->position, aimed->scale,
                         aimed->rotation, aimed->pitch, color, 0.045f);
        } else if (editorTool == EditorTool::Spawn) {
            glm::vec3 color = glm::vec3(1.0f, 1.0f, 0.0f);
            switch (editorSpawnMode) {
                case SPAWN_TEAM_A:      color = glm::vec3(1.0f, 0.25f, 0.25f); break;
                case SPAWN_TEAM_B:      color = glm::vec3(0.35f, 0.55f, 1.0f); break;
                case SPAWN_BOMB_SITE_A: color = glm::vec3(1.0f, 0.85f, 0.0f); break;
                case SPAWN_BOMB_SITE_B: color = glm::vec3(1.0f, 0.5f, 0.0f); break;
                default: break;
            }
            const glm::vec3 position = editorAimHit_
                                           ? editorAimPoint_ + editorAimNormal_ * 0.3f
                                           : editorGridPos;
            drawBoxEdges(renderer, position, glm::vec3(0.8f), 0.0f, 0.0f, color, 0.05f);
        }
    }

    // ── Panels ────────────────────────────────────────────────────────────
    UIHelpers::setProjection(renderer, window);
    const EditorLayout layout = computeLayout(window, editorTool == EditorTool::Spawn);
    const float mx = mouseGUIPosX;
    const float my = mouseGUIPosY;
    auto hover = [&](const Rect& r) { return r.contains(mx, my); };

    quad(renderer, layout.top, kPanel);
    border(renderer, layout.top, kEdge);
    quad(renderer, layout.left, kPanel);
    border(renderer, layout.left, kEdge);
    quad(renderer, layout.right, kPanel);
    border(renderer, layout.right, kEdge);
    quad(renderer, layout.bottom, kPanel);
    border(renderer, layout.bottom, kEdge);

    // Top bar.
    renderer.drawTextColored("MAP EDITOR", layout.top.x + 14.0f,
                             layout.top.y + 4.0f, 0.52f, kAccent);
    std::string mapLine = "MAP: " + editorMapName;
    if (editorDirty) mapLine += "  *UNSAVED";
    {
        const float maxW = std::max(80.0f, layout.newBtn.x - layout.top.x - 22.0f);
        float s = 0.30f;
        const float need = renderer.getTextWidth(mapLine, s);
        if (need > maxW && need > 0.0f) s *= maxW / need;
        renderer.drawTextColored(mapLine, layout.top.x + 14.0f,
                                 layout.top.y + 29.0f, s,
                                 editorDirty ? kAccentSoft : kMuted);
    }

    button(renderer, layout.newBtn, "NEW", false, hover(layout.newBtn));
    button(renderer, layout.openBtn, "OPEN", false, hover(layout.openBtn));
    button(renderer, layout.saveBtn, "SAVE", false, hover(layout.saveBtn));
    button(renderer, layout.helpBtn, "HELP", false, hover(layout.helpBtn));
    button(renderer, layout.exitBtn, "EXIT", false, hover(layout.exitBtn), true);

    // Left rail.
    sectionHeader(renderer, "TOOLS", layout.left.x + 12.0f, layout.left.y + 10.0f,
                  layout.left.w - 24.0f, kAccentSoft);
    static const char* const kToolLabels[5] = {
        "BUILD  [B]", "PAINT  [P]", "ERASE  [X]", "PICK   [I]", "SPAWN  [M]"
    };
    for (int i = 0; i < 5; ++i) {
        const bool active = static_cast<int>(editorTool) == i;
        button(renderer, layout.tool[i], kToolLabels[i], active, hover(layout.tool[i]));
    }
    button(renderer, layout.undoBtn, "UNDO",
           false, hover(layout.undoBtn), false, !editorUndo_.empty());
    button(renderer, layout.redoBtn, "REDO",
           false, hover(layout.redoBtn), false, !editorRedo_.empty());
    renderer.drawTextColored("CTRL+Z / CTRL+Y", layout.left.x + 12.0f,
                             layout.undoBtn.y - 18.0f, 0.26f, kMuted);

    // Right inspector.
    const float rx = layout.right.x + 12.0f;
    const float rw = layout.right.w - 24.0f;
    if (editorTool == EditorTool::Spawn) {
        sectionHeader(renderer, "SPAWN POINTS", rx, layout.right.y + 10.0f, rw, kAccentSoft);
        static const char* const kSpawnLabels[5] = {
            "DEATHMATCH", "TEAM A (RED)", "TEAM B (BLUE)",
            "BOMB SITE A", "BOMB SITE B"
        };
        static const EditorSpawnMode kModes[5] = {
            SPAWN_DEATHMATCH, SPAWN_TEAM_A, SPAWN_TEAM_B,
            SPAWN_BOMB_SITE_A, SPAWN_BOMB_SITE_B
        };
        for (int i = 0; i < 5; ++i) {
            button(renderer, layout.spawn[i], kSpawnLabels[i],
                   editorSpawnMode == kModes[i], hover(layout.spawn[i]));
        }
        const float hintY = layout.spawn[4].y + layout.spawn[4].h + 18.0f;
        renderer.drawTextFit("CLICK IN THE WORLD TO PLACE", rx, hintY, rw, 0.32f);
        renderer.drawTextFit("ERASE TOOL REMOVES MARKERS", rx, hintY + 20.0f, rw, 0.32f);
    } else {
        float y = layout.right.y + 10.0f;
        sectionHeader(renderer, "BRUSH", rx, y, rw, kAccentSoft);
        y += 22.0f;

        char value[32];
        static const char* const kAxis[3] = {"SIZE X", "SIZE Y", "SIZE Z"};
        for (int i = 0; i < 3; ++i) {
            const Rect row{rx, y, rw, 24.0f};
            renderer.drawTextColored(kAxis[i], rx, textY(row, 0.34f), 0.34f, kLabel);
            std::snprintf(value, sizeof(value), "%.1f", editorBlockSize[i]);
            textRight(renderer, value, {rx + 90.0f, y, 72.0f, 24.0f}, 0.34f, kAccent);
            button(renderer, layout.sizeMinus[i], "-", false, hover(layout.sizeMinus[i]));
            button(renderer, layout.sizePlus[i], "+", false, hover(layout.sizePlus[i]));
            y += 28.0f;
        }

        auto rotationRow = [&](const char* label, float value, const Rect& minus,
                               const Rect& plus, const char* suffix) {
            const Rect row{rx, y, rw, 24.0f};
            renderer.drawTextColored(label, rx, textY(row, 0.34f), 0.34f, kLabel);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f%s", value, suffix);
            textRight(renderer, buf, {rx + 90.0f, y, 72.0f, 24.0f}, 0.34f, kAccent);
            button(renderer, minus, "-", false, hover(minus));
            button(renderer, plus, "+", false, hover(plus));
            y += 28.0f;
        };
        rotationRow("YAW", editorBlockRotation, layout.yawMinus, layout.yawPlus, " DEG");
        rotationRow("PITCH", editorBlockPitch, layout.pitchMinus, layout.pitchPlus, " DEG");

        sectionHeader(renderer, "TEXTURE", rx, y, rw, kAccentSoft);
        y += 22.0f;
        for (int i = 0; i < 8; ++i) {
            const Rect& cell = layout.texture[i];
            quad(renderer, cell, kTextureColors[i]);
            border(renderer, cell, i == editorSelectedTexture ? kAccent : kEdge,
                   i == editorSelectedTexture ? 2.0f : 1.0f);
            char index[8];
            std::snprintf(index, sizeof(index), "%d", i + 1);
            const float luminance = 0.299f * kTextureColors[i].r +
                                    0.587f * kTextureColors[i].g +
                                    0.114f * kTextureColors[i].b;
            const glm::vec3 swatchInk = luminance > 0.55f
                                            ? glm::vec3(0.05f, 0.05f, 0.05f)
                                            : glm::vec3(0.95f, 0.95f, 0.95f);
            renderer.drawTextColored(index, cell.x + 4.0f, cell.y + 3.0f, 0.26f,
                                     swatchInk);
            renderer.drawTextColored(kTextureNames[i], cell.x + 4.0f,
                                     cell.y + cell.h - 9.0f, 0.20f, swatchInk);
            if (hover(cell)) border(renderer, cell, glm::vec3(1.0f), 1.0f);
        }
        y += 2.0f * 40.0f + 12.0f;

        sectionHeader(renderer, "VIEW", rx, y, rw, kAccentSoft);
        y += 22.0f;
        button(renderer, layout.gridBtn, "GRID", editorShowGrid, hover(layout.gridBtn));
        button(renderer, layout.spawnsBtn, "SPAWNS", editorShowSpawns, hover(layout.spawnsBtn));
        button(renderer, layout.snapBtn, "SNAP", editorSnapToGrid, hover(layout.snapBtn));
        y += 32.0f;
        const Rect speedRow{rx, y, rw, 24.0f};
        renderer.drawTextColored("SPEED", rx, textY(speedRow, 0.34f), 0.34f, kLabel);
        std::snprintf(value, sizeof(value), "%.0f", editorSpeed);
        textRight(renderer, value, {rx + 90.0f, y, 72.0f, 24.0f}, 0.34f, kAccent);
        button(renderer, layout.speedMinus, "-", false, hover(layout.speedMinus));
        button(renderer, layout.speedPlus, "+", false, hover(layout.speedPlus));
    }

    // Map statistics, anchored to the bottom of the inspector.
    if (currentMap) {
        char stats[128];
        std::snprintf(stats, sizeof(stats), "BLOCKS %d",
                      static_cast<int>(currentMap->getBlocks().size()));
        renderer.drawTextFit(stats, rx, layout.right.y + layout.right.h - 58.0f,
                             rw, 0.30f);
        std::snprintf(stats, sizeof(stats), "DM %d   TEAM %d",
                      static_cast<int>(currentMap->getSpawnPoints().size()),
                      static_cast<int>(currentMap->getTeamSpawns().size()));
        renderer.drawTextFit(stats, rx, layout.right.y + layout.right.h - 40.0f,
                             rw, 0.30f);
        std::snprintf(stats, sizeof(stats), "BOMB SITES %d",
                      static_cast<int>(currentMap->getBombSites().size()));
        renderer.drawTextFit(stats, rx, layout.right.y + layout.right.h - 22.0f,
                             rw, 0.30f);
    }

    // Bottom bar: every control, no hidden binder.
    {
        const float textW = layout.bottom.w - 24.0f;
        renderer.drawTextFit(
            "LMB TOOL   RMB LOOK   WASD MOVE   Q/E UP-DOWN   SHIFT FAST",
            layout.bottom.x + 12.0f, layout.bottom.y + 6.0f, textW, 0.29f);
        renderer.drawTextFit(
            "1-8 TEX   B P X I M TOOLS   CTRL+Z/Y UNDO   CTRL+S SAVE   F1 HELP",
            layout.bottom.x + 12.0f, layout.bottom.y + 25.0f, textW, 0.29f);
    }

    // Transient message just above the bottom bar, inside the world column so
    // it never covers the inspector statistics.
    if (!editorToast.empty() && glfwGetTime() < editorToastUntil) {
        const float worldX0 = layout.left.x + layout.left.w;
        const float worldX1 = layout.right.x;
        const float worldW = std::max(160.0f, worldX1 - worldX0);
        const float toastW = std::min(worldW - 20.0f,
                                      renderer.getTextWidth(editorToast, 0.34f) + 36.0f);
        const Rect toast{(worldX0 + worldX1) * 0.5f - toastW * 0.5f,
                         layout.bottom.y - 34.0f, toastW, 28.0f};
        quad(renderer, toast, kPanelInner);
        border(renderer, toast, kAccent, 1.0f);
        textCentred(renderer, editorToast, toast, 0.34f, kAccent);
    }

    // ── Dialog dialogs ────────────────────────────────────────────────────
    if (editorModal == EditorModal::None) return;

    // Focus screen: an opaque scrim keeps the panels behind the dialog from
    // showing through around its edges (UI is painted in submission order).
    quad(renderer, {b.left, b.top, b.width(), b.height()},
         glm::vec3(0.035f, 0.045f, 0.06f));

    const bool openDialog = editorModal == EditorModal::OpenMap;
    const int kind = editorModal == EditorModal::Save ? DIALOG_SAVE
                   : editorModal == EditorModal::NewMap ? DIALOG_NEW
                   : openDialog ? DIALOG_OPEN
                   : editorModal == EditorModal::Confirm ? DIALOG_CONFIRM
                   : DIALOG_HELP;
    const int optionCount = openDialog ? static_cast<int>(mapFiles.size()) : 0;
    const DialogLayout d = dialogLayout(b, kind, optionCount);

    quad(renderer, d.card, kPanelInner);
    border(renderer, d.card, kEdge, 1.0f);
    quad(renderer, {d.card.x, d.card.y, d.card.w, 3.0f}, kAccent);

    if (editorModal == EditorModal::Save) {
        renderer.drawTextColored("SAVE MAP", d.card.x + 30.0f, d.card.y + 24.0f,
                                 0.62f, kAccent);
        renderer.drawTextColored("NAME", d.card.x + 30.0f, d.card.y + 62.0f,
                                 0.30f, kMuted);
        quad(renderer, d.input, kPanel);
        border(renderer, d.input, kAccent, 1.0f);
        const std::string shown = editorNameInput +
            ((std::fmod(editorBlink, 1.0) < 0.5) ? "_" : "");
        renderer.drawTextFit(shown, d.input.x + 10.0f, textY(d.input, 0.42f),
                             d.input.w - 20.0f, 0.42f);
        const std::string stem = sanitizeStem(editorNameInput);
        const bool exists = std::filesystem::exists("assets/maps/" + stem + ".map");
        const std::string path = "FILE: ASSETS/MAPS/" + stem + ".MAP";
        renderer.drawTextFit(path, d.card.x + 30.0f, d.card.y + 132.0f,
                             d.card.w - 60.0f, 0.30f);
        if (exists) {
            renderer.drawTextColored("OVERWRITES THE EXISTING FILE",
                                     d.card.x + 30.0f, d.card.y + 154.0f,
                                     0.28f, kDanger);
        }
        button(renderer, d.secondary, "CANCEL", false, hover(d.secondary));
        button(renderer, d.primary, "SAVE", false, hover(d.primary));
    } else if (editorModal == EditorModal::NewMap) {
        renderer.drawTextColored("NEW MAP", d.card.x + 30.0f, d.card.y + 24.0f,
                                 0.62f, kAccent);
        renderer.drawTextColored("NAME", d.card.x + 30.0f, d.card.y + 62.0f,
                                 0.30f, kMuted);
        quad(renderer, d.input, kPanel);
        border(renderer, d.input, kAccent, 1.0f);
        const std::string shown = editorNameInput +
            ((std::fmod(editorBlink, 1.0) < 0.5) ? "_" : "");
        renderer.drawTextFit(shown, d.input.x + 10.0f, textY(d.input, 0.42f),
                             d.input.w - 20.0f, 0.42f);

        renderer.drawTextColored("SIZE", d.card.x + 30.0f, d.card.y + 126.0f,
                                 0.30f, kMuted);
        static const char* const kSizes[3] = {"32", "48", "64"};
        for (int i = 0; i < 3; ++i) {
            button(renderer, d.options[i], kSizes[i], editorNewSize == i,
                   hover(d.options[i]));
        }
        button(renderer, d.toggle, "STARTER ROOM", editorNewStarter, hover(d.toggle));
        renderer.drawTextFit("FLOOR, WALLS, SPAWNS AND BOMB SITES",
                             d.card.x + 30.0f, d.card.y + 238.0f,
                             d.card.w - 60.0f, 0.28f);
        button(renderer, d.secondary, "CANCEL", false, hover(d.secondary));
        button(renderer, d.primary, "CREATE", false, hover(d.primary));
    } else if (openDialog) {
        renderer.drawTextColored("OPEN MAP", d.card.x + 30.0f, d.card.y + 24.0f,
                                 0.62f, kAccent);
        const int count = static_cast<int>(mapFiles.size());
        if (count == 0) {
            renderer.drawTextColored("NO MAP FILES FOUND", d.card.x + 30.0f,
                                     d.card.y + 84.0f, 0.34f, kMuted);
        }
        const int visible = std::min(count, 10);
        const int first = std::max(0, std::min(editorOpenSelection - visible / 2,
                                               count - visible));
        for (int i = 0; i < visible; ++i) {
            const int idx = first + i;
            const std::string label = displayMapName(mapFiles[idx]);
            button(renderer, d.options[i], label, idx == editorOpenSelection,
                   hover(d.options[i]));
        }
        button(renderer, d.secondary, "CANCEL", false, hover(d.secondary));
    } else if (editorModal == EditorModal::Confirm) {
        renderer.drawTextColored("UNSAVED CHANGES", d.card.x + 30.0f,
                                 d.card.y + 30.0f, 0.58f, kAccent);
        std::string line = "DISCARD YOUR CHANGES?";
        if (editorPending == EditorPending::Exit) line = "LEAVE WITHOUT SAVING?";
        if (editorPending == EditorPending::OpenMap) line = "OPEN ANOTHER MAP AND LOSE CHANGES?";
        if (editorPending == EditorPending::NewMap) line = "CREATE A NEW MAP AND LOSE CHANGES?";
        renderer.drawTextFit(line, d.card.x + 30.0f, d.card.y + 92.0f,
                             d.card.w - 60.0f, 0.36f);
        renderer.drawTextFit("SAVE FIRST IF YOU WANT TO KEEP THEM.",
                             d.card.x + 30.0f, d.card.y + 118.0f,
                             d.card.w - 60.0f, 0.30f);
        button(renderer, d.primary, "SAVE FIRST", false, hover(d.primary));
        button(renderer, d.secondary, "DISCARD", true, hover(d.secondary), true);
        button(renderer, d.close, "CANCEL", false, hover(d.close));
    } else if (editorModal == EditorModal::Help) {
        renderer.drawTextColored("CONTROLS", d.card.x + 30.0f, d.card.y + 24.0f,
                                 0.62f, kAccent);
        static const char* const kHelp[] = {
            "WASD          MOVE CAMERA",
            "Q / E         DOWN / UP",
            "SHIFT         FAST CAMERA",
            "RMB DRAG      LOOK AROUND",
            "LMB           USE THE ACTIVE TOOL",
            "WHEEL         PLACEMENT DISTANCE",
            "CTRL + WHEEL  CYCLE TEXTURE",
            "",
            "B / P / X / I / M   BUILD / PAINT / ERASE / PICK / SPAWN",
            "1 - 8         SELECT TEXTURE",
            "[ / ]         BRUSH SIZE",
            "R             RESET ROTATION",
            "",
            "CTRL + Z / Y  UNDO / REDO",
            "CTRL + S      SAVE MAP",
            "CTRL + N / O  NEW / OPEN MAP",
            "F1            THIS HELP",
            "ESC           LEAVE THE EDITOR"
        };
        float hy = d.card.y + 86.0f;
        for (const char* line : kHelp) {
            if (line[0] != '\0') {
                renderer.drawTextFit(line, d.card.x + 34.0f, hy,
                                     d.card.w - 68.0f, 0.32f);
            }
            hy += 19.0f;
        }
        button(renderer, d.close, "CLOSE", false, hover(d.close));
    }
}
