// Collision performance test: box-vs-world queries must stay cheap on the
// biggest map, with the broad-phase grid doing the work (not a full block scan
// per query).
//
// Reports the cost of the queries the physics runs every frame for the player:
//   * bodyBlockedAt (box overlap) for a walk across the map,
//   * supportHeight / ceilingHeight (ground and head).
// and fails when a single query costs more than a small budget on average.

#include "core/game.hpp"
#include "core/map.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    Game game(800, 600);
    game.init();

    // The biggest map (135 blocks) - the worst case.
    CHECK(game.debugStartMapForTest("custom.map"));
    Map* map = game.debugCurrentMap();
    if (!map) { std::printf("FAIL: no map\n"); game.cleanup(); return 1; }
    const size_t millions = map->getBlocks().size();

    // Spread of test boxes over the whole map volume: empty space, inside
    // blocks and on surfaces - the mix the physics meets while walking.
    std::vector<glm::vec3> feet;
    for (float x = -9.0f; x <= 9.0f; x += 0.5f) {
        for (float z = -9.0f; z <= 9.0f; z += 0.5f) {
            for (float y = -1.0f; y <= 3.0f; y += 0.25f) {
                feet.emplace_back(x, y, z);
            }
        }
    }

    const int repeats = 20;
    size_t hits = 0, queries = 0;

    auto timeIt = [&](const char* what, auto&& fn, double budgetUs) {
        const auto start = std::chrono::steady_clock::now();
        for (int r = 0; r < repeats; ++r) {
            for (const glm::vec3& f : feet) fn(f);
        }
        const double us = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - start).count();
        const double perQuery = us / (static_cast<double>(feet.size()) * repeats);
        std::printf("%-16s %8.2f us/query  (%zu boxes x %d)\n", what, perQuery, feet.size(), repeats);
        CHECK(perQuery < budgetUs);
    };

    std::printf("custom.map: %zu blocks, %zu probe boxes\n", millions, feet.size());

    timeIt("bodyBlockedAt", [&](const glm::vec3& f) {
        glm::vec3 mn(-0.35f + f.x, f.y, -0.35f + f.z);
        glm::vec3 mx(0.35f + f.x, f.y + 1.8f, 0.35f + f.z);
        if (map->boxOverlapsBlock(mn, mx)) ++hits;
        ++queries;
    }, 5.0);

    timeIt("supportHeight", [&](const glm::vec3& f) {
        glm::vec3 mn(-0.35f + f.x, f.y, -0.35f + f.z);
        glm::vec3 mx(0.35f + f.x, f.y + 1.8f, 0.35f + f.z);
        float h = f.y;
        map->supportHeight(mn, mx, 0.4f, h);
    }, 5.0);

    timeIt("ceilingHeight", [&](const glm::vec3& f) {
        glm::vec3 mn(-0.35f + f.x, f.y, -0.35f + f.z);
        glm::vec3 mx(0.35f + f.x, f.y + 1.8f, 0.35f + f.z);
        map->ceilingHeight(mn, mx, 1.0f);
    }, 5.0);

    std::printf("overlaps: %zu of %zu queries (%.1f%%)\n", hits, queries,
                100.0 * static_cast<double>(hits) / static_cast<double>(queries > 0 ? queries : 1));
    CHECK(hits > 0);   // the probes must actually touch geometry

    std::printf(failures ? "COLLISION PERF TEST: %d FAILURES\n" : "COLLISION PERF TEST: ALL OK\n",
                failures);
    game.cleanup();
    return failures ? 1 : 0;
}
