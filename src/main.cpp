/**
 * @file main.cpp
 * @brief Main entry point for the OpenGL 3D Game application
 * @author OpenGL Game Developer
 * @date 2025
 */

#include "core/game.hpp"
#include <iostream>

/**
 * @brief Main function that initializes and runs the game loop
 * 
 * Creates a Game instance with window dimensions 800x600, initializes it,
 * and runs the main game loop with delta time calculations.
 * 
 * @return int Returns 0 on successful execution
 */
int main() {
    Game game(800, 600);
    game.init();

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