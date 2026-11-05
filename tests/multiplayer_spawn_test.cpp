// Multiplayer spawn test: the real Game connected to the real server.
//
// Bug (multiplayer): after connecting the player was stuck on spawn - the
// body was inside geometry ("UNSTUCK" every frame) and could not move.
// This test connects for real, starts a map, and checks that the player is
// not inside geometry and can walk; then with two clients, that one player
// sees the other at the position that player reports (the one used for hits).
//
// Needs a server on 127.0.0.1:32124 (see tests/README of the Makefile target),
// a GL context (xvfb-run) and must run from a directory with assets/.

#include "core/game.hpp"
#include <chrono>
#include <cstdio>
#include <cmath>
#include <thread>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static const float kDt = 1.0f / 60.0f;

/// Pump the session until connected (or give up).
static bool connect(Game& game, const char* host, uint16_t port) {
    game.serverAddressForTest(host, port);
    if (!game.connectToServer(host, port)) return false;
    for (int i = 0; i < 400; ++i) {
        game.debugPumpFrame();
        if (game.isConnectedToServer()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

int main() {
    const char* host = "127.0.0.1";
    const uint16_t port = 32124;

    Game a(800, 600);
    a.init();
    if (!connect(a, host, port)) {
        std::printf("FAIL: client A could not connect (%s)\n", a.getServerConnectError().c_str());
        a.cleanup();
        return 1;
    }
    CHECK(a.isConnectedToServer());

    if (!a.debugStartMapForTest("arena.map")) {
        std::printf("FAIL: map did not start\n");
        a.cleanup();
        return 1;
    }

    // A few frames: physics, network send/receive.
    a.debugStepWithInputForTest(60, kDt, 1.0f, 0.0f);

    glm::vec3 feet, vel;
    bool onGround = false;
    a.debugPlayerState(feet, vel, onGround);
    std::printf("A: feet=(%.2f, %.2f, %.2f) stuck=%d\n", feet.x, feet.y, feet.z, a.debugPlayerStuck());
    CHECK(!a.debugPlayerStuck());               // not inside geometry after connecting
    CHECK(std::abs(feet.y) < 3.0f);             // on the arena floor/platform

    // Keep walking: a stuck multiplayer player does not move at all.
    const glm::vec3 before = feet;
    a.debugStepWithInputForTest(60, kDt, 1.0f, 0.0f);
    a.debugPlayerState(feet, vel, onGround);
    std::printf("A: after walking feet=(%.2f, %.2f, %.2f) moved=%.2f stuck=%d\n",
                feet.x, feet.y, feet.z, glm::length(feet - before), a.debugPlayerStuck());
    CHECK(glm::length(feet - before) > 1.0f);
    CHECK(!a.debugPlayerStuck());

    // Second client: B joins, both walk in opposite directions, then A must
    // see B where B says it is (within the sync delay).
    Game b(800, 600);
    b.init();
    if (!connect(b, host, port)) {
        std::printf("FAIL: client B could not connect (%s)\n", b.getServerConnectError().c_str());
        a.cleanup(); b.cleanup();
        return 1;
    }
    if (!b.debugStartMapForTest("arena.map")) {
        std::printf("FAIL: B map did not start\n");
        a.cleanup(); b.cleanup();
        return 1;
    }

    for (int i = 0; i < 90; ++i) {
        a.debugStepWithInputForTest(1, kDt, 0.0f, 1.0f);
        b.debugStepWithInputForTest(1, kDt, 0.0f, -1.0f);
        // Keep both sessions pumped (inputs are acknowledged by the server
        // and copied into packets the other side receives).
        a.debugPumpFrame();
        b.debugPumpFrame();
    }
    glm::vec3 bFeet, bVel;
    bool bGround = false;
    b.debugPlayerState(bFeet, bVel, bGround);
    std::printf("B: feet=(%.2f, %.2f, %.2f) stuck=%d\n", bFeet.x, bFeet.y, bFeet.z, b.debugPlayerStuck());
    CHECK(!b.debugPlayerStuck());

    // Both clients stop moving; keep the match running (network + gameplay)
    // until A's view of B has converged onto B's final position. Remote
    // positions are smoothed on screen, so the view trails by a little.
    b.debugStepWithInputForTest(1, kDt, 0.0f, 0.0f);     // stop at once
    b.debugPlayerState(bFeet, bVel, bGround);

    glm::vec3 aSeesB;               // A's view of B's feet
    bool converged = false;
    for (int i = 0; i < 300; ++i) {
        a.debugPumpFrame();
        b.debugPumpFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        glm::vec3 current;
        if (!a.debugRemotePlayerFeetForTest(b.debugClientId(), current)) continue;
        aSeesB = current;
        if (glm::length(current - bFeet) < 0.75f) { converged = true; break; }
    }
    CHECK(converged);
    std::printf("A sees B at (%.2f, %.2f, %.2f), B is at (%.2f, %.2f, %.2f)\n",
                aSeesB.x, aSeesB.y, aSeesB.z, bFeet.x, bFeet.y, bFeet.z);

    // Heights must agree exactly (hit detection and the drawn body share the
    // feet height); X/Z within the smoothing lag.
    CHECK(std::abs(aSeesB.y - bFeet.y) < 0.05f);
    CHECK(std::abs(aSeesB.x - bFeet.x) < 0.75f && std::abs(aSeesB.z - bFeet.z) < 0.75f);

    std::printf(failures ? "MULTIPLAYER SPAWN TEST: %d FAILURES\n" : "MULTIPLAYER SPAWN TEST: ALL OK\n", failures);
    a.cleanup();
    b.cleanup();
    return failures ? 1 : 0;
}
