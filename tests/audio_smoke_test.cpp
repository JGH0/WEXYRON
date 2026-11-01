/**
 * @file audio_smoke_test.cpp
 * @brief End-to-end audio check through the real Game (needs a GL context)
 *
 * Starts the game the way the client does (Xvfb or a desktop, with or without
 * a sound card), starts a match, fires and checks that the sound engine really
 * plays effects. Meant as a quick check before a release:
 *
 *   make && xvfb-run -a ./build/bin/audio_smoke
 */

#include "core/game.hpp"
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    // Keep the Game entirely inside this block: its members (map loader
    // threads, audio device) must be destroyed before glfwTerminate() at
    // process exit, so the sound device is joined while GL still exists.
    int playing = 0;
    {
        Game game(800, 600);
        game.init();

        // Start a match on the first map.
        game.debugOpenMenu("maps");
        const bool selected = game.debugMenuSelect("Arena");
        CHECK(selected);
        if (selected) game.debugMenuKey("enter");
        std::printf("state after starting the map: %d\n", game.debugState());
        CHECK(game.debugState() == 1);              // GameState::PLAYING
        // Wait for the background map load to finish (the worker uses its own
        // GLFW window; tearing the Game down mid-load would join it inside
        // GLFW shutdown, which deadlocks on some drivers).
        for (int i = 0; i < 300 && game.isMapLoading(); ++i) game.update(1.0f / 60.0f);
        CHECK(!game.isMapLoading());

        // Fire the pistol; the shot must be mixed while the next frames run.
        game.debugShootAndStepForTest();
        playing = game.debugPlayingSoundCount();
        std::printf("playing sounds after firing: %d\n", playing);
        CHECK(playing > 0);
    }

    if (failures == 0) std::printf("audio_smoke: all checks passed\n");
    else std::printf("audio_smoke: %d failure(s)\n", failures);
    std::fflush(stdout);
    // The Game (map loader worker, GLFW window, audio device) is destroyed
    // above; some GL drivers are slow to tear the window down, so leave the
    // process directly instead of waiting for the C runtime exit path.
    return failures == 0 ? 0 : 1;
}
