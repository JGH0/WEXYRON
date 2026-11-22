/**
 * @file menu_draw.cpp
 * @brief Menu framework: layout, scrolling, drawing and input for MenuView
 *
 * The visual language matches the main menu (see game_menus.cpp): a dark shade
 * band with the screen title, a gold line, a dark card holding grouped rows,
 * a gold selection with a white marker, key caps for bindings, a help line and
 * a centred controls legend at the bottom.
 *
 * Row layout: rows are stacked from the top of the card downwards. A page that
 * is taller than the screen scrolls: the selection is kept inside the window
 * of visible lines and the view eases towards it (moving down past the last
 * visible row pushes the view by one row, like a console list).
 */

#include "menu.hpp"
#include "engine/renderer.hpp"
#include "../core/settings.hpp"
#include "../ui/ui_helpers.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace menu {

namespace {

// Layout constants in canvas units (the canvas is 800x600 plus whatever extra
// space a wide window adds, exactly like UIHelpers::getBounds).
constexpr float kSideMargin    = 64.0f;
constexpr float kHeaderBandH   = 100.0f;  ///< shade band behind the title
constexpr float kAccentY       = 99.0f;
constexpr float kCardTop       = 112.0f;
// Footer: help line (boxed) on top, controls legend at the very bottom.
constexpr float kHelpY         = 524.0f;  ///< top of the help box
constexpr float kFooterLineH   = 20.0f;
constexpr float kLegendY       = 570.0f;  ///< top of the legend text
/// Space the rows may use: from under the title rule to above the help box.
constexpr float kRowsBottom    = kHelpY - 6.0f;
constexpr float kMinCardWidth  = 560.0f;
constexpr float kMaxCardWidth  = 760.0f;

/// Font metrics of Renderer::drawText: 8px per glyph pixel, 5 cells tall.
constexpr float kGlyphPixelSize = 8.0f;
constexpr float kGlyphHeight    = 5.0f * kGlyphPixelSize;

void quad(Renderer& r, float x0, float y0, float x1, float y1, const glm::vec3& color) {
    glm::mat4 m(1.0f);
    m = glm::translate(m, glm::vec3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0.0f));
    m = glm::scale(m, glm::vec3(x1 - x0, y1 - y0, 1.0f));
    r.drawColoredQuad(m, color);
}

/// Text anchored at the top-left, shrunk to fit `maxWidth` (never enlarged).
void textFit(Renderer& r, const std::string& text, float x, float y,
             float maxWidth, float scale, const glm::vec3& color) {
    if (text.empty() || maxWidth <= 0.0f) return;
    const float needed = r.getTextWidth(text, scale);
    if (needed > maxWidth) scale *= maxWidth / needed;
    r.drawTextColored(text, x, y, scale, color);
}

/// Text anchored at the top-right corner.
void textRight(Renderer& r, const std::string& text, float right, float y,
               float scale, const glm::vec3& color, float maxWidth) {
    if (text.empty()) return;
    float s = scale;
    const float needed = r.getTextWidth(text, s);
    if (maxWidth > 0.0f && needed > maxWidth) s *= maxWidth / needed;
    r.drawTextColored(text, right - r.getTextWidth(text, s), y, s, color);
}

/// Top of a text line so that it is vertically centred in [top, bottom].
float centredTextY(float top, float bottom, float scale) {
    return top + ((bottom - top) - kGlyphHeight * scale) * 0.5f;
}

/// Height of a laid-out line (header or row).
float lineHeight(const LaidRow& line, const Metrics& m) {
    return line.header ? m.headerHeight : m.rowHeight;
}

/// Row index inside `page` for a line (-1 for headers).
int rowIndexOf(const Page& page, const LaidRow& line) {
    if (!line.row) return -1;
    return static_cast<int>(line.row - page.rows.data());
}

/**
 * @brief The slice of laid-out lines that is drawn for a page.
 *
 * The list scrolls like a console: when it is taller than the screen, the
 * window keeps `visible` lines. Selecting below the window scrolls down one
 * row, selecting above scrolls up one row. When the list fits, the window is
 * the whole list, so the card is exactly as tall as its content.
 */
struct ScrollWindow {
    int first = 0;   ///< first line drawn
    int count = 0;   ///< number of lines drawn
};

/// Height of `n` lines starting at `first`.
float heightOfLines(const std::vector<LaidRow>& lines, const Metrics& m, int first, int n) {
    float total = 0.0f;
    const int end = std::min(first + n, static_cast<int>(lines.size()));
    for (int i = first; i < end; ++i) {
        total += lineHeight(lines[i], m);
        if (i < end - 1) total += m.rowGap;
    }
    return total;
}

ScrollWindow computeWindow(const std::vector<LaidRow>& lines, const Page* page_of_lines,
                           const Metrics& m, int selected, int target, int& outVisible) {
    ScrollWindow w;
    const int count = static_cast<int>(lines.size());
    const float limit = kRowsBottom - kCardTop;
    outVisible = 0;
    if (count == 0) return w;

    // Line of the selected row (needs the page to compare pointers).
    int selLine = 0;
    if (const Page* page = page_of_lines) {
        for (int i = 0; i < count; ++i) {
            if (lines[i].row && selected >= 0 && selected < static_cast<int>(page->rows.size()) &&
                lines[i].row == &page->rows[selected] &&
                page->rows[selected].kind != RowKind::Separator) {
                selLine = i;
                break;
            }
        }
    }

    // Rows have a fixed pitch (row + gap; a header is shorter than a row), so
    // the number of lines that fit follows from the total height.
    const float allHeight = heightOfLines(lines, m, 0, count);
    int visible = count;
    if (allHeight > limit) {
        visible = std::max(1, static_cast<int>(std::floor(limit / (m.rowHeight + m.rowGap))));
        // Never show more lines than the limit allows (headers pull it down).
        while (visible > 1 && heightOfLines(lines, m, 0, visible) > limit) --visible;
    }
    outVisible = visible;

    if (visible >= count) {
        w.first = 0;
        w.count = count;
        return w;
    }

    const int maxTarget = count - visible;
    if (selLine < target) target = selLine;
    else if (selLine >= target + visible) target = selLine - visible + 1;
    target = std::max(0, std::min(target, maxTarget));
    // Never start the window on a group header without its first row, and
    // never cut the last header off at the bottom: in both cases one fewer
    // line is shown so headers always appear complete.
    while (target > 0 && target < count - 1 && lines[target].header) --target;
    while (visible > 1 && target + visible < count && lines[target + visible - 1].header) {
        --visible;
    }
    // Last line of the window must fit completely (no half-clipped row).
    while (visible > 1 && heightOfLines(lines, m, target, visible) > limit) --visible;

    w.first = target;
    w.count = visible;
    return w;
}

/// Keeps the animation state in sync with the window for this frame.
void updateScroll(const std::vector<LaidRow>& lines, const Page* page,
                  const Metrics& m, ScrollState& s, int selected) {
    s.count = static_cast<int>(lines.size());
    int visible = 0;
    const ScrollWindow w = computeWindow(lines, page, m, selected, s.target, visible);
    s.target = w.first;
    s.visible = visible;
    if (s.first <= 0.0f && s.target > 0) s.first = static_cast<float>(s.target - 1);
}

float nowSeconds() { return static_cast<float>(glfwGetTime()); }

} // namespace

// ---------------------------------------------------------------------------
// Stack helpers
// ---------------------------------------------------------------------------

void begin(MenuView& view, Page page) {
    view.stack.reset(std::move(page));
    view.scroll.assign(1, ScrollState());
    view.hoverIndex = -1;
    view.hits.clear();
}

void push(MenuView& view, Page page) {
    view.stack.push(std::move(page));
    if (view.scroll.size() < view.stack.depth()) view.scroll.resize(view.stack.depth());
    view.hoverIndex = -1;
    view.hits.clear();
}

bool pop(MenuView& view) {
    const bool popped = view.stack.pop();
    if (view.scroll.size() > view.stack.depth()) view.scroll.resize(view.stack.depth());
    if (popped && view.returnTo >= 0) {
        view.stack.setSelection(view.returnTo);
        view.returnTo = -1;
    }
    view.hoverIndex = -1;
    view.hits.clear();
    return popped;
}

void replaceCurrent(MenuView& view, Page page) {
    Page* current = view.stack.current();
    if (!current) {
        begin(view, std::move(page));
        return;
    }
    const int selected = view.stack.selection();
    const std::string selectedLabel =
        (selected >= 0 && selected < static_cast<int>(current->rows.size())) ? current->rows[selected].label
                                                                            : std::string();

    *current = std::move(page);

    // Keep the selection on the same label if it still exists.
    for (int i = 0; i < static_cast<int>(current->rows.size()); ++i) {
        if (!selectedLabel.empty() && current->rows[i].label == selectedLabel &&
            current->rows[i].selectable()) {
            view.stack.setSelection(i);
            return;
        }
    }
    if (selected < 0 || selected >= static_cast<int>(current->rows.size()) ||
        !current->rows[selected].selectable()) {
        view.stack.setSelection(current->nextSelectable(std::max(0, selected - 1), 1));
    }
    view.hoverIndex = -1;
    view.hits.clear();
}

void resetView(MenuView& view) {
    view.hoverIndex = -1;
    view.hits.clear();
    for (ScrollState& s : view.scroll) s = ScrollState();
}

bool isCapturing(const MenuView& view) {
    const Page* page = view.stack.current();
    if (!page) return false;
    for (const Row& row : page->rows) {
        if (row.kind == RowKind::Keybind && row.capturing) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

void tick(MenuView& view, float deltaTime) {
    if (view.scroll.empty()) return;
    ScrollState& s = view.scroll.back();
    const float target = static_cast<float>(s.target);
    const float delta = target - s.first;
    if (std::fabs(delta) < 0.02f) {
        s.first = target;
        return;
    }
    // Exponential ease: ~10 % per 60 Hz frame.
    s.first += delta * (1.0f - std::pow(0.001f, deltaTime));
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void draw(MenuView& view, Renderer& r, GLFWwindow* window) {
    const Page* page = view.stack.current();
    if (!page) return;

    if (view.lastTime < 0.0f) view.lastTime = nowSeconds();
    const float now = nowSeconds();
    const float dt = now - view.lastTime;
    if (dt > 0.0f && dt < 0.25f) tick(view, dt);
    view.lastTime = now;

    const Theme& t = view.theme;
    const UIHelpers::UIBounds b = UIHelpers::getBounds(window);
    const float viewW = b.width();
    const float viewH = b.height();
    const float cx = b.centerX();
    const Metrics m = Metrics::compute(viewH);

    // ── Background + header band ──────────────────────────────────────────
    quad(r, b.left, b.top, b.right, b.bottom, t.background);
    quad(r, b.left, b.top, b.right, b.top + kHeaderBandH, t.shade);

    // Title + optional subtitle, centred above the card with a gold rule.
    r.drawTextCenteredColored(page->title, cx, 34.0f, 0.92f, glm::vec3(1.0f),
                              viewW - 2.0f * kSideMargin);
    if (!page->subtitle.empty()) {
        r.drawTextCenteredColored(page->subtitle, cx, 78.0f, m.hintScale, t.muted,
                                  viewW - 2.0f * kSideMargin);
    }
    quad(r, b.left + kSideMargin, kAccentY, b.right - kSideMargin, kAccentY + 2.0f, t.selection);

    // ── Card + rows ───────────────────────────────────────────────────────
    const std::vector<LaidRow> lines = layoutRows(*page);
    const int selected = view.stack.selection();
    if (view.scroll.size() < view.stack.depth()) view.scroll.resize(view.stack.depth());
    ScrollState fallback;
    ScrollState& s = view.scroll.empty() ? fallback : view.scroll.back();

    updateScroll(lines, page, m, s, selected);
    const ScrollWindow view0 = computeWindow(lines, page, m, selected, s.target, s.visible);

    // Smooth (fractional) first line: while scrolling, lines slide vertically.
    const float shift = std::max(0.0f, s.first - static_cast<float>(view0.first));
    const float contentH = heightOfLines(lines, m, view0.first, view0.count);

    // The card ends above the footer (help line or capture prompt). Short
    // pages are centred in the space between the header and the footer.
    const float areaTop = kCardTop;
    const float areaBottom = kRowsBottom;
    const float cardHeight = std::min(contentH + 2.0f * m.pad, areaBottom - areaTop);
    const float cardTop = areaTop + ((areaBottom - areaTop) - cardHeight) * 0.5f;
    const float cardBottom = cardTop + cardHeight;
    const float cardW = std::max(kMinCardWidth, std::min(viewW - 2.0f * kSideMargin, kMaxCardWidth));
    const float cardX0 = cx - cardW * 0.5f;
    const float cardX1 = cx + cardW * 0.5f;
    const float rowX0 = cardX0 + m.pad;
    const float rowX1 = cardX1 - m.pad;

    quad(r, cardX0, cardTop, cardX1, cardBottom, t.card);
    quad(r, cardX0, cardTop, cardX1, cardTop + 2.0f, glm::vec3(0.28f, 0.33f, 0.42f));

    view.hits.assign(page->rows.size(), RowHit());

    const int first = view0.first;
    const int last = view0.first + view0.count;
    float rowY = cardTop + m.pad - shift;
    for (int i = first; i < last && i < static_cast<int>(lines.size()); ++i) {
        float y0 = rowY;
        const float lineH = lineHeight(lines[i], m);
        rowY += lineH + m.rowGap;
        if (y0 + lineH <= cardTop + 2.0f) continue;   // scrolled above the card
        // Clip partially scrolled rows at the card's top edge.
        if (y0 < cardTop + 2.0f) y0 = cardTop + 2.0f;

        // Anything past the bottom of the card is clipped (the footer below
        // must stay readable, especially while scrolling).
        if (y0 >= cardBottom) continue;

        const LaidRow& line = lines[i];
        if (line.header) {
            r.drawTextColored(line.headerText, rowX0 + 2.0f, y0 + 8.0f, m.hintScale, t.accent);
            continue;
        }

        const Row& row = *line.row;
        const int index = rowIndexOf(*page, line);
        const bool isSelected = (index == selected) && row.selectable();
        const bool isHovered = (index == view.hoverIndex) && row.selectable() && !isSelected;

        glm::vec3 fill = t.row;
        if (isSelected)     fill = row.destructive ? t.danger : t.selection;
        else if (isHovered) fill = t.rowHover;
        quad(r, rowX0, y0, rowX1, y0 + m.rowHeight, fill);
        if (isSelected) {
            // White marker on the left edge, like the main menu buttons.
            quad(r, rowX0 + 3.0f, y0 + 1.0f, rowX0 + 8.0f, y0 + m.rowHeight - 1.0f,
                 glm::vec3(1.0f));
        }

        const bool rowSupported = row.enabled && row.currentOptionEnabled();
        const glm::vec3 labelInk = isSelected ? t.selectionInk : (rowSupported ? t.label : t.off);
        const float textY = centredTextY(y0, y0 + m.rowHeight, m.textScale);
        float valueLeft = rowX1 - 14.0f;

        if (row.kind == RowKind::Keybind) {
            const bool waiting = row.capturing;
            const std::string cap = waiting ? "?" : settings::bindingName(row.code);
            float capScale = m.valueScale;
            float capTextW = r.getTextWidth(cap, capScale);
            if (capTextW > 140.0f) { capScale *= 140.0f / capTextW; capTextW = 140.0f; }
            const float capW = std::max(56.0f, capTextW + 22.0f);
            const float capX1 = rowX1 - 10.0f;
            const float capX0 = capX1 - capW;
            const float capY0 = y0 + 4.0f;
            const float capY1 = y0 + m.rowHeight - 4.0f;

            glm::vec3 capFill = isSelected ? glm::vec3(1.0f) : t.keyCap;
            glm::vec3 capInk  = isSelected ? t.selectionInk : t.value;
            if (waiting) {
                capFill = isSelected ? glm::vec3(1.0f) : glm::vec3(0.40f, 0.29f, 0.05f);
                capInk  = isSelected ? t.selectionInk : glm::vec3(1.0f, 0.88f, 0.40f);
            }
            quad(r, capX0, capY0, capX1, capY1, capFill);
            r.drawTextCenteredColored(cap, (capX0 + capX1) * 0.5f,
                                      centredTextY(capY0, capY1, capScale), capScale, capInk,
                                      capW - 16.0f);
            valueLeft = capX0 - 10.0f;
        } else if (!row.value.empty()) {
            glm::vec3 valueInk = t.value;
            if (row.kind == RowKind::Toggle) valueInk = row.on ? t.on : t.off;
            if (row.kind == RowKind::Info) valueInk = t.label;
            if (isSelected) valueInk = t.selectionInk;
            if (!row.enabled || !row.currentOptionEnabled()) valueInk = t.off;
            const float vw = r.getTextWidth(row.value, m.valueScale);
            textRight(r, row.value, rowX1 - 14.0f, textY, m.valueScale, valueInk,
                      rowX1 - rowX0 - 40.0f);
            valueLeft = rowX1 - 14.0f - vw - 10.0f;
        }

        const float labelX = rowX0 + 18.0f;
        textFit(r, row.label, labelX, textY, valueLeft - labelX, m.textScale, labelInk);

        if (index >= 0) {
            RowHit& hit = view.hits[index];
            hit.rowIndex = index;
            hit.x0 = rowX0;
            hit.y0 = y0;
            hit.x1 = rowX1;
            hit.y1 = y0 + m.rowHeight;
        }
    }

    // ── Footer ────────────────────────────────────────────────────────────
    const std::string help = (selected >= 0 && selected < static_cast<int>(page->rows.size()))
                                 ? page->rows[selected].help : std::string();
    if (isCapturing(view)) {
        // The "waiting for a key" prompt replaces the help line and the card is
        // shortened so nothing is hidden behind it.
        quad(r, cardX0, kHelpY, cardX1, kHelpY + kFooterLineH,
             glm::vec3(0.40f, 0.29f, 0.05f));
        r.drawTextCenteredColored("PRESS A KEY TO BIND   ESC CANCEL", cx,
                                  centredTextY(kHelpY, kHelpY + kFooterLineH, m.hintScale),
                                  m.hintScale, glm::vec3(1.0f, 0.88f, 0.40f),
                                  cardW - 20.0f);
    } else if (!help.empty()) {
        quad(r, cardX0, kHelpY, cardX1, kHelpY + kFooterLineH, t.shade);
        r.drawTextCenteredColored(help, cx,
                                  centredTextY(kHelpY, kHelpY + kFooterLineH, m.hintScale),
                                  m.hintScale, t.accent, cardW - 2.0f * m.pad);
    }

    // Controls legend, centred at the very bottom (e.g. UP/DOWN SELECT ...).
    if (!view.legend.empty() && !isCapturing(view)) {
        const float gap = 26.0f;
        float total = 0.0f;
        float widest = 0.0f;
        for (const KeyHint& hint : view.legend) {
            const float w = r.getTextWidth(hint.key, m.hintScale) +
                            r.getTextWidth(" " + hint.text, m.hintScale);
            total += w;
            widest = std::max(widest, w);
        }
        total += gap * static_cast<float>(view.legend.size() - 1);

        // Shrink the whole legend (and its gaps) until it fits the canvas.
        const float maxWidth = viewW - 2.0f * kSideMargin;
        float scale = m.hintScale;
        if (total > maxWidth) {
            scale = std::max(0.18f, m.hintScale * maxWidth / total);
        }
        float scaledTotal = 0.0f;
        float scaledGap = gap;
        for (const KeyHint& hint : view.legend) {
            scaledTotal += r.getTextWidth(hint.key, scale) + r.getTextWidth(" " + hint.text, scale);
        }
        scaledGap *= scale / m.hintScale;
        scaledTotal += scaledGap * static_cast<float>(view.legend.size() - 1);
        (void)widest;

        float x = cx - scaledTotal * 0.5f;
        for (const KeyHint& hint : view.legend) {
            r.drawTextColored(hint.key, x, kLegendY, scale, t.label);
            x += r.getTextWidth(hint.key, scale);
            r.drawTextColored(" " + hint.text, x, kLegendY, scale, t.selection);
            x += r.getTextWidth(" " + hint.text, scale) + scaledGap;
        }
    }
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

int hitTest(const MenuView& view, float x, float y) {
    for (const RowHit& hit : view.hits) {
        if (hit.rowIndex >= 0 && x >= hit.x0 && x <= hit.x1 && y >= hit.y0 && y <= hit.y1) {
            return hit.rowIndex;
        }
    }
    return -1;
}

} // namespace menu
