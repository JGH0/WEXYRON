// Spawn regression test: the real Game, real maps, real physics.
//
// Bug: in every map a player started inside geometry ("stuck on spawn") and
// could not move; the old unstuck step pushed them up and gravity dropped
// them back, once per frame.
//
// For every map: start a real match, let physics settle, check the spawn is
// clear and settled, then walk forward, jump, and walk into the nearest wall.
// The player must never end up inside geometry, and pressing the keys must
// really move them. Finally the normal respawn (resetGame) must be safe too.
//
// Needs a GL context (xvfb-run) and must run from a directory with assets/.

#include "core/game.hpp"
#include <cstdio>
#include <cmath>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static const float kDt = 1.0f / 60.0f;

int main() {
    Game game(800, 600);
    game.init();

    const char* maps[] = {"simple.map", "arena.map", "large.map", "sniper.map",
                          "complex.map", "team_arena.map", "custom.map"};

    for (const char* map : maps) {
        std::printf("--- %s\n", map);
        if (!game.debugStartMapForTest(map)) {
            std::printf("%-16s could not start\n", map);
            ++failures;
            continue;
        }

        // Let gravity settle the spawn.
        game.debugStepForTest(60, kDt);

        glm::vec3 feet, vel;
        bool onGround = false;
        game.debugPlayerState(feet, vel, onGround);
        const glm::vec3 spawn = feet;
        std::printf("%-16s spawn=(%.2f, %.2f, %.2f)\n", map, feet.x, feet.y, feet.z);
        CHECK(!game.debugPlayerStuck());
        CHECK(std::abs(vel.y) < 0.5f);           // settled, not cycling
        CHECK(feet.y > -5.0f);                   // inside the world

        // Walk forward for one second: the body must really move.
        game.debugStepWithInputForTest(60, kDt, 1.0f, 0.0f);
        game.debugPlayerState(feet, vel, onGround);
        const float moved = glm::length(feet - spawn);
        std::printf("                 walked %.2f\n", moved);
        CHECK(moved > 1.0f);
        CHECK(!game.debugPlayerStuck());

        // Jump from the place the walk ended on, but only if that is still
        // ground (walking off a ledge is allowed; jumping off one is not a
        // spawn bug). Otherwise jump from the spawn point again.
        if (!game.debugStandingOnFloorForTest()) {
            game.debugSetFeetForTest(spawn);
            game.debugStepForTest(2, kDt);
            game.debugPlayerState(feet, vel, onGround);
        }
        // Jump: leaves the ground and lands again.
        const float groundY = feet.y;
        game.debugJumpForTest();
        bool left = false;
        for (int i = 0; i < 180; ++i) {
            game.debugStepForTest(1, kDt);
            game.debugPlayerState(feet, vel, onGround);
            if (feet.y > groundY + 0.05f) left = true;
            if (left && i > 10 && std::abs(vel.y) < 0.5f) break;
        }
        std::printf("                 after jump y=%.2f vy=%.2f left=%d\n", feet.y, vel.y, left);
        CHECK(left);                                 // really left the ground
        CHECK(std::abs(feet.y - groundY) < 1.0f);    // and came back down
        CHECK(!game.debugPlayerStuck());

        // Roam forward for 3 seconds: whatever it bumps into, the body must
        // never end up inside blocks.
        bool clean = true;
        for (int i = 0; i < 180; ++i) {
            game.debugStepWithInputForTest(1, kDt, 1.0f, 0.0f);
            if (game.debugPlayerStuck()) clean = false;
        }
        std::printf("                 after roaming stuck=%d\n", game.debugPlayerStuck());
        CHECK(clean);

        // The normal respawn path must be safe as well.
        game.debugRespawnForTest();
        game.debugStepForTest(60, kDt);
        game.debugPlayerState(feet, vel, onGround);
        std::printf("                 respawn=(%.2f, %.2f, %.2f)\n", feet.x, feet.y, feet.z);
        CHECK(!game.debugPlayerStuck());
        CHECK(std::abs(vel.y) < 0.5f);
    }

    // Text checks for the reporter: none of these lines may appear.
    std::printf(failures ? "SPAWN TEST: %d FAILURES\n" : "SPAWN TEST: ALL OK\n", failures);
    std::printf("(UNSTUCK lines before this one: see above; there must be none)\n");
    game.cleanup();
    return failures ? 1 : 0;
}
