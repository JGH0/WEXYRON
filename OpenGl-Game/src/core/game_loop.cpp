#include "game.hpp"
#include "../ui/ui_helpers.hpp"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

void Game::update(float deltaTime) {
    if (gameState == PLAYING) {
        // Update network client
        if (networkClient && (networkClient->isConnected() || networkClient->isConnecting())) {
            networkClient->update();
        }
        // Update player physics (always run, client controls own movement in multiplayer)
        updatePlayerPhysics(deltaTime);

        // Send player input to server
        if (networkClient && networkClient->isConnected()) {
            float moveX = 0.0f, moveZ = 0.0f;
            if (glfwGetKey(window, keyBindings.forward) == GLFW_PRESS) {
                moveX += cos(glm::radians(camera.Yaw));
                moveZ += sin(glm::radians(camera.Yaw));
            }
            if (glfwGetKey(window, keyBindings.backward) == GLFW_PRESS) {
                moveX -= cos(glm::radians(camera.Yaw));
                moveZ -= sin(glm::radians(camera.Yaw));
            }
            if (glfwGetKey(window, keyBindings.left) == GLFW_PRESS) {
                moveX += cos(glm::radians(camera.Yaw - 90.0f));
                moveZ += sin(glm::radians(camera.Yaw - 90.0f));
            }
            if (glfwGetKey(window, keyBindings.right) == GLFW_PRESS) {
                moveX += cos(glm::radians(camera.Yaw + 90.0f));
                moveZ += sin(glm::radians(camera.Yaw + 90.0f));
            }

            bool jump  = glfwGetKey(window, keyBindings.jump) == GLFW_PRESS;
            bool shoot = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            // Send eye-level position for projectile spawning (playerPos + eye height)
            glm::vec3 eyePos = playerPos + glm::vec3(0.0f, 1.6f, 0.0f);
            networkClient->sendPlayerInput(moveX, moveZ, camera.Yaw, camera.Pitch, jump, shoot,
                                           static_cast<uint8_t>(currentWeapon),
                                           eyePos.x, eyePos.y, eyePos.z);
        }

        // Update weapon cooldowns
        if (currentWeaponInstance) {
            currentWeaponInstance->updateCooldown(deltaTime);
        }

        // Update projectiles
        updateProjectiles(deltaTime);

        // Update enemies
        updateEnemies(deltaTime);

        // Check enemy-player collision
        checkEnemyPlayerCollision(deltaTime);

        // Update bullet holes (decay lifetime)
        for (int i = 0; i < bulletHoleCount; i++) {
            if (bulletHoles[i].lifetime > 0.0f) {
                bulletHoles[i].lifetime -= deltaTime;
            }
        }

        // Update shooting cooldowns
        if (shootCooldown > 0.0f)    shootCooldown    -= deltaTime;
        if (pistolCooldown > 0.0f)   pistolCooldown   -= deltaTime;
        if (shotgunCooldown > 0.0f)  shotgunCooldown  -= deltaTime;
        if (grenadeCooldown > 0.0f)  grenadeCooldown  -= deltaTime;
        if (flashbangCooldown > 0.0f) flashbangCooldown -= deltaTime;

        // Update flashbang effect
        updateFlashbangEffect(deltaTime);

        // Update explosions
        updateExplosions(deltaTime);

        // Update damage effect
        if (damageEffectTime > 0.0f) {
            damageEffectTime -= deltaTime;
        }

        // PVP collision checks (only in multiplayer)
        if (isConnectedToServer()) {
            checkProjectilePlayerCollision();
            checkPlayerPlayerCollision();
        }
    }
}

void Game::render() {
    // Fullscreen rendering: no borders/letterboxing.
    int fbW = 800, fbH = 600;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = 800;
    if (fbH <= 0) fbH = 600;
    renderer.setViewport(0, 0, fbW, fbH);

    if (gameState == PLAYING) {
        renderer.clear();

        glm::mat4 view       = camera.GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom),
                                                (float)fbW / (float)fbH, 0.1f, 100.0f);
        renderer.setViewProjection(view, projection);

        // Render the map
        if (currentMap) {
            renderer.renderMap(currentMap);
        }

        // Render enemies
        renderEnemies();

        // Render explosions
        renderExplosions();

        // Render bullet holes
        renderBulletHoles();

        // Render projectiles (visible debug cubes)
        for (int i = 0; i < MAX_PROJECTILES; i++) {
            if (projectiles[i].active) {
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, projectiles[i].position);

                if (projectiles[i].type == 1) {
                    // Grenade
                    model = glm::scale(model, glm::vec3(0.2f, 0.2f, 0.2f));
                    renderer.drawCube(model, glm::vec3(0.0f, 1.0f, 0.0f)); // Green for grenades
                } else if (projectiles[i].type == 2) {
                    // Flashbang
                    model = glm::scale(model, glm::vec3(0.15f, 0.15f, 0.15f));
                    renderer.drawCube(model, glm::vec3(1.0f, 1.0f, 1.0f)); // White for flashbangs
                } else if (projectiles[i].type == 3) {
                    // Shotgun pellet
                    model = glm::scale(model, glm::vec3(0.08f, 0.08f, 0.08f));
                    renderer.drawCube(model, glm::vec3(1.0f, 0.5f, 0.0f)); // Orange for shotgun pellets
                } else {
                    // Pistol bullet
                    model = glm::scale(model, glm::vec3(0.1f, 0.1f, 0.1f));
                    renderer.drawCube(model, glm::vec3(1.0f, 1.0f, 0.0f)); // Yellow for pistol bullets
                }
            }
        }

        // Render remote players (multiplayer)
        if (multiplayerMode) {
            for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                if (remotePlayers[i].active && !remotePlayers[i].isDead) {
                    const float interpolationFactor = 0.2f;
                    glm::vec3 targetPos  = remotePlayers[i].position;
                    glm::vec3 currentPos = remotePlayers[i].renderPosition;

                    if (remotePlayers[i].renderPosition.x == 0 &&
                        remotePlayers[i].renderPosition.y == 0 &&
                        remotePlayers[i].renderPosition.z == 0) {
                        remotePlayers[i].renderPosition = targetPos;
                    } else {
                        remotePlayers[i].renderPosition += (targetPos - currentPos) * interpolationFactor;
                    }

                    glm::mat4 model = glm::mat4(1.0f);
                    model = glm::translate(model, remotePlayers[i].renderPosition);
                    model = glm::scale(model, glm::vec3(0.6f, 1.6f, 0.6f));
                    glm::vec3 color = (remotePlayers[i].id % 2 == 0)
                                    ? glm::vec3(1.0f, 0.0f, 0.0f)
                                    : glm::vec3(0.0f, 1.0f, 0.0f);
                    renderer.drawCube(model, color);
                }
            }
        }

        // Render crosshair
        renderCrosshair();

        // Render damage effect
        renderDamageEffect();

        // Render flashbang effect
        renderFlashbangEffect();

        glfwSwapBuffers(window);
        glfwPollEvents();

    } else if (gameState == DEAD) {
        renderer.clear();
        UIHelpers::setProjection(renderer, window);
        renderDeathScreen();
        glfwSwapBuffers(window);
        glfwPollEvents();

    } else if (gameState == MENU) {
        renderer.clear();
        UIHelpers::setProjection(renderer, window);
        renderMenu();
        glfwSwapBuffers(window);
        glfwPollEvents();

    } else if (gameState == SETTINGS) {
        renderer.clear();
        UIHelpers::setProjection(renderer, window);
        renderSettings();
        glfwSwapBuffers(window);
        glfwPollEvents();

    } else if (gameState == MAP_SELECTION) {
        renderer.clear();
        UIHelpers::setProjection(renderer, window);
        renderMapSelection();
        glfwSwapBuffers(window);
        glfwPollEvents();

    } else if (gameState == SERVER_CONNECT) {
        renderServerConnect();
        glfwSwapBuffers(window);
        glfwPollEvents();

    } else if (gameState == MAP_EDITOR) {
        renderEditor();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}

void Game::cleanup() {
    if (currentMap) {
        delete currentMap;
        currentMap = nullptr;
    }
}

bool Game::shouldClose() const {
    return glfwWindowShouldClose(window);
}