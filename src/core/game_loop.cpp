#define GLFW_INCLUDE_NONE
#include "game.hpp"
#include "brand.hpp"
#include "discord_presence.hpp"
#include "../ui/ui_helpers.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>

void Game::update(float deltaTime) {
    // Discord Rich Presence: dispatch callbacks and publish the current
    // menu / match / editor state. No-op while Discord is closed.
    discord_presence::poll();
    updateDiscordPresence();

    // Swap in a background-loaded map as soon as it is ready (main thread).
    pollMapLoad();

    // Live settings: pick up edits to assets/settings.cfg without a restart.
    pollSettingsFile();
    refreshFrameGenStatus();

    // Network session: runs in every state, so the handshake completes while
    // the connect screen is open (the status there must turn CONNECTED) and a
    // dropped server is noticed from the menus too.
    updateNetwork();

    // Audio first: the listener follows the camera from last frame, then the
    // gameplay below may start sounds that are mixed this frame.
    updateAudio(deltaTime);

    // Sniper scope: the held SCOPE binding zooms while the sniper is held. Runs in
    // every state so the FOV also returns to normal after death or a menu.
    updateScope(deltaTime);
    updateSniperBolt(deltaTime);

    if (gameState == PLAYING) {
        // Deathmatch and team deathmatch have no round cycle; the HUD shows
        // elapsed match time. Bomb defusal advances its own clock in updateBomb().
        if (currentGameMode != BOMB_DEFUSAL) roundTime += deltaTime;

        const bool spectating = spectatorMode && isDead;
        // Round-end screen: the body is frozen (no movement, jump or shot).
        const bool roundIntermission =
            matchOver_ || (roundEndTimer > 0.0f && currentGameMode == BOMB_DEFUSAL);

        if (spectating) {
            // Dead players fly freely (map-editor style) instead of running
            // gravity/collision on a body that is no longer in the round.
            updateSpectator(deltaTime);
        } else {
            if (roundIntermission) {
                // CS-style round end: no shooting / planting, but the body
                // keeps walking around until the next round starts.
                isPlanting = false;
                isDefusing = false;
                plantProgress = 0.0f;
                defuseProgress = 0.0f;
            }
            // Update player physics (always run, client controls own movement in multiplayer)
            updatePlayerPhysics(deltaTime);

            // Footsteps / landing (needs the velocity physics just produced)
            updateFootsteps(deltaTime);
        }

        // Update weapon cooldowns
        if (currentWeaponInstance) {
            currentWeaponInstance->updateCooldown(deltaTime);
        }

        // Single-player bot AI runs before projectiles so a shot fired this
        // frame can already collide with the body's new position.
        if (localBots_ && !matchOver_) updateLocalBots(deltaTime);

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

        // Weapon bob / sway / recoil animation (kept running between rounds so
        // the gun settles instead of freezing mid-kick).
        updateWeaponViewmodel(deltaTime);

        // Update damage effect
        if (damageEffectTime > 0.0f) {
            damageEffectTime -= deltaTime;
        }

        // Update bomb in bomb defusal mode (the winner is already decided
        // while the round-end screen is showing).
        if (!roundIntermission) updateBomb(deltaTime);

    } else if (gameState == DEAD) {
        // Update spectator camera in dead state
        if (spectatorMode) {
            updateSpectator(deltaTime);
        }
    }

    // Tell the server every frame whether we are alive or dead/spectating.
    // This runs in DEAD too, so a lost damage packet cannot leave a client
    // dead locally while the server still believes it is alive (or the other
    // way round): the next input corrects it. Menu states are not gameplay:
    // they must not report a living player as dead.
    if (networkClient && networkClient->isConnected() &&
        (gameState == PLAYING || gameState == DEAD)) {
        const bool alive = (gameState == PLAYING) && !isDead && !spectatorMode;
        const bool roundFrozen =
            matchOver_ || (roundEndTimer > 0.0f && currentGameMode == BOMB_DEFUSAL);
        const bool interacting = isPlanting || isDefusing;
        float moveX = 0.0f, moveZ = 0.0f;
        if (alive && !interacting) {
            if (keyDown(keyBindings.forward)) {
                moveX += cos(glm::radians(camera.Yaw));
                moveZ += sin(glm::radians(camera.Yaw));
            }
            if (keyDown(keyBindings.backward)) {
                moveX -= cos(glm::radians(camera.Yaw));
                moveZ -= sin(glm::radians(camera.Yaw));
            }
            if (keyDown(keyBindings.left)) {
                moveX += cos(glm::radians(camera.Yaw - 90.0f));
                moveZ += sin(glm::radians(camera.Yaw - 90.0f));
            }
            if (keyDown(keyBindings.right)) {
                moveX += cos(glm::radians(camera.Yaw + 90.0f));
                moveZ += sin(glm::radians(camera.Yaw + 90.0f));
            }
        }

        const bool jump  = alive && !interacting &&
                           keyDown(keyBindings.jump);
        const bool shoot = alive && !roundFrozen && !interacting &&
                           bindingDown(gameSettings_.bindShoot);

        // Send eye-level position for projectile spawning (playerPos + eye height)
        const glm::vec3 eyePos = playerPos + glm::vec3(0.0f, currentEyeHeight(), 0.0f);
        networkClient->sendPlayerInput(moveX, moveZ, camera.Yaw, camera.Pitch, jump, shoot,
                                       static_cast<uint8_t>(currentWeapon),
                                       eyePos.x, eyePos.y, eyePos.z,
                                       alive,
                                       static_cast<uint8_t>(currentGameMode),
                                       static_cast<uint8_t>(matchFormat_),
                                       static_cast<uint8_t>(stance_));
    }

    // Update round end timer (delay before next round)
    updateRoundEndTimer(deltaTime);

    // Update explosions (always run)
    updateExplosions(deltaTime);

    // Remote-body bookkeeping: networked players or offline bots, whichever
    // currently owns the remote-player slots.
    if (isConnectedToServer() || localBots_) {
        checkPlayerPlayerCollision();
        updateRemotePlayerDeath(deltaTime);
        updateRemoteAnimations(deltaTime);
    }
}

void Game::frameWait() {
    // When vsync is on, presenting already paces the frame; adding our own
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
        // The far plane follows the loaded map: the large maps are wider than
        // the old fixed 100 units, and the opposite corner must stay visible
        // instead of being clipped away.
        float farPlane = 100.0f;
        if (currentMap) {
            farPlane = std::max(farPlane, glm::length(currentMap->getDimensions()) * 1.5f);
        }
        glm::mat4 view       = camera.GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom),
                                                (float)fbW / (float)fbH, 0.1f, farPlane);
        renderer.setCamera(view, projection);

        // Map geometry is persistent in the WORLD_OPAQUE batch and is drawn
        // once by endFrame(). Do not draw the map here as well: the frame is
        // still open, so a second draw (with another camera position) showed
        // the world twice ("two worlds" / doubled seams).
        // Bomb sites, enemies, projectiles are queued right below; the draw
        // itself happens once in endFrame().

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
                } else if (projectiles[i].type == 4) {
                    // Sniper round: small and pale blue, like a tracer.
                    model = glm::scale(model, glm::vec3(0.07f, 0.07f, 0.07f));
                    renderer.drawCube(model, glm::vec3(0.55f, 0.80f, 1.0f));
                } else {
                    // Pistol bullet
                    model = glm::scale(model, glm::vec3(0.1f, 0.1f, 0.1f));
                    renderer.drawCube(model, glm::vec3(1.0f, 1.0f, 0.0f)); // Yellow for pistol bullets
                }
            }
        }

        // Render remote players (multiplayer) or single-player bots.
        if (multiplayerMode || localBots_) {
            renderRemotePlayers();
        }

        // First-person weapon (its own pass, after the world). The viewmodel is
        // hidden while looking through the scope.
        if (!spectatorMode && !isDead && !sniperScoped_) renderWeaponViewmodel();

        // Render crosshair, or the scope overlay while aiming the sniper.
        if (!spectatorMode) {
            if (sniperScoped_) {
                renderScopeOverlay();
            } else {
                renderCrosshair();
            }
        }

        // Render damage effect
        renderDamageEffect();

        // Render flashbang effect
        renderFlashbangEffect();

        // Always-on gameplay HUD (health, weapon/ammo, round, K/D)
        renderGameHUD();

        // Render bomb defusal HUD (team, bomb timer, progress)
        renderBombDefusalHUD();

        // A playtest is a temporary visit to the editor map: say how to leave.
        if (editorPlaytesting_) {
            UIHelpers::setProjection(renderer, window);
            const UIHelpers::UIBounds b = UIHelpers::getBounds(window);
            renderer.drawTextFit("PLAYTEST  -  F5 / ESC  RETURN TO EDITOR",
                                 b.left + 12.0f, b.bottom - 28.0f,
                                 b.width() - 24.0f, 0.34f);
        }

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
               gameState == MAP_SELECTION || gameState == SERVER_CONNECT ||
               gameState == TEAM_SETUP) {
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
    renderer.present();
    glfwPollEvents();
}

// ---------------------------------------------------------------------------
// Discord Rich Presence
// ---------------------------------------------------------------------------
void Game::updateDiscordPresence() {
    if (!discord_presence::isEnabled()) return;

    std::string mapName;
    if (!activeMapFile_.empty()) {
        mapName = formatMapDisplayName(activeMapFile_);
    } else if (currentMap && !currentMap->getMapName().empty()) {
        mapName = currentMap->getMapName();
    } else {
        mapName = "UNKNOWN";
    }

    std::string modeText = "Deathmatch";
    std::string modeKey = "deathmatch";
    switch (currentGameMode) {
        case TEAM_VS_TEAM:
            modeText = "Team Deathmatch";
            modeKey = "team";
            break;
        case BOMB_DEFUSAL:
            modeText = "Bomb Defusal";
            modeKey = "bomb";
            break;
        case DEATHMATCH:
        default:
            break;
    }

    std::string details;
    std::string state;
    std::string smallImage = "menu";
    std::string smallText = "Main Menu";

    switch (gameState) {
        case PLAYING:
            details = "Playing " + modeText;
            state = "Map: " + mapName;
            if (multiplayerMode && playerCount > 1) {
                state += " | " + std::to_string(playerCount) + " players";
            }
            smallImage = modeKey;
            smallText = modeText;
            break;
        case DEAD:
            details = "Eliminated - waiting to respawn";
            state = "Map: " + mapName;
            smallImage = modeKey;
            smallText = modeText;
            break;
        case MAP_EDITOR:
            details = "Building a map";
            state = "Editing: " + (editorMapName.empty() ? std::string("UNTITLED")
                                                         : editorMapName);
            smallImage = "editor";
            smallText = "Map Editor";
            break;
        case SERVER_CONNECT:
            details = "Looking for a server";
            state = "Multiplayer";
            smallImage = "menu";
            smallText = "Main Menu";
            break;
        case MENU:
        case SETTINGS:
        case MAP_SELECTION:
        default:
            details = "In the main menu";
            state = brand::kShortName;
            smallImage = "menu";
            smallText = "Main Menu";
            break;
    }

    discord_presence::setActivity(details, state, brand::kDiscordLargeImage,
                                  brand::kShortName, smallImage, smallText);
}

void Game::cleanup() {
    // currentMap is cleaned up in ~Game(); avoid double-delete here.
    discord_presence::shutdown();
    audio.shutdown();
}

bool Game::shouldClose() const {
    return glfwWindowShouldClose(window);
}