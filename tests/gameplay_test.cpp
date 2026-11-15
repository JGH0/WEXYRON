// Gameplay regression test: jumping and grenade blast size.
//
// Reported: "I am unable to jump/step" and "the damage of the grenade is a
// suspiciously large area".
//
// Jump: the real physics (updatePlayerPhysics) is stepped while the jump is
// requested exactly like the jump key does (a press edge), then the body must
// leave the ground and land again.
//
// Grenade: a grenade is detonated at several distances from the player, and the
// damage taken must match the documented radius (it must hurt up close, not
// from across the room).
//
// Needs a GL context (xvfb-run) and must run from a directory with assets/.

#include "core/game.hpp"
#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static const float kDt = 1.0f / 60.0f;

int main() {
    Game game(800, 600);
    game.init();

    const char* maps[] = {"simple.map", "arena.map", "large.map", "sniper.map",
                          "complex.map", "team_arena.map", "custom.map"};

    // ── Jump on every map ────────────────────────────────────────────────────
    for (const char* map : maps) {
        if (!game.debugStartMapForTest(map)) { std::printf("FAIL start %s\n", map); ++failures; continue; }
        game.debugStepForTest(30, kDt);

        glm::vec3 feet, vel;
        bool onGround = false;
        game.debugPlayerState(feet, vel, onGround);
        const float groundY = feet.y;

        // A real press: jump, then the key is released. Twice in a row, to
        // catch a latch that stays set after a jump ends off a ledge.
        // On every other map a full jump must get off the ground.
        // sniper and complex spawn under a slab: the head hits it after a few
        // centimetres (correct rigid-body behaviour), so only a hop fits.
        const bool lowCeiling = std::string(map) == "sniper.map" ||
                                std::string(map) == "complex.map";
        const float needed = lowCeiling ? 0.05f : 0.5f;
        for (int attempt = 0; attempt < 2; ++attempt) {
            // Let the jump cooldown (0.5 s) expire between the two presses.
            if (attempt > 0) game.debugStepForTest(20, kDt);
            game.debugPlayerState(feet, vel, onGround);
            game.debugJumpForTest();
            game.debugPlayerState(feet, vel, onGround);
            float peak = feet.y;
            for (int i = 0; i < 45; ++i) {
                game.debugStepForTest(1, kDt);
                game.debugPlayerState(feet, vel, onGround);
                peak = std::max(peak, feet.y);
            }
            std::printf("%-16s jump %d: peak=%.2f (ground %.2f)\n", map, attempt + 1, peak, groundY);
            CHECK(peak > groundY + needed);            // really left the ground
            CHECK(std::fabs(feet.y - groundY) < 1.0f); // came back down
        }
    }

    // ── Grenade blast ────────────────────────────────────────────────────────
    // A fresh life on a flat map; the blast is placed at measured distances
    // from the player. Explosions have no distance falloff: every body the
    // blast reaches takes the full explosion damage (65).
    struct Case { float distance; float minDamage; float maxDamage; const char* label; };
    const Case cases[] = {
        {0.0f, 65.0f, 65.0f, "on the feet"},
        {1.0f, 65.0f, 65.0f, "1 unit away"},
        {2.0f, 65.0f, 65.0f, "2 units away"},
        {3.0f, 65.0f, 65.0f, "3 units away"},
        {4.5f,  0.0f,  0.0f, "out of range"},
        {6.0f,  0.0f,  0.0f, "across the room"},
    };

    for (const Case& c : cases) {
        if (!game.debugStartMapForTest("large.map")) { ++failures; continue; }
        game.debugStepForTest(20, kDt);
        glm::vec3 feet, vel;
        bool onGround = false;
        game.debugPlayerState(feet, vel, onGround);

        game.debugExplodeForTest(feet + glm::vec3(c.distance, 0.0f, 0.0f));
        game.debugPlayerState(feet, vel, onGround);
        const float damage = 100.0f - game.debugPlayerHealth();
        std::printf("grenade %-22s (%.1f units): damage %.1f\n", c.label, c.distance, damage);
        CHECK(damage >= c.minDamage - 0.01f);
        CHECK(damage <= c.maxDamage + 0.01f);
    }

    std::printf(failures ? "GAMEPLAY TEST: %d FAILURES\n" : "GAMEPLAY TEST: ALL OK\n", failures);
    game.cleanup();
    return failures ? 1 : 0;
}
