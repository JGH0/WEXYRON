/**
 * @file settings_test.cpp
 * @brief Tests for the settings registry and the settings file: defaults,
 *        creating the file, saving/loading, invalid values, key names.
 *
 * Build & run:
 *   g++ -std=c++17 -I./include -I./src tests/settings_test.cpp \
 *       src/core/settings.cpp -lglfw -o /tmp/settings_test && /tmp/settings_test
 */

#include "core/settings.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace settings;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::cout << "FAIL " << __LINE__ << ": " #c "\n"; ++failures; } } while (0)

static const char* kPath = "/tmp/cube_settings_test/settings.cfg";

/// Copy the current GameSettings into the registry rows.
static void resetRegistry(GameSettings& s) { syncSettingDefs(s); }

/// Mirror Game::loadSettings(): file -> registry -> GameSettings.
static void loadInto(GameSettings& s) {
    auto& defs = const_cast<std::vector<SettingDef>&>(gameSettingDefs());
    SettingsFile file(kPath);
    file.load(defs);
    applySettingDefs(s, defs);
}

/// Change a row the way the menu does and check the member followed.
static void setRow(const char* key, const Value& v, GameSettings& s) {
    for (auto& d : gameSettingDefs()) {
        if (d.key != key) continue;
        *d.value = v;
        d.apply(v);
        const Value now = d.get(s);
        CHECK(now.type == d.value->type && now.b == d.value->b && now.i == d.value->i);
        return;
    }
    CHECK(false && "missing setting");
}

int main() {
    // Defaults are the values the game launched with before settings existed.
    GameSettings def;
    CHECK(def.bindForward == GLFW_KEY_W);
    CHECK(def.bindBackward == GLFW_KEY_S);
    CHECK(def.bindLeft == GLFW_KEY_A);
    CHECK(def.bindRight == GLFW_KEY_D);
    CHECK(def.bindJump == GLFW_KEY_SPACE);
    CHECK(def.bindMenu == GLFW_KEY_ESCAPE);
    CHECK(def.bindGrenade == GLFW_KEY_G);
    CHECK(def.bindShoot == mouseBinding(GLFW_MOUSE_BUTTON_LEFT));
    CHECK(def.maxFps == 144 && def.vsync && !def.showFps && !def.infiniteAmmo);
    CHECK(def.performance == PERF_BALANCED);

    // First launch: the file is created with the defaults.
    std::filesystem::remove_all("/tmp/cube_settings_test");
    {
        GameSettings s;
        auto defs = makeGameSettingDefs(s);
        SettingsFile file(kPath);
        CHECK(file.load(defs));
        CHECK(std::filesystem::exists(kPath));
        CHECK(!file.lastMessage().empty());
    }

    // Change rows, save, reload into the settings.
    {
        GameSettings s;
        resetRegistry(s);
        loadInto(s);
        setRow("show_fps", Value(ValueType::Bool, true, 0), s);
        setRow("bind_forward", Value(ValueType::Key, false, GLFW_KEY_UP), s);
        setRow("bind_shoot", Value(ValueType::Key, false, mouseBinding(GLFW_MOUSE_BUTTON_RIGHT)), s);
        setRow("max_fps", Value(ValueType::Int, false, 0), s);
        setRow("performance", Value(ValueType::Choice, false, PERF_HIGH), s);
        SettingsFile file(kPath);
        CHECK(file.save(gameSettingDefs()));
    }
    {
        GameSettings s;
        resetRegistry(s);
        loadInto(s);
        CHECK(s.showFps);
        CHECK(s.bindForward == GLFW_KEY_UP);
        CHECK(s.bindShoot == mouseBinding(GLFW_MOUSE_BUTTON_RIGHT));
        CHECK(s.maxFps == 0);
        CHECK(s.performance == PERF_HIGH);
        CHECK(s.bindJump == GLFW_KEY_SPACE);   // untouched entries keep defaults
    }

    // Invalid values keep the default, unknown keys are dropped, the file is
    // rewritten with the keys the build knows.
    {
        std::ofstream out(kPath);
        out << "# test\nunknown_key=1\nshow_fps=maybe\nmax_fps=999\nperformance=high\n"
               "bind_left=mouse:1\ngarbage line\nvsync=off\n";
        out.close();
        GameSettings s;
        resetRegistry(s);
        loadInto(s);
        CHECK(!s.showFps);
        CHECK(s.maxFps == 144);
        CHECK(s.performance == PERF_HIGH);
        CHECK(s.bindLeft == mouseBinding(1));
        CHECK(!s.vsync);
        std::ifstream in(kPath);
        const std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(all.find("unknown_key") == std::string::npos);
        CHECK(all.find("bind_forward=") != std::string::npos);
    }

    // Display names.
    CHECK(keyName(GLFW_KEY_SPACE) == "SPACE");
    CHECK(keyName(GLFW_KEY_ESCAPE) == "ESC");
    CHECK(keyName(GLFW_KEY_F5) == "F5");
    CHECK(bindingName(mouseBinding(GLFW_MOUSE_BUTTON_RIGHT)) == "RIGHT MOUSE");

    std::cout << (failures ? "FAILURES " : "ALL OK ") << failures << "\n";
    return failures ? 1 : 0;
}
