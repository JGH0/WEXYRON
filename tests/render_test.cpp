// Rendering regression test: the map must be drawn exactly once per frame.
//
// Bug: Game::render() drew the persistent map batch in the middle of the frame
// (before the scene objects were queued, so it still held the previous frame's
// instances) and endFrame() drew it again with the next camera position. The
// two copies never lined up: the map appeared doubled ("two worlds") and the
// seams/edges looked like thin flickering slivers.
//
// The test checks, for every map, that a rendered frame gives exactly one map
// draw and that the batch content is just the map (6 faces per block), with no
// leftovers from earlier frames accumulating.
//
// Needs a GL context (xvfb-run) and must run from a directory with assets/.

#include "core/game.hpp"
#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    Game game(800, 600);
    game.init();

    const char* maps[] = {"simple.map", "arena.map", "large.map", "sniper.map",
                          "complex.map", "team_arena.map", "custom.map"};

    for (const char* map : maps) {
        if (!game.debugStartMapForTest(map)) {
            std::printf("%-16s could not start\n", map);
            ++failures;
            continue;
        }
        Map* m = game.debugCurrentMap();
        const size_t blocks = m ? m->getBlocks().size() : 0;

        // Render several frames: an extra draw or a growing batch would show.
        bool okDraws = true, okCount = true;
        for (int f = 0; f < 10; ++f) {
            game.debugStepForTest(1, 1.0f / 60.0f);
            game.render();
            if (game.debugWorldDrawsLastFrame() != 1) okDraws = false;
            // Exactly six faces per block: nothing from previous frames, no
            // duplicate copy of the world.
            if (game.debugMapInstancesLastFrame() != blocks * 6) okCount = false;
        }

        std::printf("%-16s blocks=%2zu mapInstances=%zu draws/frame=%d\n",
                    map, blocks, game.debugMapInstancesLastFrame(), game.debugWorldDrawsLastFrame());
        CHECK(okDraws);
        CHECK(okCount);
    }

    std::printf(failures ? "RENDER TEST: %d FAILURES\n" : "RENDER TEST: ALL OK\n", failures);
    game.cleanup();
    return failures ? 1 : 0;
}
