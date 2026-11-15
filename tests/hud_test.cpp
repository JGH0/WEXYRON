// HUD / viewmodel regression test.
//
// Verifies that:
//   * every weapon produces a first-person viewmodel (faces submitted in the
//     weapon pass) and the HUD reads the weapon that is actually held,
//   * a new match starts with clean kill/death/round statistics,
//   * dying through the normal damage path increments the death counter once,
//   * installing another map resets the match statistics.
//
// Needs a GL context (xvfb-run) and must run from a directory with assets/.

#include "core/game.hpp"
#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static const float kDt = 1.0f / 60.0f;

int main() {
    Game game(800, 600);
    game.init();

    CHECK(game.debugStartMapForTest("simple.map"));
    game.debugStepForTest(2, kDt);
    game.render();

    // A fresh match starts with a clean scoreboard and a drawn weapon.
    CHECK(game.debugKills() == 0);
    CHECK(game.debugDeaths() == 0);
    CHECK(game.debugRoundNumber() == 1);
    CHECK(game.debugViewmodelInstancesLastFrame() > 0);

    // Every held weapon has a model and the HUD reports that weapon.
    for (int weapon = 0; weapon < 4; ++weapon) {
        game.debugSelectWeaponForTest(weapon);
        game.debugStepForTest(2, kDt);
        game.render();
        std::printf("weapon %d: viewmodel faces=%zu ammo=%d\n", weapon,
                    game.debugViewmodelInstancesLastFrame(), game.debugCurrentAmmo());
        CHECK(game.debugCurrentWeapon() == weapon);
        CHECK(game.debugViewmodelInstancesLastFrame() > 0);
    }

    // Two point-blank blasts (65 damage each) kill the player through the
    // normal damage path; the HUD death counter must count the life once.
    game.debugSelectWeaponForTest(0);
    glm::vec3 feet, velocity;
    bool onGround = false;
    game.debugPlayerState(feet, velocity, onGround);
    game.debugExplodeForTest(feet);
    game.debugExplodeForTest(feet);
    game.debugStepForTest(2, kDt);
    std::printf("after death: health=%.0f kills=%d deaths=%d\n",
                game.debugPlayerHealth(), game.debugKills(), game.debugDeaths());
    CHECK(game.debugPlayerHealth() <= 0.0f);
    CHECK(game.debugDeaths() == 1);

    // A new map is a new match: the scoreboard resets again.
    CHECK(game.debugStartMapForTest("large.map"));
    game.debugStepForTest(2, kDt);
    game.render();
    CHECK(game.debugKills() == 0);
    CHECK(game.debugDeaths() == 0);
    CHECK(game.debugRoundNumber() == 1);
    CHECK(game.debugViewmodelInstancesLastFrame() > 0);

    std::printf(failures ? "HUD TEST: %d FAILURES\n" : "HUD TEST: ALL OK\n", failures);
    game.cleanup();
    return failures ? 1 : 0;
}
