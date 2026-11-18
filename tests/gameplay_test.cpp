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

    // ── Stance: sneak (hold) and lie down (toggle) ──────────────────────────
    if (game.debugStartMapForTest("large.map")) {
        game.gameSettings().proneToggle = true;
        game.applySettingsForTest();
        game.debugStepForTest(30, kDt);
        const float standEye = game.debugEyeHeightForTest();
        const int sneakKey = game.debugSneakKey();
        const int proneKey = game.debugProneKey();

        game.debugStepWithKeysForTest(kDt, {sneakKey});
        for (int i = 0; i < 60; ++i) game.debugStepWithKeysForTest(kDt, {sneakKey});
        const float crouchEye = game.debugEyeHeightForTest();
        std::printf("stance crouch: stance=%d eye=%.2f (stand %.2f)\n",
                    game.debugStanceForTest(), crouchEye, standEye);
        CHECK(game.debugStanceForTest() == 1);
        CHECK(crouchEye < standEye - 0.3f);

        for (int i = 0; i < 60; ++i) game.debugStepWithKeysForTest(kDt, {});
        std::printf("stance released: stance=%d eye=%.2f\n",
                    game.debugStanceForTest(), game.debugEyeHeightForTest());
        CHECK(game.debugStanceForTest() == 0);
        CHECK(game.debugEyeHeightForTest() > standEye - 0.1f);

        // C toggles lying down and back up.
        game.debugStepWithKeysForTest(kDt, {proneKey});
        for (int i = 0; i < 60; ++i) game.debugStepWithKeysForTest(kDt, {});
        const float proneEye = game.debugEyeHeightForTest();
        std::printf("stance prone: stance=%d eye=%.2f\n",
                    game.debugStanceForTest(), proneEye);
        CHECK(game.debugStanceForTest() == 2);
        CHECK(proneEye < crouchEye);

        game.debugStepWithKeysForTest(kDt, {proneKey});
        for (int i = 0; i < 60; ++i) game.debugStepWithKeysForTest(kDt, {});
        std::printf("stance stood up: stance=%d eye=%.2f\n",
                    game.debugStanceForTest(), game.debugEyeHeightForTest());
        CHECK(game.debugStanceForTest() == 0);
    }

    // ── Infinite ammo reaches every weapon (including throwables) ────────────
    game.gameSettings().infiniteAmmo = true;
    game.applySettingsForTest();
    for (int i = 0; i < 5; ++i) {
        game.debugSelectWeaponForTest(i);
        std::printf("infinite ammo weapon %d: %s\n", i,
                    game.debugWeaponInfiniteForTest() ? "INF" : "finite");
        CHECK(game.debugWeaponInfiniteForTest());
    }

    // Throw more grenades than the finite clip holds. With infinite ammo the
    // count must not reach 0 and the weapon must keep firing (the old bug left
    // grenades/flashbangs finite while the HUD showed INF).
    game.debugSelectWeaponForTest(3);
    const int grenadeAmmoBefore = game.debugCurrentAmmo();
    for (int i = 0; i < 4; ++i) {
        game.debugShootAndStepForTest();
        game.debugStepForTest(60, kDt);   // let the throw cooldown expire
    }
    std::printf("infinite grenades: ammo %d -> %d\n", grenadeAmmoBefore,
                game.debugCurrentAmmo());
    CHECK(game.debugCurrentAmmo() > 0);
    CHECK(game.debugWeaponCanShootForTest());
    CHECK(game.debugWeaponInfiniteForTest());
    game.gameSettings().infiniteAmmo = false;
    game.applySettingsForTest();

    // ── Prone hold mode (C toggle off) ──────────────────────────────────────
    if (game.debugStartMapForTest("large.map")) {
        game.gameSettings().proneToggle = false;
        game.applySettingsForTest();
        const int proneKey = game.debugProneKey();
        for (int i = 0; i < 30; ++i) game.debugStepWithKeysForTest(kDt, {proneKey});
        std::printf("prone hold: stance=%d\n", game.debugStanceForTest());
        CHECK(game.debugStanceForTest() == 2);
        for (int i = 0; i < 60; ++i) game.debugStepWithKeysForTest(kDt, {});
        std::printf("prone released: stance=%d\n", game.debugStanceForTest());
        CHECK(game.debugStanceForTest() == 0);
        game.gameSettings().proneToggle = true;
        game.applySettingsForTest();
    }

    // ── Mouse-wheel weapon switching ────────────────────────────────────────
    if (game.debugStartMapForTest("large.map")) {
        game.debugStepForTest(2, kDt);
        const int startWeapon = game.debugWeaponForTest();
        game.debugScrollWeaponForTest(1);   // one notch down -> next weapon
        game.debugStepWithKeysForTest(kDt, {});
        std::printf("weapon wheel: %d -> %d\n", startWeapon, game.debugWeaponForTest());
        CHECK(game.debugWeaponForTest() == (startWeapon + 1) % 5);
        game.debugScrollWeaponForTest(-1);  // and back
        game.debugStepWithKeysForTest(kDt, {});
        CHECK(game.debugWeaponForTest() == startWeapon);
    }

    // ── Sniper bolt-action: rack on switch, cycle after each shot ───────────
    if (game.debugStartMapForTest("large.map")) {
        game.debugSelectWeaponForTest(2);   // switch to sniper
        CHECK(!game.debugWeaponCanShootForTest());   // must rack first
        game.debugStepForTest(60, kDt);
        CHECK(game.debugWeaponCanShootForTest());
        game.debugShootAndStepForTest();
        CHECK(!game.debugWeaponCanShootForTest());   // bolt cycle after the shot
        game.debugStepForTest(90, kDt);
        CHECK(game.debugWeaponCanShootForTest());
    }

    // ── CS-style round end: 3 s result popup, players may move ─────────────
    if (game.debugStartMapForTest("large.map")) {
        game.debugSetGameModeForTest(2);   // bomb defusal, round-based
        game.debugEndRoundForTest(0, ROUND_END_DEFENDERS_ELIMINATED);
        CHECK(game.debugRoundEndTimerForTest() > 2.5f);
        CHECK(game.debugRoundEndTimerForTest() <= 3.0f);

        glm::vec3 before, vel;
        bool onGround = false;
        game.debugPlayerState(before, vel, onGround);
        game.debugStepWithKeysForTest(kDt, {game.debugForwardKey()});
        game.debugPlayerState(before, vel, onGround);
        const float moveSpeed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
        std::printf("round end movement speed: %.2f\n", moveSpeed);
        CHECK(moveSpeed > 1.0f);   // the body is not frozen during the popup

        // Countdown still expires into the next round.
        for (int i = 0; i < 400; ++i) game.debugStepForTest(1, kDt);
        CHECK(game.debugRoundEndTimerForTest() <= 0.0f);
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
