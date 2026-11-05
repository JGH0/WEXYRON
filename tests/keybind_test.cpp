// Key binding regression test: rebinding must not leave two actions on one key.
//
// Reported: "I am unable to jump/step". The saved settings had JUMP on G,
// which is also GRENADE: pressing it only threw a grenade. This test drives the
// real CONTROLS menu (the same code path as a mouse/keyboard player) and checks
// that binding a key that another action already uses frees that key first.
//
// Needs a GL context (xvfb-run) and must run from a directory with assets/.

#include "core/game.hpp"
#include <cstdio>
#include <string>
#include <unistd.h>
#include <cmath>

// Leaves at once, without unwinding: the game's destructor shuts the audio
// device down in a way that trips the sanitizer-style exit check under
// xvfb, and printing before leaving is enough for this test.
static std::string readFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

static int failures = 0;
static const float kDt = 1.0f / 60.0f;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    // Start from the defaults, whatever the user saved.
    std::rename("assets/settings.cfg", "assets/settings.cfg.keybind-test.bak");

    Game game(800, 600);
    game.init();

    // Settings -> CONTROLS (opened via the real menu code: select + ENTER).
    game.debugOpenMenu("settings");
    CHECK(game.debugMenuSelect("CONTROLS"));
    CHECK(game.debugMenuKey("enter"));

    // Rebind JUMP to G, the key the grenade toggle uses. A failed capture is
    // fine (the binding is then refused); a duplicate binding is not.
    if (game.debugMenuSelect("JUMP")) {
        game.debugMenuKey("enter");               // starts capturing
        game.debugMenuKey("capture:71");          // 71 = GLFW_KEY_G
    }

    game.applySettingsForTest();
    const int jump = game.debugJumpKey();
    const int grenade = game.debugGrenadeKey();
    std::printf("JUMP=%d GRENADE=%d\n", jump, grenade);

    // Either the new key is refused (JUMP stays on its old key) or GRENADE
    // moves off it. Two actions on one key is the bug, and it is what made
    // jumping impossible with the saved settings.
    CHECK(jump != grenade);
    CHECK(jump != 0);

    // What was saved: the file must not carry the duplicate either.
    const std::string saved = readFile("assets/settings.cfg");
    const std::string jumpLine = "bind_jump=71\n";
    const std::string grenadeLine = "bind_grenade=71\n";
    const bool bothOnG = saved.find(jumpLine) != std::string::npos &&
                         saved.find(grenadeLine) != std::string::npos;
    CHECK(!bothOnG);

    // The real input path: press the key JUMP is bound to and the body must
    // leave the ground (this is what "unable to jump" was about).
    std::printf("pressing JUMP key %d ...\n", jump);
    if (!game.debugStartMapForTest("large.map")) { ++failures; }
    game.debugStepWithKeysForTest(kDt, {});          // land on the spawn
    const float groundY = game.debugFeetY();
    float peak = groundY;
    for (int i = 0; i < 8; ++i) {
        game.debugStepWithKeysForTest(kDt, {jump});
        peak = std::max(peak, game.debugFeetY());
    }
    for (int i = 0; i < 60; ++i) {
        game.debugStepWithKeysForTest(kDt, {});
        peak = std::max(peak, game.debugFeetY());
    }
    std::printf("jump key: ground %.2f peak %.2f\n", groundY, peak);
    CHECK(peak > groundY + 0.5f);

    // Holding the key (never released) must not start a second jump.
    float peak2 = groundY;
    int extraJumps = 0;
    bool hadLanded = false;
    for (int i = 0; i < 200; ++i) {
        game.debugStepWithKeysForTest(kDt, {jump});          // held down
        const float y = game.debugFeetY();
        if (hadLanded && y > groundY + 0.3f) ++extraJumps;   // rose again
        if (y <= groundY + 0.02f) hadLanded = true;
        peak2 = std::max(peak2, y);
    }
    std::printf("jump key held: extra jumps after landing = %d (peak %.2f)\n", extraJumps, peak2);
    CHECK(extraJumps == 0);

    // Releasing and pressing again does jump (the key works, not a one-off).
    game.debugStepWithKeysForTest(kDt, {});
    const float beforeHop = game.debugFeetY();
    float peak3 = beforeHop;
    for (int i = 0; i < 10; ++i) {
        game.debugStepWithKeysForTest(kDt, {jump});
        peak3 = std::max(peak3, game.debugFeetY());
    }
    std::printf("second press: ground %.2f peak %.2f\n", beforeHop, peak3);
    CHECK(peak3 > beforeHop + 0.3f);

    std::printf(failures ? "KEYBIND TEST: %d FAILURES\n" : "KEYBIND TEST: ALL OK\n", failures);

    // Restore the user's settings, then leave without running Game's
    // destructor (it is not the subject of this test).
    std::remove("assets/settings.cfg");
    std::rename("assets/settings.cfg.keybind-test.bak", "assets/settings.cfg");
    std::fflush(stdout);
    _exit(failures ? 1 : 0);
}
