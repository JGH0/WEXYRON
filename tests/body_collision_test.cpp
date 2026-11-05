// Body collision regression test: the player is a rigid box, not a point.
//
// Reported: a floating slab at foot height (feet under it, body above) could be
// walked through; only the feet touched it when jumping. Collision is now a box
// -vs- block test, so any contact with the body (toes to head) stops it.
//
// A test map is built in memory: one floor slab and one floating slab that the
// test moves to different heights. The body must be stopped when walking into
// the slab from any side, for slabs that overlap any part of the body, and must
// pass when the slab is entirely above the head.
//
// Needs a GL context (xvfb-run); no assets needed beyond what init() loads.

#include "core/game.hpp"
#include "core/map.hpp"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static const float kDt = 1.0f / 60.0f;

namespace {

/// Builds a map: floor top at y = 0, plus one floating slab centred at
/// `slabCentre` with size 2 x 0.4 x 2 (and high walls so the body cannot just
/// walk off the small floor).
std::unique_ptr<Map> makeTestMap(const glm::vec3& slabCentre, bool withSlab) {
    auto map = std::make_unique<Map>();
    map->addBlock(MapBlock(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f),
                           0, 0, true, glm::vec3(0.2f, 0.7f, 0.2f)));
    if (withSlab) {
        map->addBlock(MapBlock(slabCentre, glm::vec3(2.0f, 0.4f, 2.0f),
                               1, 1, true, glm::vec3(0.5f, 0.5f, 0.5f)));
    }
    return map;
}

/// Place the body at `feet` and let physics settle for a few frames.
void settle(Game& game, const glm::vec3& feet) {
    game.debugSetFeetForTest(feet);
    game.debugSetVerticalVelocityForTest(0.0f);
    game.debugStepForTest(3, kDt);
}

/// Feet position after the settle.
glm::vec3 feetOf(Game& game) {
    glm::vec3 f, v; bool g = false;
    game.debugPlayerState(f, v, g);
    return f;
}

} // namespace

int main() {
    Game game(800, 600);
    game.init();

    // The body comes from the game itself: feet + (bodyWidth x bodyHeight).
    const float halfBody = 0.4f;

    // Heights of the slab centre. The slab is 0.4 thick, so it spans
    // centre-0.2 .. centre+0.2. The body is 0 .. 1.8 above the feet.
    struct Case { const char* what; float centreY; bool mustBlock; };
    const Case cases[] = {
        // Slab entirely below the feet (0.0 .. -0.4): nothing to hit, the body
        // stands on the floor above it.
        {"slab below the feet",      -0.6f, false},
        // Slab at foot height (0.0 .. 0.4): the old bug - walked through.
        {"slab at foot height",       0.2f, true},
        // Slab at knee height (0.6 .. 1.0).
        {"slab at knee height",       0.8f, true},
        // Slab through the torso (1.0 .. 1.4).
        {"slab through the torso",    1.2f, true},
        // Slab at head height (1.6 .. 2.0).
        {"slab at head height",       1.8f, true},
        // Slab entirely above the body (2.0 .. 2.4): walking under it is fine.
        {"slab above the head",       2.2f, false},
    };

    for (const Case& c : cases) {
        // Slab 4 units away (footprint x from 3 to 5, body half-width 0.4), so
        // the body starts clear and has to walk into it.
        if (!game.debugInstallMapForTest(makeTestMap(glm::vec3(4.0f, c.centreY, 0.0f), true))) {
            std::printf("FAIL: map did not install\n");
            ++failures;
            continue;
        }
        // The map is already installed; enter the match on it.
        game.debugEnterPlayingForTest();

        settle(game, glm::vec3(0.0f, 0.0f, 0.0f));
        const glm::vec3 start = feetOf(game);

        // Walk towards +X for a second (5 units at 5 u/s) and see where the
        // body ends up. Without a collision it would be at x = 5.
        for (int i = 0; i < 60; ++i) {
            game.debugStepWithInputForTest(1, kDt, 1.0f, 0.0f);
        }
        const glm::vec3 end = feetOf(game);
        const float travelled = end.x - start.x;

        // Stopped before the slab's near face (3.0) minus the body half.
        const float limit = 3.0f - halfBody + 0.05f;
        const bool blocked = travelled < limit;

        std::printf("%-22s centre.y=%4.1f moved %.2f -> x=%.2f (limit %.2f) blocked=%d\n",
                    c.what, c.centreY, travelled, end.x, limit, blocked);
        if (c.mustBlock) {
            CHECK(blocked);
            CHECK(!game.debugPlayerStuck());            // stopped, not inside it
        } else {
            CHECK(!blocked);                            // free to pass
        }
    }

    // ── A fast step must not tunnel through a thin slab ────────────────────
    // One frame at a sprint speed (say 30 u/s -> 0.5 per frame) into a 0.1
    // thick plate: the body has to stop in front of it, not end up inside.
    {
        auto map = std::make_unique<Map>();
        map->addBlock(MapBlock(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f),
                               0, 0, true, glm::vec3(0.2f, 0.7f, 0.2f)));
        map->addBlock(MapBlock(glm::vec3(3.0f, 0.9f, 0.0f), glm::vec3(0.1f, 1.8f, 4.0f),
                               1, 1, true, glm::vec3(0.5f, 0.5f, 0.5f)));
        game.debugInstallMapForTest(std::move(map));
        game.debugEnterPlayingForTest();
        settle(game, glm::vec3(1.0f, 0.0f, 0.0f));

        // Sprint: the real input path at 5 u/s on a long frame (1/3 s = 1.67
        // units per frame) would jump straight through a 0.1 plate without
        // sub-stepping.
        game.debugStepWithInputForTest(1, 1.0f / 3.0f, 1.0f, 0.0f);
        const glm::vec3 after = feetOf(game);
        std::printf("fast step into thin slab: feet x=%.2f (plate at 2.95..3.05) stuck=%d\n",
                    after.x, game.debugPlayerStuck());
        CHECK(after.x < 2.95f + 0.01f);           // stopped in front of it
        CHECK(!game.debugPlayerStuck());
    }

    // ── Jumping onto a low slab (step height) ──────────────────────────────
    // A 0.5-high block must be climbable with a jump and the body must stand
    // on top of it, not inside it.
    {
        auto map = std::make_unique<Map>();
        map->addBlock(MapBlock(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f),
                               0, 0, true, glm::vec3(0.2f, 0.7f, 0.2f)));
        map->addBlock(MapBlock(glm::vec3(6.0f, 0.2f, 0.0f), glm::vec3(6.0f, 0.4f, 4.0f),
                               1, 1, true, glm::vec3(0.5f, 0.5f, 0.5f)));
        game.debugInstallMapForTest(std::move(map));
        game.debugEnterPlayingForTest();
        settle(game, glm::vec3(1.5f, 0.0f, 0.0f));

        // Jump and walk right: the body must clear the 0.4-high lip and end on
        // top of it.
        game.debugJumpForTest();
        for (int i = 0; i < 90; ++i) {
            game.debugStepWithInputForTest(1, kDt, 1.0f, 0.0f);
        }
        const glm::vec3 top = feetOf(game);
        std::printf("after jump onto slab: feet=(%.2f, %.2f, %.2f) stuck=%d\n",
                    top.x, top.y, top.z, game.debugPlayerStuck());
        CHECK(top.y > 0.35f);                 // on top of the slab (0.4 lip)
        CHECK(!game.debugPlayerStuck());      // not lodged inside it
    }

    std::printf(failures ? "BODY COLLISION TEST: %d FAILURES\n" : "BODY COLLISION TEST: ALL OK\n",
                failures);
    game.cleanup();
    return failures ? 1 : 0;
}
