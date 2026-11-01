#define GLFW_INCLUDE_NONE
#include "game.hpp"
#include "../ui/ui_helpers.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>

void Game::update(float deltaTime) {
    // Swap in a background-loaded map as soon as it is ready (main thread).
    pollMapLoad();

    // Live settings: pick up edits to assets/settings.cfg without a restart.
    pollSettingsFile();

    // Network session: runs in every state, so the handshake completes while
    // the connect screen is open (the status there must turn CONNECTED) and a
    // dropped server is noticed from the menus too.
    updateNetwork();

    // Audio first: the listener follows the camera from last frame, then the
    // gameplay below may start sounds that are mixed this frame.
    updateAudio(deltaTime);

    if (gameState == PLAYING) {
        // Update player physics (always run, client controls own movement in multiplayer)
        updatePlayerPhysics(deltaTime);

        // Footsteps / landing (needs the velocity physics just produced)
        updateFootsteps(deltaTime);

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

        // Update damage effect
        if (damageEffectTime > 0.0f) {
            damageEffectTime -= deltaTime;
        }

        // Update bomb in bomb defusal mode
        updateBomb(deltaTime);

        // Update spectator camera
        if (spectatorMode) {
            updateSpectator();
        }

    } else if (gameState == DEAD) {
        // Update spectator camera in dead state
        if (spectatorMode) {
            updateSpectator();
        }
    }

    // Update round end timer (delay before next round)
    updateRoundEndTimer(deltaTime);

    // Update explosions (always run)
    updateExplosions(deltaTime);

    // PVP collision checks (only in multiplayer)
    if (isConnectedToServer()) {
        checkProjectilePlayerCollision();
        checkPlayerPlayerCollision();
    }
}

void Game::frameWait() {
    // When vsync is on, glfwSwapBuffers already paces the frame; adding our own
    // wait on top would only add latency. Otherwise sleep instead of spinning
    // so other threads (network, loaders) keep getting CPU time.
    if (maxFps <= 0 || vsyncEnabled) return;
    const double targetTime = glfwGetTime() + 1.0 / static_cast<double>(maxFps);
    double remaining = targetTime - glfwGetTime();
    while (remaining > 0.0) {
        if (remaining > 0.002) {
            // Leave ~2 ms for the busy check; sleeping precisely avoids burning a core.
            auto sleepMs = std::chrono::milliseconds(static_cast<long long>((remaining - 0.002) * 1000.0));
            if (sleepMs.count() > 0) std::this_thread::sleep_for(sleepMs);
        }
        remaining = targetTime - glfwGetTime();
    }
}

void Game::endFrame(float deltaTime) {
    fpsFrameCount++;
    fpsTimer += deltaTime;
    if (fpsTimer >= 1.0f) {
        fpsDisplayValue = fpsFrameCount;
        fpsFrameCount = 0;
        fpsTimer -= 1.0f;
    }
}

void Game::render() {
    // Fullscreen rendering: no borders/letterboxing.
    int fbW = 800, fbH = 600;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = 800;
    if (fbH <= 0) fbH = 600;
    renderer.setViewport(0, 0, fbW, fbH);

    renderer.beginFrame();

    if (gameState == PLAYING) {
        glm::mat4 view       = camera.GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom),
                                                (float)fbW / (float)fbH, 0.1f, 100.0f);
        renderer.setCamera(view, projection);

        // Static map geometry lives in a persistent GPU buffer; this is one
        // instanced draw call, not one per block.
        if (currentMap) {
            renderer.renderWorld();
        }

        // Render bomb sites and bomb
        renderBombSites();

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

        // Render health-based red screen effect
        renderHealthEffect();

        // Render flashbang effect
        renderFlashbangEffect();

        // Render bomb defusal HUD (team, bomb timer, progress)
        renderBombDefusalHUD();

        // FPS counter (only when enabled)
        if (showFps) {
            char fpsBuf[32];
            snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %d", fpsDisplayValue);
            renderer.drawText(fpsBuf, 4.0f, 8.0f, 0.46f);
        }

    } else if (gameState == DEAD) {
        UIHelpers::setProjection(renderer, window);
        renderDeathScreen();
        if (showFps) {
            char fpsBuf[32];
            snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %d", fpsDisplayValue);
            renderer.drawText(fpsBuf, 4.0f, 8.0f, 0.46f);
        }

    } else if (gameState == MENU || gameState == SETTINGS ||
               gameState == MAP_SELECTION || gameState == SERVER_CONNECT) {
        // All menu screens are drawn by the menu framework (ui/menu.hpp);
        // the view decides title, rows and footer.
        UIHelpers::setProjection(renderer, window);
        if (gameState == SERVER_CONNECT) {
            renderServerConnect();
        } else {
            menu();
        }
        if (showFps) {
            char fpsBuf[32];
            snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %d", fpsDisplayValue);
            renderer.drawText(fpsBuf, 4.0f, 8.0f, 0.46f);
        }

    } else if (gameState == MAP_EDITOR) {
        renderEditor();
    }

    // Flush all queued geometry as a handful of instanced draw calls, then
    // present. This is the only swap/poll in the frame.
    renderer.endFrame();
    glfwSwapBuffers(window);
    glfwPollEvents();
}

void Game::cleanup() {
    // currentMap is cleaned up in ~Game(); avoid double-delete here.
    audio.shutdown();
}

bool Game::shouldClose() const {
    return glfwWindowShouldClose(window);
}