/**
 * @file menu_core.hpp
 * @brief Theme and row types for the in-game menus
 *
 * This header only needs glm (for the theme colours) so the menu model can be
 * unit-tested without an OpenGL context. See menu.hpp for the framework that
 * draws a MenuStack and routes input to it.
 *
 * Design language (matches the main menu):
 *   - dark shade band behind the screen title
 *   - gold accent line under the title
 *   - dark card holding the rows
 *   - selected row: gold fill, dark text, white marker on the left
 *   - values right-aligned; key bindings shown as a key cap
 *   - controls legend centred at the bottom
 */

#ifndef MENU_CORE_HPP
#define MENU_CORE_HPP

#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <vector>

namespace menu {

/// Palette of the whole UI, so all menus look alike.
struct Theme {
    glm::vec3 background{0.13f, 0.18f, 0.26f};   ///< screens without a 3D scene
    glm::vec3 shade{0.06f, 0.08f, 0.11f};        ///< band behind the title
    glm::vec3 card{0.09f, 0.11f, 0.15f};         ///< panel holding the rows
    glm::vec3 row{0.16f, 0.19f, 0.25f};          ///< normal row
    glm::vec3 rowHover{0.21f, 0.25f, 0.32f};     ///< row under the mouse
    glm::vec3 keyCap{0.06f, 0.07f, 0.09f};       ///< key-cap background
    glm::vec3 selection{0.95f, 0.78f, 0.20f};    ///< selected row fill
    glm::vec3 selectionInk{0.10f, 0.09f, 0.05f}; ///< text on the selection
    glm::vec3 accent{0.98f, 0.62f, 0.20f};       ///< group headers, hints
    glm::vec3 label{0.85f, 0.88f, 0.92f};        ///< row text
    glm::vec3 value{1.00f, 0.85f, 0.35f};        ///< choice values
    glm::vec3 on{0.45f, 0.78f, 0.32f};
    glm::vec3 off{0.60f, 0.64f, 0.70f};
    glm::vec3 muted{0.62f, 0.68f, 0.76f};
    glm::vec3 danger{0.95f, 0.40f, 0.35f};
};

/// How a row reacts to ENTER / LEFT / RIGHT / mouse.
enum class RowKind {
    Action,     ///< runs `actionRow` (navigate, apply, quit...)
    Toggle,     ///< flips `on`
    Cycle,      ///< steps forward/back through `options`
    Keybind,    ///< shows a key cap; ENTER starts waiting for a key
    Info,       ///< read-only text (current game mode, network status)
    Separator,  ///< group header, never selectable
};

/// One line in a menu.
struct Row {
    RowKind kind = RowKind::Action;
    std::string label;          ///< left column
    std::string value;          ///< right column (rebuild with refresh())
    std::string help;           ///< shown in the footer while selected
    bool enabled = true;        ///< disabled rows are dimmed and skipped
    bool destructive = false;   ///< drawn in the danger colour
    int  id = -1;               ///< caller-defined id for actionRow()

    // Toggle
    bool on = false;
    /// Display text for the two toggle states (defaults: SET / CLEAR).
    std::string onText = "SET";
    std::string offText = "CLEAR";

    // Cycle
    std::vector<std::string> options;
    int optionIndex = 0;

    // Keybind (kind == Keybind). `code` is a GLFW key or mouse binding
    // (see settings::bindingName); `isDefault` only drives the hint.
    int  code = -1;
    bool isDefault = true;
    /// Set by the framework while waiting for the user to press a key.
    bool capturing = false;

    /// True when the row can be selected (information is read-only).
    bool selectable() const {
        return kind != RowKind::Separator && kind != RowKind::Info && enabled;
    }

    /// Callbacks (set by the factory functions, or by hand).
    std::function<void()> onActivate;   ///< Action rows
    std::function<void(bool)> onChanged;///< Toggle rows
    std::function<void(int)> onCycle;   ///< Cycle rows (new option index)
    std::function<void(int)> onRebound; ///< Keybind rows (new binding code)

    /// Activate a row as if ENTER was pressed. The framework owns selection
    /// and key capture; the callbacks are only about the value.
    void toggleValue() {
        on = !on;
        value = on ? onText : offText;
        if (onChanged) onChanged(on);
    }
    void cycleValue(int direction) {
        if (options.empty()) return;
        const int count = static_cast<int>(options.size());
        optionIndex = ((optionIndex + (direction >= 0 ? 1 : -1)) % count + count) % count;
        value = options[optionIndex];
        if (onCycle) onCycle(optionIndex);
    }
    void setBinding(int newCode) {
        if (newCode < 0) return;
        code = newCode;
        if (onRebound) onRebound(code);
    }
};

/// Factory functions keep menu construction terse and uniform.
Row action(std::string label, std::function<void()> onActivate,
           std::string help = std::string(), int id = -1);
Row toggle(std::string label, bool value, std::function<void(bool)> onChanged,
           std::string help = std::string());
Row cycle(std::string label, std::vector<std::string> options, int index,
          std::function<void(int)> onChanged, std::string help = std::string());
Row keybind(std::string label, int code, std::function<void(int)> onRebound,
            std::string help = std::string(), bool isDefault = true);
Row info(std::string label, std::string value, std::string help = std::string());
Row separator(std::string groupLabel);

/// One screen of rows under a title.
struct Page {
    std::string title;
    std::string subtitle;
    std::vector<Row> rows;

    Page() = default;
    explicit Page(std::string title, std::string subtitle = std::string());

    Page& add(Row row);

    /// First selectable row at or after `from`; -1 when there is none.
    int nextSelectable(int from, int step) const;
    /// Whether any row can be selected.
    bool hasSelectable() const { return nextSelectable(0, 1) >= 0; }
};

/// One entry of the footer legend ("UP/DOWN", "SELECT").
struct KeyHint { std::string key; std::string text; };

/// Build the standard controls legend for a menu.
std::vector<KeyHint> defaultLegend();

/**
 * @brief A stack of pages navigated with ESC/BACK.
 *
 * The main menu is the bottom page; sub-screens (settings, controls, ...) are
 * pushed on top. Escape pops one page; the bottom page is never popped (the
 * owner decides what ESC means there, e.g. leave the menu).
 */
class MenuStack {
public:
    /// Drop the stack and push `page` as the only page.
    void reset(Page page);

    /// Push a page on top of the current one.
    void push(Page page);

    /// Go back one page. Returns false when already on the bottom page.
    bool pop();

    bool empty() const { return pages_.empty(); }
    size_t depth() const { return pages_.size(); }
    /// Page `index` from the bottom (0 = root); index < depth().
    const Page& pageAt(size_t index) const { return pages_[index]; }
    Page* current();
    const Page* current() const;

    void clear();

    /// Selected row on the current page (-1 when no selectable row).
    int selection() const;
    void setSelection(int index);

    /// Move the selection up (-1) or down (+1), skipping separators and
    /// disabled rows and wrapping around.
    void moveSelection(int step);

private:
    std::vector<Page> pages_;
    std::vector<int> selection_;     ///< selection per page
};

// ---------------------------------------------------------------------------
// Layout helpers (pure, no drawing)
// ---------------------------------------------------------------------------

/// Row metrics derived from the available canvas height.
struct Metrics {
    float rowHeight = 30.0f;
    float rowGap = 4.0f;
    float headerHeight = 18.0f;
    float pad = 10.0f;
    float textScale = 0.52f;
    float valueScale = 0.46f;
    float hintScale = 0.38f;

    static Metrics compute(float canvasHeight);
};

/**
 * @brief A row plus its group header, flattened for scrolling.
 *
 * Rows are laid out top to bottom; the card in menu_draw.cpp keeps the
 * selected row inside the visible window.
 */
struct LaidRow {
    const Row* row = nullptr; ///< null for group headers
    bool header = false;
    std::string headerText;
};


/// Flatten the rows of `page` into laid-out lines (headers + rows).
std::vector<LaidRow> layoutRows(const Page& page);

/// Per-page scroll state (see menu_draw.cpp).
struct ScrollState {
    float first = 0.0f;  ///< first visible line, fractional while animating
    int   target = 0;    ///< line the view scrolls towards
    int   count = 0;     ///< laid-out lines in the page
    int   visible = 0;   ///< lines in the window at the current target
    int   selectedLine = -1;
};

} // namespace menu

#endif // MENU_CORE_HPP
