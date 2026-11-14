// Bot AI smoke test.
//
// Verifies the parts that are easy to break and do not need a network/GL
// context: the map parser (old and new block formats), ground collision,
// accuracy-driven shooting, flashbang blinding, and throwable use.
//
// Build: g++ -std=c++17 -I include -I src tests/bot_ai_test.cpp src/game/bot.cpp

#include "game/bot.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    // ── Navigation / spawn validation on the remade maps ────────────────────
    for (const char* mapName : {"simple.map", "complex.map", "custom.map", "sniper.map"}) {
        bots::BotMap testMap;
        const std::string path = std::string("assets/maps/") + mapName;
        CHECK(testMap.load(path));
        CHECK(!testMap.blocks.empty());
        CHECK(testMap.spawns.size() >= 8);
        CHECK(testMap.teamSpawns.size() >= 6);
        CHECK(testMap.bombSites.size() >= 2);
        CHECK(testMap.navNodeCount() > 100);

        for (const bots::SpawnPoint& spawn : testMap.spawns) {
            CHECK(testMap.standable(spawn.position));
        }
        for (const bots::TeamSpawnPoint& spawn : testMap.teamSpawns) {
            CHECK(testMap.standable(spawn.position));
        }

        std::mt19937 navRng(11);
        glm::vec3 from(0.0f), to(0.0f);
        CHECK(testMap.randomNavPoint(navRng, from));
        CHECK(testMap.randomNavPoint(navRng, to));
        std::vector<glm::vec3> pathPoints;
        CHECK(testMap.findPath(from, to, pathPoints));
        CHECK(!pathPoints.empty());

        const bots::SpawnResult safe = testMap.findSpawn(false, 0, navRng);
        CHECK(testMap.standable(safe.position));
        std::printf("%s: blocks=%zu nav=%zu path=%zu\n", mapName,
                    testMap.blocks.size(), testMap.navNodeCount(), pathPoints.size());
    }

    bots::BotMap map;
    CHECK(map.load("assets/maps/arena.map"));
    // Arena uses the old block format (no colour). The floor must still be
    // solid, otherwise every bot falls out of the map.
    CHECK(!map.blocks.empty());
    CHECK(map.standable(glm::vec3(0.0f, 0.0f, 10.0f)));

    std::mt19937 spawnRng(7);
    const bots::SpawnResult spawn = map.findSpawn(false, 0, spawnRng);
    CHECK(map.standable(spawn.position));
    std::printf("spawn=(%.2f, %.2f, %.2f)\n", spawn.position.x, spawn.position.y, spawn.position.z);

    // Use a known open stretch of the arena floor for the combat checks, so
    // the test does not depend on the random spawn point.
    const glm::vec3 botFeet(-5.0f, 0.0f, 10.0f);
    const glm::vec3 targetFeet(5.0f, 0.0f, 10.0f);
    CHECK(map.standable(botFeet));
    CHECK(map.standable(targetFeet));
    CHECK(!map.lineBlocked(botFeet + glm::vec3(0.0f, 1.6f, 0.0f),
                           targetFeet + glm::vec3(0.0f, 0.9f, 0.0f)));

    bots::World world;
    world.map = &map;
    world.gameMode = 0;
    bots::Target target;
    target.id = 2;
    target.team = 1;
    target.alive = true;
    target.feet = targetFeet;
    world.targets.push_back(target);

    // ── Shooting ────────────────────────────────────────────────────────────
    bots::Bot shooter(1, 0, 1.0f, 1234);
    shooter.teleport(botFeet, 0.0f, 0.0f, map);
    int shots = 0;
    int hits = 0;
    for (int i = 0; i < 240; ++i) {   // 4 seconds
        std::vector<bots::Action> actions;
        shooter.update(1.0f / 60.0f, world, actions);
        for (const bots::Action& action : actions) {
            if (action.type != bots::Action::FireBullet) continue;
            ++shots;
            if (action.victimId == target.id) ++hits;
        }
    }
    std::printf("shots=%d hits=%d\n", shots, hits);
    CHECK(shots > 0);
    CHECK(hits > 0);

    // The body must stay on the map floor while moving.
    CHECK(shooter.position().y > -2.0f);

    // ── Flashbang blinding ──────────────────────────────────────────────────
    bots::Bot flashed(3, 0, 1.0f, 99);
    flashed.teleport(botFeet, 0.0f, 0.0f, map);
    flashed.applyFlash(botFeet + glm::vec3(3.0f, 1.0f, 0.0f), map);
    CHECK(flashed.blinded());

    int blindShots = 0;
    for (int i = 0; i < 60; ++i) {    // 1 second, still blinded
        std::vector<bots::Action> actions;
        flashed.update(1.0f / 60.0f, world, actions);
        blindShots += static_cast<int>(actions.size());
    }
    CHECK(blindShots == 0);           // cannot aim/fire while blind

    // ── Grenades / flashbangs ───────────────────────────────────────────────
    bots::Bot thrower(4, 0, 0.8f, 4242);
    thrower.teleport(botFeet, 0.0f, 0.0f, map);
    bots::World throwWorld = world;
    throwWorld.targets[0].feet = glm::vec3(15.0f, 0.0f, 10.0f);

    int thrown = 0;
    for (int i = 0; i < 60 * 25; ++i) {   // 25 seconds
        std::vector<bots::Action> actions;
        thrower.update(1.0f / 60.0f, throwWorld, actions);
        for (const bots::Action& action : actions) {
            if (action.type == bots::Action::ThrowGrenade ||
                action.type == bots::Action::ThrowFlashbang) {
                ++thrown;
            }
        }
    }
    std::printf("throwables=%d\n", thrown);
    CHECK(thrown > 0);

    std::printf(failures ? "BOT AI TEST: %d FAILURES\n" : "BOT AI TEST: ALL OK\n", failures);
    return failures ? 1 : 0;
}
