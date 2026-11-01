/**
 * @file main.cpp
 * @brief Main entry point for the OpenGL 3D Game application
 * @author OpenGL Game Developer
 * @date 2025
 */

#include "core/game.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <ctime>

/**
 * @brief Main function that initializes and runs the game loop
 *
 * Creates a Game instance with window dimensions 800x600, initializes it,
 * and runs the main game loop with delta time calculations. With CUBE_MENU_TEST
 * set, a scripted menu run is executed instead (development aid).
 *
 * @return int Returns 0 on successful execution
 */
// ---------------------------------------------------------------------------
// Scripted UI check (development aid)
//
// CUBE_MENU_TEST="menu:settings;down;enter;dump" opens a menu, sends keys and
// prints the page after each step. It lets the UI be exercised on a headless
// machine (see tools/ui_screenshots.sh). Without the variable the normal game
// loop runs.
// ---------------------------------------------------------------------------
static void runMenuTest(Game& game, const char* script) {
    std::stringstream steps(script);
    std::string step;
    while (std::getline(steps, step, ';')) {
        if (step.empty()) continue;
        if (step.rfind("menu:", 0) == 0) {
            game.debugOpenMenu(step.substr(5));
            game.render();   // first frame of the screen
            if (step.substr(5) == "main") game.render();
        } else if (step.rfind("select:", 0) == 0) {
            game.debugMenuSelect(step.substr(7));
            game.render();
        } else if (step.rfind("game:", 0) == 0) {
            // Run the real frame path (update + render) for N frames.
            game.debugRunFrames(std::atoi(step.c_str() + 5));
        } else if (step == "dump") {
            game.debugMenuDump();
        } else if (step == "shot") {
            game.render();
        } else {
            game.debugMenuKey(step);
            game.render();
            // Let the game loop build the page this key opens before the next
            // step acts on it (a real player cannot press faster either).
            struct timespec ts = {0, 60000000};
            nanosleep(&ts, nullptr);
        }
        std::cout << "step " << step << std::endl;
    }
    game.debugMenuDump();
    std::cout << "ready" << std::endl;
    std::cout.flush();

    // Keep the last frame on screen so an external screenshot can capture it.
    const double holdUntil = glfwGetTime() + 4.0;
    while (glfwGetTime() < holdUntil) {
        game.render();
        glfwPollEvents();
        struct timespec ts = {0, 16000000};
        nanosleep(&ts, nullptr);
    }
}

int main() {
    Game game(800, 600);
    game.init();

    if (const char* script = std::getenv("CUBE_MENU_TEST")) {
        runMenuTest(game, script);
        // The Game lives until main() returns, i.e. until after glfwTerminate()
        // has run; shut the sound device down before that so its mixing
        // thread is joined while everything it touches still exists.
        game.cleanup();
        return 0;
    }

    float lastFrame = glfwGetTime();
    while (!game.shouldClose()) {
        float currentFrame = glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        if (deltaTime > 0.05f) deltaTime = 0.05f; // Cap at 50ms to avoid physics issues

        game.processInput(deltaTime);
        game.update(deltaTime); // Only update physics when playing
        game.render();
        game.frameWait();
        game.endFrame(deltaTime);
    }

    game.cleanup();
    return 0;
}