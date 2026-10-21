/**
 * @file menu_model_test.cpp
 * @brief Unit test for the menu model (menu_core): selection, navigation,
 *        page stack, value rows and layout. No OpenGL needed.
 *
 * Build & run:
 *   g++ -std=c++17 -I./include -I./src tests/menu_model_test.cpp \
 *       src/ui/menu_core.cpp -o /tmp/menu_model && /tmp/menu_model
 */

#include "ui/menu_core.hpp"
#include <iostream>

using namespace menu;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::cout << "FAIL " << __LINE__ << ": " #c "\n"; ++failures; } } while (0)

int main() {
    Page p("TEST");
    p.add(separator("GROUP"));                 // 0, not selectable
    p.add(toggle("A", false, nullptr));        // 1
    Row disabled = action("DISABLED", nullptr);
    disabled.enabled = false;
    p.add(disabled);                           // 2, skipped
    p.add(cycle("C", {"X", "Y", "Z"}, 1, nullptr));  // 3
    p.add(keybind("K", 87, nullptr));          // 4
    p.add(info("I", "V"));                     // 5, read-only

    // nextSelectable: finds the row itself, skips separators/disabled/info.
    CHECK(p.nextSelectable(0, 1) == 1);
    CHECK(p.nextSelectable(2, 1) == 3);
    CHECK(p.nextSelectable(5, 1) == 1);        // wraps (info not selectable)
    CHECK(p.nextSelectable(0, -1) == 4);       // wraps backwards

    MenuStack stack;
    stack.reset(p);
    CHECK(stack.selection() == 1);
    stack.moveSelection(+1);
    CHECK(stack.selection() == 3);             // skipped the disabled row
    stack.moveSelection(-1);
    CHECK(stack.selection() == 1);
    stack.moveSelection(-1);
    CHECK(stack.selection() == 4);

    Page sub("SUB");
    sub.add(action("back", nullptr));
    stack.push(sub);
    CHECK(stack.depth() == 2 && stack.selection() == 0);
    CHECK(stack.pop());
    CHECK(stack.depth() == 1 && stack.selection() == 4);
    CHECK(!stack.pop());                       // the bottom page stays

    // Value rows.
    Row t = toggle("T", false, nullptr);
    CHECK(t.value == "CLEAR");
    t.toggleValue();
    CHECK(t.on && t.value == "SET");
    Row c = cycle("C", {"A", "B", "C"}, 0, nullptr);
    c.cycleValue(+1);
    CHECK(c.value == "B");
    c.cycleValue(-1);
    c.cycleValue(-1);
    CHECK(c.value == "C");                     // wraps backwards
    c.cycleValue(+1);
    CHECK(c.value == "A");
    Row k = keybind("K", 87, nullptr);
    int rebound = -1;
    k.onRebound = [&](int code) { rebound = code; };
    k.setBinding(65);
    CHECK(k.code == 65 && rebound == 65);

    // Layout.
    Metrics m = Metrics::compute(600.0f);
    CHECK(m.rowHeight > 20 && m.rowHeight < 40);
    const auto lines = layoutRows(p);
    CHECK(lines.size() == 6);                  // 5 rows + 1 header
    CHECK(lines[0].header && lines[1].row != nullptr);

    std::cout << (failures ? "FAILURES " : "ALL OK ") << failures << "\n";
    return failures ? 1 : 0;
}
