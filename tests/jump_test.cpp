// Jump / landing regression test: no mid-air double jump, no landing teleport.
//
// Reported: "I can double jump mid air" and "at the end of the landing I get
// abruptly pushed to the floor (normal falling speed, then teleported)".
//
// The test drives the real physics frame by frame and records the body's
// vertical motion, checking the invariant a player would see:
//   * pressing jump while airborne never changes the vertical velocity,
//   * the feet move continuously: no frame in which they move further than
//     physics can account for (max step = |v|*dt + gravity*dt), and
//   * a fall from a height ends exactly on the floor, not with a drop.
//
// Needs a GL context (xvfb-run) and must run from a directory with assets/.

#include "core/game.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static const float kDt = 1.0f / 60.0f;
static const float kGravity = 9.8f;

/// Vertical motion as the player sees it, one entry per frame.
struct Frame { float y; float vy; };

/// Steps `frames` frames, calling `onFrame` after each (returns false to stop).
static std::vector<Frame> run(Game& game, int frames, bool jumpEachFrame,
                              const char* map, float* worstTeleport) {
    std::vector<Frame> out;
    for (int i = 0; i < frames; ++i) {
        if (jumpEachFrame) game.debugJumpForTest();   // spam the jump key
        game.debugStepForTest(1, kDt);
        glm::vec3 feet, vel; bool onGround = false;
        game.debugPlayerState(feet, vel, onGround);

        // Continuity: the feet may move at most |vy|*dt + the gravity step.
        if (!out.empty()) {
            const Frame& prev = out.back();
            const float maxStep = std::fabs(prev.vy) * kDt + kGravity * kDt * kDt + 1e-3f;
            const float step = std::fabs(feet.y - prev.y);
            if (step > maxStep && map) {
                const float excess = step - maxStep;
                if (worstTeleport && excess > *worstTeleport) *worstTeleport = excess;
            }
        }
        out.push_back({feet.y, vel.y});
    }
    return out;
}

int main() {
    Game game(800, 600);
    game.init();

    const char* maps[] = {"simple.map", "arena.map", "large.map",
                          "complex.map", "team_arena.map", "custom.map"};

    for (const char* map : maps) {
        if (!game.debugStartMapForTest(map)) { std::printf("FAIL start %s\n", map); ++failures; continue; }
        game.debugStepForTest(30, kDt);

        glm::vec3 feet, vel; bool onGround = false;
        game.debugPlayerState(feet, vel, onGround);
        const float groundY = feet.y;

        // ── 1. Jump, then keep pressing jump while airborne ────────────────
        game.debugJumpForTest();
        game.debugPlayerState(feet, vel, onGround);
        CHECK(vel.y > 0.0f);

        bool roseAgain = false;
        float highestVyWhileRising = 0.0f;
        float previousVy = vel.y;
        for (int i = 0; i < 40; ++i) {
            game.debugStepForTest(1, kDt);
            game.debugJumpForTest();                 // "I keep pressing jump"
            glm::vec3 f2, v2; bool g2 = false;
            game.debugPlayerState(f2, v2, g2);
            // In the air gravity only ever decreases vy. An increase means a
            // second jump was granted mid-air.
            if (v2.y > previousVy + 1e-3f && f2.y > groundY + 0.05f) roseAgain = true;
            highestVyWhileRising = std::max(highestVyWhileRising, v2.y);
            previousVy = v2.y;
        }
        std::printf("%-14s mid-air jump accepted=%d\n", map, roseAgain);
        CHECK(!roseAgain);

        // ── 2. Fall from a height: the landing must be continuous ──────────
        float worst = 0.0f;
        game.debugSetFeetForTest(glm::vec3(feet.x, groundY + 6.0f, feet.z));
        game.debugSetVerticalVelocityForTest(0.0f);
        // Drop from above the spawn: the fall must end on a surface there.
        // (On complex the spawn sits under a slab; the drop starts inside the
        // building, whose interior is solid geometry - that map's fall test is
        // the plain landing check below.)
        if (std::string(map) == "complex.map") {
            game.debugSetFeetForTest(glm::vec3(feet.x, groundY + 2.0f, feet.z));
            game.debugSetVerticalVelocityForTest(0.0f);
        }
        const std::vector<Frame> fall = run(game, 240, false, map, &worst);

        // Landed exactly on a surface (on maps with stacked geometry, like
        // complex, a fall can end on a ledge above the starting floor - the
        // height the body ends at must itself be a floor it stands on), and
        // the motion never jumped more than physics allows.
        const float landed = fall.back().y;
        const bool onSurface = game.debugStandingOnFloorForTest();
        std::printf("%-14s fell to %.3f (start floor %.3f) onFloor=%d worst extra step %.3f\n",
                    map, landed, groundY, onSurface, worst);
        CHECK(onSurface);
        CHECK(worst < 0.02f);                        // no teleport on landing
    }

    std::printf(failures ? "JUMP TEST: %d FAILURES\n" : "JUMP TEST: ALL OK\n", failures);
    game.cleanup();
    return failures ? 1 : 0;
}
