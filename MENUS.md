# Menus and settings

The in-game menus (main menu, settings, controls, map selection, connect to
server) are built from one small framework and one settings registry, so adding
a screen, a value or a key binding does not mean touching layout code.

| File | What it holds |
| --- | --- |
| `src/ui/menu_core.hpp/.cpp` | Theme, `Row`, `Page`, `MenuStack`, layout metrics (no GL) |
| `src/ui/menu.hpp` | `MenuView`: one menu screen (stack + drawing + input state) |
| `src/ui/menu_draw.cpp` | Card, rows, key caps, help line, legend, scrolling |
| `src/ui/menu_input.cpp` | Keyboard / mouse handling and key-binding capture |
| `src/ui/menu_pages.cpp` | Pages generated from the settings registry |
| `src/core/settings.hpp/.cpp` | All settings, defaults, the settings file |
| `src/core/game_menus.cpp` | The game's screens: main menu, settings, maps |
| `src/core/game_server_menu.cpp` | "Connect to server" screen (IP/port, LAN list) |

## Using the framework

A menu screen is a `menu::Page` (title + rows) shown in a `menu::MenuView`:

```cpp
menu::Page page("MY SCREEN", "A SHORT SUBTITLE");

page.add(menu::separator("GROUP"));
page.add(menu::action("DO THING", [this]() { doThing(); }, "Help shown at the bottom"));
page.add(menu::toggle("MY OPTION", false, [](bool on) { /* ... */ }));
page.add(menu::cycle("MODE", {"EASY", "NORMAL", "HARD"}, 1, [](int index) { /* ... */ }));
page.add(menu::keybind("JUMP", GLFW_KEY_SPACE, [](int code) { /* ... */ }));
page.add(menu::info("STATUS", "READY"));

openMenuView(myView, std::move(page));   // draws on the next frame
```

An action that opens another screen pushes it on the same stack:

```cpp
page.add(menu::action("CONTROLS", [this]() {
    menu::Page controls("CONTROLS", "KEYBOARD AND MOUSE BINDINGS");
    appendBindingRows(controls);                    // one key-cap row per binding
    menu::push(settingsMenu_, std::move(controls));
}, "Change the keyboard and mouse bindings"));
```

ESC pops one page; ESC on the bottom page calls `MenuView::onClose` (the game
uses this to go back to the match). `MenuView::legend` sets the controls
legend at the bottom, `MenuView::returnTo` (set automatically) puts the
selection back on the row that opened a sub-page.

Rows:

| Factory | ENTER | LEFT / RIGHT |
| --- | --- | --- |
| `menu::action` | runs the callback | – |
| `menu::toggle` | SET / CLEAR | SET / CLEAR |
| `menu::cycle` | next option | previous / next option |
| `menu::keybind` | waits for a key or mouse button (ESC cancels) | – |
| `menu::info` | – (not selectable) | – |
| `menu::separator` | group header (not selectable) | – |

The list scrolls when it does not fit: the selection moves through the
visible window and the rows slide up/down. Holding UP/DOWN repeats.

Mouse: hovering selects, clicking activates. Moving the mouse takes over the
selection; leaving it alone leaves the keyboard selection untouched.

## Adding a setting

All persistent settings live in `settings::GameSettings` (`settings.hpp`) and
in the registry built by `makeGameSettingDefs()` (`settings.cpp`). To add one:

1. Add the member to `GameSettings` with its default value.
2. Add a row in `makeGameSettingDefs()` with a getter and setter adapter.
3. If the game has to react to it, apply it in `Game::applySettings()`
   (`game_menus.cpp`).

The settings screen, the settings file and the console dump pick it up
automatically (the screen lists everything except key bindings; key bindings
appear on the CONTROLS page).

## Settings file

`assets/settings.cfg` is written on first launch with the defaults and saved
whenever a value changes:

```
# VIDEO
max_fps=144
vsync=true
performance=1

# MOVEMENT
bind_forward=87
bind_jump=32

# COMBAT
bind_shoot=mouse:0        # mouse buttons are stored as mouse:<n>
```

The file is also watched while the game runs: edit it and save, and the change
(show FPS, FPS cap, vsync, performance, infinite ammo, bindings) is applied
within a second, without restarting. Edits made in the game are written back
immediately.

Rules:

- Unknown keys are ignored (a newer file opened by an older build is fine).
- Invalid values keep the default and are reported in the console.
- Missing keys keep the default and the file is rewritten with them.
- `bind_menu` is the key that opens the menu during play (default ESC).
- Audio volumes are stored now and applied on the next launch (the game has
  no audio mixer yet).

Delete the file to get the defaults back.

## Troubleshooting

- **Blank screen at start / keys do nothing** – every menu state needs its page
  built before it is drawn. `init()` builds the main menu; `processInput()`
  rebuilds any menu state whose page is unexpectedly missing. If you add a new
  menu state, call `openMainMenu()`/`open…()` before `render()`.
- **Key names show as `?` or `KEY 87`** – `settings::keyName()` asks GLFW for
  the layout name and falls back to the US layout; `KEY <n>` means the code is
  not a standard GLFW key (usually a broken keymap, e.g. some Xvfb setups).
- **Menu does not react to the keyboard but the mouse works** – the window does
  not have keyboard focus (click it once), or a key grab from another
  application is active.
- **Keys act once per press** – menu edges follow the physical key state.
  After a screen change `resetNavKeys()` only *marks* the edges as needing a
  re-sync; `syncMenuEdges()` re-reads them once at the start of the next frame
  (never between the menu's separate ENTER / KP ENTER queries for one press).
  A key that is still held counts as already handled. Holding UP/DOWN repeats
  on purpose; ENTER, ESC and LEFT/RIGHT only act on the initial press.
- **Settings apply immediately** – changes made in the menu are applied to the
  engine as they happen (`applySettings()`), and edits to the file are picked
  up within a second (`pollSettingsFile()`).

## Scripted UI check (development)

`CUBE_MENU_TEST` runs a menu script instead of the game loop, which makes UI
changes testable on a headless machine:

```bash
CUBE_MENU_TEST="menu:settings;down;down;enter;dump" ./build/bin/game
```

Steps: `menu:main|settings|maps|server`, `select:<row label>`, `up`, `down`,
`left`, `right`, `enter`, `escape`, `capture:<glfw code>`, `dump`, `shot`.
`tools/ui_screenshots.sh` uses it to screenshot every screen (needs Xvfb and
ImageMagick).

Automated checks live in `tests/`:

```bash
# Model only (no OpenGL)
g++ -std=c++17 -I./include -I./src tests/menu_model_test.cpp src/ui/menu_core.cpp -o /tmp/menu_model && /tmp/menu_model

# Full game menus (needs a display; xvfb-run works)
g++ -std=c++17 -I./include -I./include/stb -I./src tests/menu_smoke_test.cpp \
    build/obj/core/*.o build/obj/systems/*.o build/obj/network/*.o build/obj/ui/*.o build/obj/glad.o \
    -o /tmp/menu_smoke -lglfw -lGL -ldl -pthread
xvfb-run -a /tmp/menu_smoke
```
