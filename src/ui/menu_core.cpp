#include "menu_core.hpp"

#include <algorithm>
#include <utility>

namespace menu {

namespace {
inline int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

// ---------------------------------------------------------------------------
// Row factories
// ---------------------------------------------------------------------------

Row action(std::string label, std::function<void()> onActivate, std::string help, int id) {
    Row r;
    r.kind = RowKind::Action;
    r.label = std::move(label);
    r.help = std::move(help);
    r.onActivate = std::move(onActivate);
    r.id = id;
    return r;
}

Row toggle(std::string label, bool value, std::function<void(bool)> onChanged, std::string help) {
    Row r;
    r.kind = RowKind::Toggle;
    r.label = std::move(label);
    r.on = value;
    r.value = value ? r.onText : r.offText;
    r.onChanged = std::move(onChanged);
    r.help = std::move(help);
    return r;
}

Row cycle(std::string label, std::vector<std::string> options, int index,
          std::function<void(int)> onChanged, std::string help) {
    Row r;
    r.kind = RowKind::Cycle;
    r.label = std::move(label);
    r.options = std::move(options);
    r.optionIndex = r.options.empty() ? 0 : clampInt(index, 0, static_cast<int>(r.options.size()) - 1);
    if (!r.options.empty()) r.value = r.options[r.optionIndex];
    r.onCycle = std::move(onChanged);
    r.help = std::move(help);
    return r;
}

Row keybind(std::string label, int code, std::function<void(int)> onRebound,
            std::string help, bool isDefault) {
    Row r;
    r.kind = RowKind::Keybind;
    r.label = std::move(label);
    r.code = code;
    r.isDefault = isDefault;
    r.onRebound = std::move(onRebound);
    r.help = std::move(help);
    return r;
}

Row info(std::string label, std::string value, std::string help) {
    Row r;
    r.kind = RowKind::Info;
    r.label = std::move(label);
    r.value = std::move(value);
    r.help = std::move(help);
    return r;
}

Row separator(std::string groupLabel) {
    Row r;
    r.kind = RowKind::Separator;
    r.label = std::move(groupLabel);
    return r;
}

std::vector<KeyHint> defaultLegend() {
    return {
        {"UP/DOWN", "SELECT"},
        {"LEFT/RIGHT", "CHANGE"},
        {"ENTER", "CONFIRM"},
        {"ESC", "BACK"},
    };
}

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------

Page::Page(std::string title, std::string subtitle)
    : title(std::move(title)), subtitle(std::move(subtitle)) {}

Page& Page::add(Row row) {
    rows.push_back(std::move(row));
    return *this;
}

int Page::nextSelectable(int from, int step) const {
    const int count = static_cast<int>(rows.size());
    if (count == 0 || step == 0) return -1;
    step = step > 0 ? 1 : -1;

    int index = from;
    for (int visited = 0; visited <= count; ++visited) {
        if (index < 0) index = count - 1;
        if (index >= count) index = 0;
        if (rows[index].selectable()) return index;
        index += step;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// MenuStack
// ---------------------------------------------------------------------------

void MenuStack::clear() {
    pages_.clear();
    selection_.clear();
}

void MenuStack::reset(Page page) {
    clear();
    pages_.push_back(std::move(page));
    selection_.push_back(pages_.back().nextSelectable(0, 1));
}

void MenuStack::push(Page page) {
    if (pages_.empty()) {
        reset(std::move(page));
        return;
    }
    pages_.push_back(std::move(page));
    selection_.push_back(pages_.back().nextSelectable(0, 1));
}

bool MenuStack::pop() {
    if (pages_.size() <= 1) return false;
    pages_.pop_back();
    selection_.pop_back();
    return true;
}

Page* MenuStack::current() { return pages_.empty() ? nullptr : &pages_.back(); }

const Page* MenuStack::current() const { return pages_.empty() ? nullptr : &pages_.back(); }

int MenuStack::selection() const {
    return selection_.empty() ? -1 : selection_.back();
}

void MenuStack::setSelection(int index) {
    if (selection_.empty()) return;
    const Page* page = current();
    if (!page) return;
    if (index >= 0 && index < static_cast<int>(page->rows.size())) {
        selection_.back() = index;
    }
}

void MenuStack::moveSelection(int step) {
    const Page* page = current();
    if (!page || selection_.empty()) return;
    const int next = page->nextSelectable(selection_.back() + (step > 0 ? 1 : -1), step);
    if (next >= 0) selection_.back() = next;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

Metrics Metrics::compute(float canvasHeight) {
    Metrics m;
    // Rows have a fixed pitch (30px row + 3px gap) and group headers are
    // taller than that gap (they get air above and below), so a page with a
    // few groups shows about twelve rows between the title and the footer of
    // the 600-unit canvas. Scaling rows with the window would leave a gap or
    // overflow the card, because the title/footer bands are fixed.
    (void)canvasHeight;
    m.rowHeight = 30.0f;
    m.rowGap = 3.0f;
    m.headerHeight = 22.0f;
    m.pad = 10.0f;
    m.textScale = 0.52f;
    m.valueScale = 0.46f;
    m.hintScale = 0.38f;
    return m;
}

std::vector<LaidRow> layoutRows(const Page& page) {
    std::vector<LaidRow> lines;
    lines.reserve(page.rows.size() + 4);
    for (const Row& row : page.rows) {
        if (row.kind == RowKind::Separator) {
            LaidRow line;
            line.row = nullptr;
            line.header = true;
            line.headerText = row.label;
            lines.push_back(std::move(line));
            continue;
        }
        LaidRow line;
        line.row = &row;
        line.header = false;
        lines.push_back(std::move(line));
    }
    return lines;
}

} // namespace menu
