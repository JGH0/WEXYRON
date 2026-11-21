#include "game.hpp"
#include "../ui/ui_helpers.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <GLFW/glfw3.h>

void Game::renderFlashbangEffect() {
    if (isFlashed && flashEffectTime > 0.0f) {
        // Full-screen white flash that fades out as the effect wears off.
        float alpha = flashEffectTime / 3.0f;
        if (alpha > 1.0f) alpha = 1.0f;
        if (alpha < 0.0f) alpha = 0.0f;

        if (alpha > 0.0f) {
            UIHelpers::setProjection(renderer, window);
            renderer.submitOverlay(glm::vec3(1.0f), alpha);
        }
    }
}

void Game::renderEnemies() {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        // Dead bodies stay for the collapse animation (and a moment afterwards).
        if (!enemies[i].active) continue;
        // Practice targets use the same blocky body as remote players.
        // Their health is 50, so it drives the :) -> :| -> :( face too.
        const float healthPct = enemies[i].alive
                                    ? glm::clamp(enemies[i].health / 50.0f, 0.0f, 1.0f)
                                    : 0.0f;
        if (playerModel_ && enemyModelAnims[i].valid()) {
            posePlayerInstance(enemyModelAnims[i], enemies[i].anim);
            renderer.submitModel(
                playerModel_.get(), &enemyModelAnims[i],
                humanoid::rootTransform(glm::vec3(enemies[i].x, enemies[i].y, enemies[i].z),
                                        enemies[i].yaw),
                glm::vec3(0.95f, 0.18f, 0.15f));
        } else {
            humanoid::render(renderer,
                             glm::vec3(enemies[i].x, enemies[i].y, enemies[i].z),
                             enemies[i].yaw,
                             glm::vec3(0.95f, 0.18f, 0.15f),
                             healthPct, enemies[i].anim, 0.0f, enemies[i].alive);
        }
    }
}

void Game::renderCrosshair() {
    UIHelpers::setProjection(renderer, window);
    
    // Draw crosshair in center of screen
    float crosshairSize = 25.0f;
    float centerX = 400.0f; // Half of window width
    float centerY = 300.0f; // Half of window height
    float thickness = 3.0f; // Thicker for better visibility
    
    // Horizontal line
    glm::mat4 horizontalModel = glm::mat4(1.0f);
    horizontalModel = glm::translate(horizontalModel, glm::vec3(centerX, centerY, 0.0f));
    horizontalModel = glm::scale(horizontalModel, glm::vec3(crosshairSize, thickness, 1.0f));
    renderer.drawColoredQuad(horizontalModel, glm::vec3(1.0f, 1.0f, 1.0f)); // White
    
    // Vertical line
    glm::mat4 verticalModel = glm::mat4(1.0f);
    verticalModel = glm::translate(verticalModel, glm::vec3(centerX, centerY, 0.0f));
    verticalModel = glm::scale(verticalModel, glm::vec3(thickness, crosshairSize, 1.0f));
    renderer.drawColoredQuad(verticalModel, glm::vec3(1.0f, 1.0f, 1.0f)); // White
}

void Game::renderScopeOverlay() {
    if (!window || !sniperScoped_) return;

    UIHelpers::setProjection(renderer, window);
    const UIHelpers::UIBounds b = UIHelpers::getBounds(window);
    const float cx = b.centerX();
    const float cy = b.centerY();
    // Round lens in the 800x600 design canvas. The world outside the circle is
    // masked by vertical strips, which approximates a circle with quads.
    const float radius = 245.0f;

    auto quad = [&](float centerX, float centerY, float w, float h,
                    const glm::vec3& color) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f),
                                     glm::vec3(centerX, centerY, 0.0f));
        m = glm::scale(m, glm::vec3(w, h, 1.0f));
        renderer.drawColoredQuad(m, color);
    };

    const glm::vec3 black(0.0f, 0.0f, 0.0f);
    const glm::vec3 ring(0.20f, 0.22f, 0.24f);
    const glm::vec3 reticle(0.20f, 0.20f, 0.20f);
    const glm::vec3 dot(0.85f, 0.18f, 0.12f);

    const int strips = 96;
    const float stripW = b.width() / static_cast<float>(strips) + 1.0f;
    for (int i = 0; i < strips; ++i) {
        const float x = b.left + (static_cast<float>(i) + 0.5f) * b.width() /
                                              static_cast<float>(strips);
        const float dx = x - cx;
        const float half = std::abs(dx) < radius
                               ? std::sqrt(radius * radius - dx * dx)
                               : 0.0f;

        const float topH = (cy - half) - b.top;
        if (topH > 0.0f) {
            quad(x, b.top + topH * 0.5f, stripW, topH, black);
        }
        const float bottomY = cy + half;
        const float bottomH = b.bottom - bottomY;
        if (bottomH > 0.0f) {
            quad(x, bottomY + bottomH * 0.5f, stripW, bottomH, black);
        }
        if (half > 0.0f) {
            // Thin ring right at the lens edge.
            quad(x, cy - half - 1.0f, stripW, 2.0f, ring);
            quad(x, cy + half + 1.0f, stripW, 2.0f, ring);
        }
    }

    // Reticle: a cross with a clear centre gap and mil-dot ticks.
    const float gap = 13.0f;
    const float armX = radius - 18.0f;
    const float armY = radius * 0.62f;
    quad(cx - (gap + armX) * 0.5f, cy, armX - gap, 1.5f, reticle);
    quad(cx + (gap + armX) * 0.5f, cy, armX - gap, 1.5f, reticle);
    quad(cx, cy - (gap + armY) * 0.5f, 1.5f, armY - gap, reticle);
    quad(cx, cy + (gap + armY) * 0.5f, 1.5f, armY - gap, reticle);

    for (float offset : {50.0f, 100.0f, 150.0f, 200.0f}) {
        quad(cx + offset, cy, 2.0f, 8.0f, reticle);
        quad(cx - offset, cy, 2.0f, 8.0f, reticle);
    }
    for (float offset : {50.0f, 100.0f, 150.0f}) {
        quad(cx, cy + offset, 8.0f, 2.0f, reticle);
        quad(cx, cy - offset, 8.0f, 2.0f, reticle);
    }

    // Centre dot.
    quad(cx, cy, 3.5f, 3.5f, dot);
}

void Game::renderDamageEffect() {
    if (damageEffectTime > 0.0f) {
        UIHelpers::setProjection(renderer, window);

        // A short red flash over the whole screen each time the player is hit;
        // it fades out over the effect's lifetime.
        const float duration = 0.5f;
        float alpha = damageEffectTime / duration;
        if (alpha > 1.0f) alpha = 1.0f;
        if (alpha < 0.0f) alpha = 0.0f;

        renderer.submitOverlay(glm::vec3(1.0f, 0.0f, 0.0f), alpha * 0.55f);
    }
}

void Game::renderHealthEffect() {
    // Low health: a red pulse over the whole screen that gets stronger and
    // faster the lower the player's health.
    if (playerHealth < playerMaxHealth && playerHealth > 0.0f && window) {
        UIHelpers::setProjection(renderer, window);

        const float healthPercent = playerHealth / playerMaxHealth;
        const float missing = 1.0f - healthPercent;   // 0 = full health, 1 = dead

        // Peak opacity grows with the damage taken (up to ~0.35, so the screen
        // stays readable at low health).
        const float intensity = 0.35f * std::pow(missing, 1.3f);
        // The pulse starts at ~2 Hz and speeds up towards ~7 Hz near death.
        const float pulseHz = 2.0f + 5.0f * missing;
        const float phase = static_cast<float>(glfwGetTime()) * pulseHz * 6.2831853f;
        const float pulse = 0.5f + 0.5f * std::sin(phase);   // 0..1

        // Start from a low baseline so the red never disappears completely.
        const float alpha = intensity * (0.45f + 0.55f * pulse);
        if (alpha > 0.0f) renderer.submitOverlay(glm::vec3(1.0f, 0.0f, 0.0f), alpha);
    }
}

void Game::renderDeathScreen() {
    UIHelpers::setProjection(renderer, window);
    
    auto _b = UIHelpers::getBounds(window);
    float centerX = _b.centerX();
    float centerY = _b.centerY();
    // Strong red background, then the label on top. Fit it to 80% of the
    // viewport: the old scale 3.0 text was wider than the screen.
    renderer.submitOverlay(glm::vec3(0.8f, 0.0f, 0.0f), 1.0f);
    const float maxWidth = (_b.right - _b.left) * 0.8f;
    renderer.drawTextCenteredColored("YOU DIED", centerX, centerY - 20.0f,
                                     1.4f, glm::vec3(1.0f), maxWidth);
}

void Game::renderBombSites() {
    if (!bombDefusalMode) return;
    
    double currentTime = glfwGetTime();
    
    // Render bomb sites (subtle ground markers only)
    for (int i = 0; i < activeBombSites; i++) {
        if (bombSites[i].active) {
            glm::vec3 sitePos = bombSites[i].position;
            
            // Colors for A (gold) and B (orange)
            bool isSiteA = (bombSites[i].label == 'A');
            glm::vec3 siteColor = isSiteA ? glm::vec3(1.0f, 0.85f, 0.0f) : glm::vec3(1.0f, 0.5f, 0.0f);
            
            // Pulsing ground ring
            float pulse = static_cast<float>(sin(currentTime * 1.5f + i * 2.0f)) * 0.1f + 1.0f;
            glm::mat4 groundRing = glm::mat4(1.0f);
            groundRing = glm::translate(groundRing, sitePos + glm::vec3(0.0f, 0.03f, 0.0f));
            groundRing = glm::scale(groundRing, glm::vec3(2.2f * pulse, 0.06f, 2.2f * pulse));
            renderer.drawCube(groundRing, siteColor * 0.5f);
            
            // Center marker dot
            glm::mat4 centerDot = glm::mat4(1.0f);
            centerDot = glm::translate(centerDot, sitePos + glm::vec3(0.0f, 0.08f, 0.0f));
            centerDot = glm::scale(centerDot, glm::vec3(0.4f, 0.04f, 0.4f));
            renderer.drawCube(centerDot, siteColor);
        }
    }
    
    // Render planted bomb (BIG and prominent). `active` is false once the
    // bomb exploded or was defused, so the model disappears immediately
    // instead of lingering through the round-end window.
    if (bomb.planted && !bomb.defused && bomb.active) {
        glm::vec3 bombPos = bomb.position;
        float bombTimeLeft = bomb.explodeTime - bomb.plantTime;
        
        // Glow aura on ground beneath bomb
        float auraSize = 0.8f + static_cast<float>(sin(currentTime * 3.0f) * 0.15f);
        glm::mat4 bombAura = glm::mat4(1.0f);
        bombAura = glm::translate(bombAura, bombPos + glm::vec3(0.0f, 0.02f, 0.0f));
        bombAura = glm::scale(bombAura, glm::vec3(auraSize, 0.03f, auraSize));
        renderer.drawCube(bombAura, glm::vec3(1.0f, 0.0f, 0.0f)); // Red glow
        
        // Second outer aura ring
        glm::mat4 bombAura2 = glm::mat4(1.0f);
        bombAura2 = glm::translate(bombAura2, bombPos + glm::vec3(0.0f, 0.01f, 0.0f));
        bombAura2 = glm::scale(bombAura2, glm::vec3(auraSize * 1.5f, 0.02f, auraSize * 1.5f));
        renderer.drawCube(bombAura2, glm::vec3(1.0f, 0.3f, 0.0f) * 0.4f); // Dim orange outer
        
        // Bomb main body, dome and blinking light come from the glTF prop.
        if (bombModel_ && bombModelAnim_.valid()) {
            const float blinkSpeed = (bombTimeLeft < 5.0f) ? 1.8f : 0.9f;
            const float blinkTime = std::fmod(static_cast<float>(currentTime) * blinkSpeed,
                                              1.0f);
            bombModelAnim_.setPose(bombBlinkClip_, blinkTime, 1.0f);
            bombModelAnim_.update();
            renderer.submitModel(bombModel_.get(), &bombModelAnim_,
                                 glm::translate(glm::mat4(1.0f), bombPos),
                                 glm::vec3(1.0f));
        } else {
            // Bomb main body (bigger cube)
            glm::mat4 bombBody = glm::mat4(1.0f);
            bombBody = glm::translate(bombBody, bombPos + glm::vec3(0.0f, 0.15f, 0.0f));
            bombBody = glm::scale(bombBody, glm::vec3(0.6f, 0.3f, 0.6f));
            renderer.drawCube(bombBody, glm::vec3(0.3f, 0.3f, 0.25f)); // Dark green/grey

            // Bomb top dome
            glm::mat4 bombDome = glm::mat4(1.0f);
            bombDome = glm::translate(bombDome, bombPos + glm::vec3(0.0f, 0.35f, 0.0f));
            bombDome = glm::scale(bombDome, glm::vec3(0.4f, 0.15f, 0.4f));
            renderer.drawCube(bombDome, glm::vec3(0.5f, 0.5f, 0.4f)); // Light grey top

            // Blinking red light on top of bomb
            const float blinkSpeed = (bombTimeLeft < 5.0f) ? 10.0f : 6.0f;
            const float blink = static_cast<float>((sin(currentTime * blinkSpeed) + 1.0f) * 0.5f);
            glm::mat4 lightMarker = glm::mat4(1.0f);
            lightMarker = glm::translate(lightMarker, bombPos + glm::vec3(0.0f, 0.45f, 0.0f));
            lightMarker = glm::scale(lightMarker, glm::vec3(0.15f, 0.08f, 0.15f));
            renderer.drawCube(lightMarker, glm::vec3(1.0f, blink * 0.2f, blink * 0.2f));
        }
        
        // Siren effect: spinning red particles around the bomb
        float sirenAngle = static_cast<float>(currentTime * 4.0f);
        for (int s = 0; s < 3; s++) {
            float sa = sirenAngle + s * 2.09f;
            glm::mat4 siren = glm::mat4(1.0f);
            siren = glm::translate(siren, bombPos + glm::vec3(cos(sa) * 0.5f, 0.3f, sin(sa) * 0.5f));
            siren = glm::scale(siren, glm::vec3(0.06f, 0.06f, 0.06f));
            renderer.drawCube(siren, glm::vec3(1.0f, 0.1f, 0.1f));
        }
        for (int s = 0; s < 3; s++) {
            float sa = -sirenAngle + s * 2.09f;
            glm::mat4 siren = glm::mat4(1.0f);
            siren = glm::translate(siren, bombPos + glm::vec3(cos(sa) * 0.5f, 0.8f, sin(sa) * 0.5f));
            siren = glm::scale(siren, glm::vec3(0.06f, 0.06f, 0.06f));
            renderer.drawCube(siren, glm::vec3(1.0f, 0.2f, 0.0f));
        }
    }
}

// ---------------------------------------------------------------------------
// Always-on gameplay HUD
// ---------------------------------------------------------------------------
void Game::renderGameHUD() {
    if (!window) return;

    UIHelpers::setProjection(renderer, window);
    const UIHelpers::UIBounds b = UIHelpers::getBounds(window);
    const float left = b.left, right = b.right, top = b.top, bottom = b.bottom;

    // Solid (not blended) panels: the UI compositor paints opaque elements
    // before translucent ones, so a translucent panel would cover the text
    // submitted after it. Dark solid panels keep every label readable.
    auto panel = [&](float cx, float cy, float w, float h, const glm::vec3& color) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(cx, cy, 0.0f));
        m = glm::scale(m, glm::vec3(w, h, 1.0f));
        renderer.drawColoredQuad(m, color);
    };
    auto textRight = [&](const std::string& text, float rightX, float y,
                         float scale, const glm::vec3& color) {
        renderer.drawTextColored(text, rightX - renderer.getTextWidth(text, scale),
                                 y, scale, color);
    };

    const glm::vec3 kPanel(0.045f, 0.055f, 0.070f);
    const glm::vec3 kPanelLine(0.12f, 0.14f, 0.17f);
    const glm::vec3 kLabel(0.72f, 0.76f, 0.82f);
    const glm::vec3 kGold(1.0f, 0.82f, 0.30f);
    const glm::vec3 kGreen(0.25f, 0.90f, 0.35f);
    const glm::vec3 kRed(0.95f, 0.22f, 0.22f);

    // ── Health (bottom-left) ────────────────────────────────────────────────
    const float healthPct = glm::clamp(
        playerMaxHealth > 0.0f ? playerHealth / playerMaxHealth : 0.0f, 0.0f, 1.0f);
    glm::vec3 healthColor = (healthPct > 0.5f)
        ? glm::mix(glm::vec3(1.0f, 0.80f, 0.10f), kGreen, (healthPct - 0.5f) * 2.0f)
        : glm::mix(kRed, glm::vec3(1.0f, 0.80f, 0.10f), healthPct * 2.0f);

    const float healthW = 220.0f, healthH = 66.0f;
    const float healthX = left + 14.0f;
    const float healthY = bottom - healthH - 14.0f;
    panel(healthX + healthW * 0.5f, healthY + healthH * 0.5f, healthW, healthH, kPanel);
    panel(healthX + healthW * 0.5f, healthY + 1.0f, healthW, 2.0f, healthColor * 0.9f);

    // Small cross icon next to the label.
    panel(healthX + 19.0f, healthY + 17.0f, 13.0f, 4.0f, healthColor);
    panel(healthX + 19.0f, healthY + 17.0f, 4.0f, 13.0f, healthColor);

    renderer.drawTextColored("HEALTH", healthX + 34.0f, healthY + 8.0f, 0.34f, kLabel);
    char healthBuf[24];
    snprintf(healthBuf, sizeof(healthBuf), "%d", static_cast<int>(std::ceil(playerHealth)));
    textRight(healthBuf, healthX + healthW - 12.0f, healthY + 2.0f, 0.62f, healthColor);

    const float healthBarX = healthX + 10.0f;
    const float healthBarY = healthY + 40.0f;
    const float healthBarW = healthW - 20.0f;
    const float healthBarH = 14.0f;
    panel(healthBarX + healthBarW * 0.5f, healthBarY + healthBarH * 0.5f,
          healthBarW, healthBarH, glm::vec3(0.08f, 0.09f, 0.11f));
    if (healthPct > 0.0f) {
        const float fillW = (healthBarW - 4.0f) * healthPct;
        panel(healthBarX + 2.0f + fillW * 0.5f,
              healthBarY + 2.0f + (healthBarH - 4.0f) * 0.5f,
              fillW, healthBarH - 4.0f, healthColor);
    }

    // ── Weapon + ammo (bottom-right) ────────────────────────────────────────
    static const char* const kWeaponNames[5] = {"PISTOL", "SHOTGUN", "SNIPER", "GRENADE", "FLASHBANG"};
    static const char* const kSlotNames[5]   = {"PISTOL", "SHOTGUN", "SNIPER", "GRENADE", "FLASH"};
    const int weaponIndex = static_cast<int>(currentWeapon);

    // Three rows of two slots (the fifth slot sits alone in the last row).
    const float weaponW = 250.0f, weaponH = 132.0f;
    const float weaponX = right - weaponW - 14.0f;
    const float weaponY = bottom - weaponH - 14.0f;
    panel(weaponX + weaponW * 0.5f, weaponY + weaponH * 0.5f, weaponW, weaponH, kPanel);
    panel(weaponX + 1.0f, weaponY + weaponH * 0.5f, 2.0f, weaponH - 16.0f, kGold);

    renderer.drawTextFit(kWeaponNames[weaponIndex], weaponX + 12.0f, weaponY + 7.0f,
                         weaponW - 24.0f, 0.46f);

    std::string ammoStr;
    if (infiniteAmmoMode) {
        ammoStr = "INF";
    } else if (weaponIndex == static_cast<int>(PISTOL) ||
               weaponIndex == static_cast<int>(PUMP_SHOTGUN) ||
               weaponIndex == static_cast<int>(SNIPER)) {
        ammoStr = std::to_string(currentWeaponInstance ? currentWeaponInstance->getAmmo() : 0);
    } else {
        ammoStr = "x" + std::to_string(currentWeaponInstance ? currentWeaponInstance->getAmmo() : 0);
    }
    renderer.drawTextColored("AMMO", weaponX + 12.0f, weaponY + 33.0f, 0.32f, kLabel);
    textRight(ammoStr, weaponX + weaponW - 12.0f, weaponY + 23.0f, 0.90f, kGold);

    // 2 x 3 weapon slots (the keys 1-5 work exactly like this list).
    for (int i = 0; i < 5; ++i) {
        const float slotW = 112.0f, slotH = 18.0f;
        const float slotX = weaponX + 10.0f + static_cast<float>(i % 2) * 116.0f;
        const float slotY = weaponY + 68.0f + static_cast<float>(i / 2) * 20.0f;
        const bool selected = (i == weaponIndex);
        panel(slotX + slotW * 0.5f, slotY + slotH * 0.5f, slotW, slotH,
              selected ? glm::vec3(0.30f, 0.24f, 0.07f) : kPanelLine);
        if (selected) {
            panel(slotX + 2.0f, slotY + slotH * 0.5f, 3.0f, slotH - 4.0f, kGold);
        }
        char slotBuf[32];
        snprintf(slotBuf, sizeof(slotBuf), "%d %s", i + 1, kSlotNames[i]);
        renderer.drawTextFit(slotBuf, slotX + 8.0f, slotY + 4.0f, slotW - 14.0f,
                             selected ? 0.33f : 0.30f);
    }

    // ── Kills / deaths (top-right) ──────────────────────────────────────────
    const float statsW = 180.0f, statsH = 58.0f;
    const float statsX = right - statsW - 14.0f;
    const float statsY = top + 12.0f;
    panel(statsX + statsW * 0.5f, statsY + statsH * 0.5f, statsW, statsH, kPanel);
    panel(statsX + statsW * 0.5f, statsY + statsH - 1.0f, statsW, 2.0f,
          glm::vec3(0.18f, 0.20f, 0.24f));

    renderer.drawTextColored("KILLS", statsX + 12.0f, statsY + 7.0f, 0.30f, kLabel);
    renderer.drawTextColored("DEATHS", statsX + statsW * 0.5f + 8.0f,
                             statsY + 7.0f, 0.30f, kLabel);
    char killsBuf[16], deathsBuf[16];
    snprintf(killsBuf, sizeof(killsBuf), "%d", kills_);
    snprintf(deathsBuf, sizeof(deathsBuf), "%d", deaths_);
    renderer.drawTextColored(killsBuf, statsX + 12.0f, statsY + 22.0f, 0.75f, kGreen);
    renderer.drawTextColored(deathsBuf, statsX + statsW * 0.5f + 8.0f,
                             statsY + 22.0f, 0.75f, kRed);

    // ── Round details (top-centre) ──────────────────────────────────────────
    const bool roundBased = (currentGameMode == BOMB_DEFUSAL);
    const float roundW = 270.0f, roundH = 72.0f;
    const float roundX = (left + right) * 0.5f - roundW * 0.5f;
    const float roundY = top + 12.0f;
    panel(roundX + roundW * 0.5f, roundY + roundH * 0.5f, roundW, roundH, kPanel);
    panel(roundX + roundW * 0.5f, roundY + 1.0f, roundW, 2.0f,
          roundBased ? kGold * 0.8f : glm::vec3(0.35f, 0.55f, 0.85f));

    std::string header;
    std::string timerText;
    std::string detail;
    glm::vec3 timerColor(1.0f);

    if (roundBased) {
        header = "ROUND " + std::to_string(roundNumber_);

        if (bombDefusalMode && bomb.planted && !bomb.defused && bomb.active) {
            const int secs = static_cast<int>(std::ceil(
                std::max(0.0f, bomb.explodeTime - bomb.plantTime)));
            timerText = "BOMB " + std::to_string(secs);
            timerColor = (secs <= 5) ? kRed : glm::vec3(1.0f, 0.85f, 0.20f);
        } else {
            // The round clock counts down in both local and server play.
            const int totalSeconds =
                static_cast<int>(std::ceil(std::max(0.0f, roundTimeLeft)));
            char clock[16];
            snprintf(clock, sizeof(clock), "%02d:%02d",
                     totalSeconds / 60, totalSeconds % 60);
            timerText = clock;
            if (totalSeconds <= 10) {
                timerColor = kRed;
            }
        }

        const int roundTarget = matchRoundTarget();
        char scoreBuf[64];
        if (roundTarget > 0) {
            snprintf(scoreBuf, sizeof(scoreBuf), "A %d : %d B   FIRST TO %d",
                     teamAScore_, teamBScore_, roundTarget);
        } else {
            snprintf(scoreBuf, sizeof(scoreBuf), "A %d   :   %d B",
                     teamAScore_, teamBScore_);
        }
        detail = scoreBuf;
    } else {
        int totalSeconds = static_cast<int>(roundTime);
        char clock[16];
        snprintf(clock, sizeof(clock), "%02d:%02d",
                 totalSeconds / 60, totalSeconds % 60);
        const bool teamMode = (currentGameMode == TEAM_VS_TEAM);
        header = teamMode ? "TEAM DEATHMATCH" : "DEATHMATCH";
        timerText = clock;

        const int killTarget = matchKillTarget();
        if (teamMode) {
            char scoreBuf[64];
            if (killTarget > 0) {
                snprintf(scoreBuf, sizeof(scoreBuf), "A %d : %d B   FIRST TO %d",
                         teamAScore_, teamBScore_, killTarget);
            } else {
                snprintf(scoreBuf, sizeof(scoreBuf), "A %d   :   %d B",
                         teamAScore_, teamBScore_);
            }
            detail = scoreBuf;
        } else if (killTarget > 0) {
            detail = "YOUR KILLS " + std::to_string(kills_) + " / " +
                     std::to_string(killTarget);
        } else if (multiplayerMode) {
            detail = "PLAYERS " + std::to_string(std::max(1, playerCount));
        } else {
            detail = "SOLO PRACTICE";
        }
    }

    renderer.drawTextCentered(header, roundX + roundW * 0.5f, roundY + 6.0f, 0.38f);
    renderer.drawTextCenteredColored(timerText, roundX + roundW * 0.5f,
                                     roundY + 22.0f, 0.62f, timerColor,
                                     roundW - 20.0f);
    renderer.drawTextCenteredColored(detail, roundX + roundW * 0.5f,
                                     roundY + 50.0f, 0.34f, kLabel,
                                     roundW - 20.0f);

    // ── Match result overlay ────────────────────────────────────────────────
    if (matchOver_) {
        const float w = 340.0f, h = 108.0f;
        const float cx = (left + right) * 0.5f;
        const float cy = (top + bottom) * 0.5f;
        panel(cx, cy, w, h, glm::vec3(0.035f, 0.04f, 0.055f));
        panel(cx, cy + h * 0.5f - 2.0f, w, 4.0f, kGold * 0.85f);

        std::string result = "MATCH OVER";
        if (matchWinner_ == 2) result = "YOU WIN";
        else if (matchWinner_ == 0) result = "TEAM A WINS";
        else if (matchWinner_ == 1) result = "TEAM B WINS";
        renderer.drawTextCenteredColored(result, cx, cy - h * 0.5f + 14.0f,
                                         0.78f, kGold, w - 24.0f);
        char score[64];
        if (currentGameMode == TEAM_VS_TEAM || currentGameMode == BOMB_DEFUSAL) {
            snprintf(score, sizeof(score), "A %d   :   %d B", teamAScore_, teamBScore_);
        } else {
            snprintf(score, sizeof(score), "YOUR KILLS: %d", kills_);
        }
        renderer.drawTextCenteredColored(score, cx, cy - 4.0f, 0.40f,
                                         glm::vec3(0.88f), w - 24.0f);
        const char* hint = isConnectedToServer() ? "ESC: RETURN TO MENU"
                                                 : "ENTER: NEW MATCH";
        renderer.drawTextCenteredColored(hint, cx,
                                         cy + h * 0.5f - 30.0f, 0.34f,
                                         glm::vec3(0.72f, 0.76f, 0.82f), w - 24.0f);
    }
}

// ---------------------------------------------------------------------------
// First-person weapon viewmodel
// ---------------------------------------------------------------------------
void Game::updateWeaponViewmodel(float deltaTime) {
    if (deltaTime <= 0.0f) return;

    // Recoil kicks up immediately and settles over a fraction of a second.
    if (weaponRecoil_ > 0.0f) {
        weaponRecoil_ = std::max(0.0f, weaponRecoil_ - deltaTime * 7.0f);
    }
    // Switching weapons lowers the old gun and raises the new one.
    if (weaponSwitch_ > 0.0f) {
        weaponSwitch_ = std::max(0.0f, weaponSwitch_ - deltaTime * 4.5f);
    }

    // Walk bob follows the actual horizontal speed, and only while grounded.
    const float speed = std::sqrt(playerVelocity.x * playerVelocity.x +
                                  playerVelocity.z * playerVelocity.z);
    const bool movingOnGround = !spectatorMode && !isDead && speed > 0.15f && isOnGround();
    const float bobTarget = movingOnGround ? std::min(1.0f, speed / 5.0f) : 0.0f;
    weaponBobAmount_ += (bobTarget - weaponBobAmount_) *
                        std::min(1.0f, deltaTime * 8.0f);
    if (weaponBobAmount_ > 0.01f) {
        weaponBobTime_ += deltaTime * std::max(0.8f, speed) * 1.7f;
    }

    // Look sway: a small lag opposite to the mouse movement. Wrapping the yaw
    // delta keeps a turn across the ±180° seam from snapping the gun sideways.
    float dyaw = camera.Yaw - lastViewYaw_;
    while (dyaw > 180.0f) dyaw -= 360.0f;
    while (dyaw < -180.0f) dyaw += 360.0f;
    const float dpitch = camera.Pitch - lastViewPitch_;

    const float swayTargetX = glm::clamp(-dyaw * 0.010f, -0.045f, 0.045f);
    const float swayTargetY = glm::clamp(dpitch * 0.006f, -0.030f, 0.030f);
    const float swayBlend = std::min(1.0f, deltaTime * 10.0f);
    weaponSwayX_ += (swayTargetX - weaponSwayX_) * swayBlend;
    weaponSwayY_ += (swayTargetY - weaponSwayY_) * swayBlend;

    lastViewYaw_ = camera.Yaw;
    lastViewPitch_ = camera.Pitch;
}

void Game::renderWeaponViewmodel() {
    if (!window || spectatorMode || isDead || !currentWeaponInstance) return;

    int fbW = 800, fbH = 600;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = 800;
    if (fbH <= 0) fbH = 600;
    const float aspect = static_cast<float>(fbW) / static_cast<float>(fbH);

    // The weapon is submitted directly in view space (camera at the origin,
    // looking down -Z), so the view matrix is the identity: the gun stays
    // glued to the eye no matter where the player looks. A slightly wider FOV
    // than the world keeps it from filling the screen.
    const glm::mat4 view = glm::mat4(1.0f);
    const glm::mat4 projection = glm::perspective(glm::radians(58.0f), aspect, 0.01f, 10.0f);
    renderer.setViewmodelCamera(view, projection);

    // Animation: bob, look sway, recoil and weapon-switch drop.
    const float bobX = std::sin(weaponBobTime_) * 0.012f * weaponBobAmount_;
    const float bobY = -std::abs(std::cos(weaponBobTime_)) * 0.012f * weaponBobAmount_;
    // The gun sits in the lower-right quadrant but above the weapon HUD panel,
    // so both stay readable. The slight inward yaw shows the side of the model
    // instead of looking straight down the barrel.
    const glm::vec3 base(
        0.23f + bobX + weaponSwayX_,
        -0.11f + bobY + weaponSwayY_ - weaponSwitch_ * 0.18f,
        -0.64f + weaponRecoil_ * 0.07f);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), base);
    model = glm::rotate(model, glm::radians(18.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(weaponRecoil_ * 8.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(weaponRecoil_ * 2.5f), glm::vec3(0.0f, 0.0f, 1.0f));
    // The model is authored roughly at world scale; shrink it as a whole so it
    // reads as a held first-person weapon instead of a wall-sized prop.
    model = glm::scale(model, glm::vec3(0.55f));

    // Add one box in weapon-local coordinates. `rotation` is applied around the
    // part's own centre (grips/stock angles).
    auto add = [&](const glm::vec3& center, const glm::vec3& size,
                   const glm::vec3& color, const glm::vec3& rotation = glm::vec3(0.0f)) {
        glm::mat4 local = glm::translate(model, center);
        if (rotation.x != 0.0f) local = glm::rotate(local, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        if (rotation.y != 0.0f) local = glm::rotate(local, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        if (rotation.z != 0.0f) local = glm::rotate(local, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        const glm::vec3 ax = glm::vec3(local[0]) * size.x;
        const glm::vec3 ay = glm::vec3(local[1]) * size.y;
        const glm::vec3 az = glm::vec3(local[2]) * size.z;
        renderer.submitViewBoxOriented(glm::vec3(local[3]), ax, ay, az, color);
    };

    // Hands / sleeve are shared by every weapon so the gun is actually held.
    // A dark glove reads better than a large skin-coloured block at this scale.
    const glm::vec3 glove(0.32f, 0.26f, 0.20f);
    const glm::vec3 sleeve(0.16f, 0.21f, 0.17f);
    auto addHands = [&](bool longArm) {
        add(glm::vec3(0.040f, -0.095f, 0.030f), glm::vec3(0.085f, 0.115f, 0.105f),
            glove, glm::vec3(-12.0f, 0.0f, -4.0f));
        // Fingers curling around the front of the grip.
        add(glm::vec3(-0.012f, -0.070f, 0.005f), glm::vec3(0.034f, 0.055f, 0.075f),
            glove, glm::vec3(-18.0f, 0.0f, 0.0f));
        add(glm::vec3(0.060f, longArm ? -0.170f : -0.160f, 0.105f),
            glm::vec3(0.115f, 0.110f, longArm ? 0.20f : 0.15f),
            sleeve, glm::vec3(-8.0f, 0.0f, -3.0f));
    };

    const int weaponIndex = static_cast<int>(currentWeapon);
    const bool useModel = weaponIndex >= 0 && weaponIndex < 5 &&
                          weaponModels_[weaponIndex] &&
                          weaponModels_[weaponIndex]->isLoaded();

    if (useModel) {
        GltfInstance& instance = weaponModelAnims_[weaponIndex];
        instance.setPose(-1, 0.0f, 0.0f);
        instance.update();
        renderer.submitViewModelModel(weaponModels_[weaponIndex].get(), &instance, model);

        // The model owns the gun and hands; only the muzzle flash is dynamic.
        switch (currentWeapon) {
            case PISTOL:
                if (weaponRecoil_ > 0.40f) {
                    add(glm::vec3(0.0f, 0.025f, -0.43f), glm::vec3(0.10f, 0.10f, 0.12f),
                        glm::vec3(1.0f, 0.78f, 0.22f));
                }
                break;
            case PUMP_SHOTGUN:
                if (weaponRecoil_ > 0.55f) {
                    add(glm::vec3(0.0f, 0.030f, -0.66f), glm::vec3(0.14f, 0.14f, 0.16f),
                        glm::vec3(1.0f, 0.72f, 0.16f));
                }
                break;
            case SNIPER:
                if (weaponRecoil_ > 0.55f) {
                    add(glm::vec3(0.0f, 0.030f, -0.98f), glm::vec3(0.16f, 0.16f, 0.20f),
                        glm::vec3(1.0f, 0.72f, 0.16f));
                }
                break;
            default:
                break;
        }
    } else {
    // Fallback: the original procedural viewmodel, used when an asset is
    // missing or fails to load.
    switch (currentWeapon) {
        case PISTOL: {
            const glm::vec3 metal(0.40f, 0.42f, 0.46f);
            const glm::vec3 dark(0.20f, 0.20f, 0.22f);
            add(glm::vec3(0.0f, 0.025f, -0.16f), glm::vec3(0.075f, 0.065f, 0.36f), metal);
            add(glm::vec3(0.0f, -0.015f, -0.05f), glm::vec3(0.070f, 0.045f, 0.20f), dark);
            add(glm::vec3(0.0f, 0.025f, -0.365f), glm::vec3(0.040f, 0.040f, 0.070f), dark);
            add(glm::vec3(0.0f, -0.100f, 0.035f), glm::vec3(0.078f, 0.180f, 0.090f),
                glm::vec3(0.24f, 0.20f, 0.14f), glm::vec3(-12.0f, 0.0f, 0.0f));
            add(glm::vec3(0.0f, -0.105f, 0.040f), glm::vec3(0.060f, 0.115f, 0.055f),
                glm::vec3(0.42f, 0.29f, 0.15f), glm::vec3(-12.0f, 0.0f, 0.0f));
            add(glm::vec3(0.0f, 0.070f, -0.315f), glm::vec3(0.014f, 0.020f, 0.022f),
                glm::vec3(0.62f));
            add(glm::vec3(0.0f, 0.070f, 0.005f), glm::vec3(0.050f, 0.018f, 0.020f),
                glm::vec3(0.50f));
            add(glm::vec3(0.0f, 0.060f, 0.060f), glm::vec3(0.028f, 0.035f, 0.028f), dark);
            // Trigger guard.
            add(glm::vec3(0.0f, -0.050f, -0.020f), glm::vec3(0.055f, 0.012f, 0.095f), dark);
            add(glm::vec3(0.0f, -0.072f, -0.065f), glm::vec3(0.050f, 0.032f, 0.012f), dark);
            addHands(false);
            if (weaponRecoil_ > 0.40f) {
                add(glm::vec3(0.0f, 0.025f, -0.43f), glm::vec3(0.10f, 0.10f, 0.12f),
                    glm::vec3(1.0f, 0.78f, 0.22f));
            }
            break;
        }
        case PUMP_SHOTGUN: {
            const glm::vec3 metal(0.22f, 0.23f, 0.25f);
            const glm::vec3 dark(0.10f, 0.10f, 0.11f);
            const glm::vec3 wood(0.38f, 0.23f, 0.12f);
            add(glm::vec3(0.0f, 0.030f, -0.31f), glm::vec3(0.055f, 0.055f, 0.62f), metal);
            add(glm::vec3(0.0f, -0.005f, -0.28f), glm::vec3(0.045f, 0.045f, 0.52f), dark);
            add(glm::vec3(0.0f, 0.020f, -0.02f), glm::vec3(0.090f, 0.090f, 0.28f), metal);
            add(glm::vec3(0.0f, 0.000f, -0.20f), glm::vec3(0.100f, 0.075f, 0.20f), wood);
            add(glm::vec3(0.0f, -0.020f, 0.160f), glm::vec3(0.085f, 0.120f, 0.30f),
                wood, glm::vec3(8.0f, 0.0f, 0.0f));
            add(glm::vec3(0.0f, -0.100f, 0.030f), glm::vec3(0.070f, 0.160f, 0.090f),
                wood, glm::vec3(-12.0f, 0.0f, 0.0f));
            add(glm::vec3(0.052f, 0.030f, -0.04f), glm::vec3(0.010f, 0.030f, 0.10f), dark);
            add(glm::vec3(0.0f, 0.065f, -0.590f), glm::vec3(0.018f, 0.018f, 0.018f),
                glm::vec3(1.0f, 0.80f, 0.20f));
            add(glm::vec3(0.0f, -0.050f, -0.010f), glm::vec3(0.055f, 0.012f, 0.095f), dark);
            add(glm::vec3(0.0f, -0.072f, -0.055f), glm::vec3(0.050f, 0.032f, 0.012f), dark);
            addHands(true);
            if (weaponRecoil_ > 0.55f) {
                add(glm::vec3(0.0f, 0.030f, -0.66f), glm::vec3(0.14f, 0.14f, 0.16f),
                    glm::vec3(1.0f, 0.72f, 0.16f));
            }
            break;
        }
        case SNIPER: {
            const glm::vec3 metal(0.22f, 0.24f, 0.26f);
            const glm::vec3 dark(0.09f, 0.10f, 0.11f);
            const glm::vec3 stock(0.17f, 0.20f, 0.16f);
            // Long barrel + receiver + bolt handle.
            add(glm::vec3(0.0f, 0.030f, -0.52f), glm::vec3(0.042f, 0.042f, 0.86f), metal);
            add(glm::vec3(0.0f, 0.020f, -0.24f), glm::vec3(0.075f, 0.075f, 0.44f), dark);
            add(glm::vec3(0.058f, 0.035f, -0.10f), glm::vec3(0.055f, 0.022f, 0.022f), metal);
            // Scope tube with front objective and rear eyepiece.
            add(glm::vec3(0.0f, 0.105f, -0.23f), glm::vec3(0.070f, 0.070f, 0.30f), dark);
            add(glm::vec3(0.0f, 0.105f, -0.39f), glm::vec3(0.090f, 0.090f, 0.055f), metal);
            add(glm::vec3(0.0f, 0.105f, -0.07f), glm::vec3(0.080f, 0.080f, 0.055f), metal);
            // Scope mounts.
            add(glm::vec3(0.0f, 0.065f, -0.15f), glm::vec3(0.028f, 0.050f, 0.028f), metal);
            add(glm::vec3(0.0f, 0.065f, -0.31f), glm::vec3(0.028f, 0.050f, 0.028f), metal);
            // Stock and grip.
            add(glm::vec3(0.0f, -0.005f, 0.175f), glm::vec3(0.085f, 0.115f, 0.34f),
                stock, glm::vec3(7.0f, 0.0f, 0.0f));
            add(glm::vec3(0.0f, -0.100f, 0.035f), glm::vec3(0.070f, 0.160f, 0.090f),
                dark, glm::vec3(-12.0f, 0.0f, 0.0f));
            // Bipod hint under the barrel.
            add(glm::vec3(-0.030f, -0.075f, -0.49f), glm::vec3(0.016f, 0.095f, 0.016f),
                dark, glm::vec3(0.0f, 0.0f, 18.0f));
            add(glm::vec3(0.030f, -0.075f, -0.49f), glm::vec3(0.016f, 0.095f, 0.016f),
                dark, glm::vec3(0.0f, 0.0f, -18.0f));
            addHands(true);
            if (weaponRecoil_ > 0.55f) {
                add(glm::vec3(0.0f, 0.030f, -0.98f), glm::vec3(0.16f, 0.16f, 0.20f),
                    glm::vec3(1.0f, 0.72f, 0.16f));
            }
            break;
        }
        case GRENADE: {
            add(glm::vec3(0.0f, -0.010f, -0.060f), glm::vec3(0.100f, 0.130f, 0.100f),
                glm::vec3(0.20f, 0.35f, 0.15f));
            add(glm::vec3(0.0f, 0.070f, -0.060f), glm::vec3(0.060f, 0.035f, 0.060f),
                glm::vec3(0.35f, 0.36f, 0.38f));
            add(glm::vec3(0.045f, 0.045f, -0.060f), glm::vec3(0.018f, 0.018f, 0.090f),
                glm::vec3(0.55f, 0.56f, 0.58f));
            add(glm::vec3(0.078f, 0.055f, -0.060f), glm::vec3(0.014f, 0.014f, 0.060f),
                glm::vec3(0.62f));
            add(glm::vec3(0.040f, 0.055f, -0.010f), glm::vec3(0.030f, 0.012f, 0.012f),
                glm::vec3(0.62f));
            addHands(false);
            break;
        }
        case FLASHBANG: {
            add(glm::vec3(0.0f, -0.010f, -0.060f), glm::vec3(0.075f, 0.140f, 0.075f),
                glm::vec3(0.58f, 0.59f, 0.62f));
            add(glm::vec3(0.0f, 0.015f, -0.060f), glm::vec3(0.080f, 0.022f, 0.080f),
                glm::vec3(0.25f, 0.26f, 0.28f));
            add(glm::vec3(0.0f, -0.040f, -0.060f), glm::vec3(0.080f, 0.018f, 0.080f),
                glm::vec3(0.25f, 0.26f, 0.28f));
            add(glm::vec3(0.0f, 0.078f, -0.060f), glm::vec3(0.040f, 0.040f, 0.040f),
                glm::vec3(0.35f, 0.36f, 0.38f));
            add(glm::vec3(0.045f, 0.052f, -0.060f), glm::vec3(0.016f, 0.016f, 0.085f),
                glm::vec3(0.55f));
            add(glm::vec3(0.075f, 0.062f, -0.060f), glm::vec3(0.013f, 0.013f, 0.055f),
                glm::vec3(0.62f));
            addHands(false);
            break;
        }
    }
    }
}

void Game::renderBombDefusalHUD() {
    // Bomb defusal draws the bomb-specific elements below. Team deathmatch
    // only needs the team indicator from this HUD, so it returns right after.
    const bool bombMode = bombDefusalMode;
    const bool teamMode = (currentGameMode == TEAM_VS_TEAM);
    if (!bombMode && !teamMode) return;
    
    UIHelpers::setProjection(renderer, window);
    auto _b = UIHelpers::getBounds(window);
    float left=_b.left, right=_b.right, top=_b.top, bottom=_b.bottom;

    auto panel = [&](float cx, float cy, float w, float h, const glm::vec3& color) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(cx, cy, 0.0f));
        m = glm::scale(m, glm::vec3(w, h, 1.0f));
        renderer.drawColoredQuad(m, color);
    };
    const glm::vec3 kPanel(0.045f, 0.055f, 0.070f);

    // The general HUD owns the bottom-left health panel, the bottom-right
    // weapon panel, the top-right K/D counters and the top-centre round
    // panel. The mode-specific elements here use the remaining corners so
    // nothing overlaps at any aspect ratio.

    // ── Team + Role indicator (top-left corner) ──
    {
        std::string teamName;
        glm::vec3 teamColor;
        if (playerTeam == 0) {
            teamName = "TEAM A  [";
            teamColor = glm::vec3(1.0f, 0.2f, 0.2f);
        } else {
            teamName = "TEAM B  [";
            teamColor = glm::vec3(0.2f, 0.4f, 1.0f);
        }

        // Role string (bomb defusal) or the mode name (team deathmatch).
        std::string roleStr;
        if (!bombMode) {
            roleStr = "TEAM DEATHMATCH";
        } else if (isDead || spectatorMode) {
            roleStr = "SPECTATOR";
        } else if (playerTeam == attackingTeam) {
            roleStr = "ATTACKING";
        } else {
            roleStr = "DEFENDING";
        }
        teamName += roleStr + "]";

        const float teamWidth = 250.0f;
        const float teamHeight = 30.0f;
        const float teamX = left + 14.0f;
        const float teamY = top + 12.0f;

        // Background bar
        glm::mat4 teamBg = glm::mat4(1.0f);
        teamBg = glm::translate(teamBg, glm::vec3(teamX + teamWidth * 0.5f,
                                                 teamY + teamHeight * 0.5f, 0.0f));
        teamBg = glm::scale(teamBg, glm::vec3(teamWidth, teamHeight, 1.0f));
        glm::vec3 bgColor = teamColor * 0.3f;
        bgColor += glm::vec3(0.1f); // Slight brightness
        renderer.drawColoredQuad(teamBg, bgColor);

        // Team text
        renderer.drawTextFit(teamName, teamX + 10.0f, teamY + 6.0f, teamWidth - 20.0f, 0.45f);
    }

    // Everything below belongs to bomb defusal (round banner, plant/defuse
    // progress, site prompts and bomb distance markers).
    if (!bombMode) return;

    // ── Round end banner (top-centre) ──────────────────────────────────────
    if (roundEndTimer > 0.0f) {
        // A compact top-of-screen result banner instead of the old full-screen
        // dark overlay: the body stays visible (and movable) during the short
        // post-round window, while the result is still unmissable.
        const float popupW = 270.0f;
        const float popupH = 104.0f;
        const float popupX = (left + right) * 0.5f - popupW * 0.5f;
        const float popupY = top + 12.0f;
        const float popupCx = popupX + popupW * 0.5f;

        // Two short centred lines, fitted to the popup width. The old single
        // line was wider than the viewport and got clipped on both sides.
        char reasonLine[64] = "ROUND OVER";
        switch (roundEndReason) {
            case ROUND_END_BOMB_EXPLODED:
                snprintf(reasonLine, sizeof(reasonLine), "BOMB EXPLODED!");
                break;
            case ROUND_END_BOMB_DEFUSED:
                snprintf(reasonLine, sizeof(reasonLine), "BOMB DEFUSED!");
                break;
            case ROUND_END_ATTACKERS_ELIMINATED:
                snprintf(reasonLine, sizeof(reasonLine), "ATTACKERS ELIMINATED!");
                break;
            case ROUND_END_DEFENDERS_ELIMINATED:
                snprintf(reasonLine, sizeof(reasonLine), "DEFENDERS ELIMINATED!");
                break;
            case ROUND_END_TIME_EXPIRED:
                snprintf(reasonLine, sizeof(reasonLine), "TIME EXPIRED!");
                break;
            default:
                snprintf(reasonLine, sizeof(reasonLine), "%s",
                         bomb.defused ? "BOMB DEFUSED!" : "BOMB EXPLODED!");
                break;
        }

        int shownWinner = roundWinner;
        if (shownWinner < 0 && roundEndReason == ROUND_END_NONE) {
            shownWinner = bomb.defused ? 1 - bomb.plantingTeam : bomb.plantingTeam;
        }

        // Personal result, not just the winning team's name: the player wants
        // to know at a glance whether *they* won the round.
        char resultLine[32];
        glm::vec3 resultColor(0.95f);
        if (shownWinner == 0 || shownWinner == 1) {
            if (shownWinner == playerTeam) {
                snprintf(resultLine, sizeof(resultLine), "ROUND WON");
                resultColor = glm::vec3(0.30f, 0.95f, 0.40f);
            } else {
                snprintf(resultLine, sizeof(resultLine), "ROUND LOST");
                resultColor = glm::vec3(1.0f, 0.28f, 0.24f);
            }
        } else {
            snprintf(resultLine, sizeof(resultLine), "ROUND DRAW");
            resultColor = glm::vec3(0.85f);
        }

        panel(popupCx, popupY + popupH * 0.5f, popupW, popupH, kPanel);
        panel(popupCx, popupY + popupH - 2.0f, popupW, 3.0f, resultColor * 0.85f);

        const float maxLineWidth = popupW - 22.0f;
        renderer.drawTextCenteredColored(resultLine, popupCx, popupY + 9.0f,
                                         0.78f, resultColor, maxLineWidth);
        renderer.drawTextCenteredColored(reasonLine, popupCx, popupY + 35.0f,
                                         0.42f, glm::vec3(0.88f), maxLineWidth);

        // Match score: the popup keeps the score visible like the CS round
        // result instead of only naming the winner.
        char scoreLine[64];
        snprintf(scoreLine, sizeof(scoreLine), "TEAM A  %d   :   %d  TEAM B",
                 teamAScore_, teamBScore_);
        renderer.drawTextCenteredColored(scoreLine, popupCx, popupY + 56.0f,
                                         0.34f, glm::vec3(0.82f), maxLineWidth);

        // Countdown text
        char countdownMsg[48];
        snprintf(countdownMsg, sizeof(countdownMsg), "Next round in %d...",
                 static_cast<int>(ceilf(roundEndTimer)));
        renderer.drawTextCenteredColored(countdownMsg, popupCx, popupY + 77.0f,
                                         0.36f, glm::vec3(0.85f), maxLineWidth);
    }
    
    // ── Planting / Defusing progress bar (center of screen) ──
    if (isPlanting) {
        float progress = plantProgress / 3.0f; // 3 seconds to plant
        if (progress > 1.0f) progress = 1.0f;
        
        // Background bar
        glm::mat4 progBg = glm::mat4(1.0f);
        progBg = glm::translate(progBg, glm::vec3((left + right) * 0.5f, (top + bottom) * 0.5f + 60.0f, 0.0f));
        progBg = glm::scale(progBg, glm::vec3(160.0f, 14.0f, 1.0f));
        renderer.drawColoredQuad(progBg, glm::vec3(0.5f, 0.5f, 0.5f));
        
        // Fill bar
        glm::mat4 progFill = glm::mat4(1.0f);
        progFill = glm::translate(progFill, glm::vec3((left + right) * 0.5f - 80.0f + 80.0f * progress, (top + bottom) * 0.5f + 60.0f, 0.0f));
        progFill = glm::scale(progFill, glm::vec3(160.0f * progress, 14.0f, 1.0f));
        renderer.drawColoredQuad(progFill, glm::vec3(1.0f, 0.8f, 0.0f)); // Yellow
        
        renderer.drawTextCentered("PLANTING BOMB...", (left + right) * 0.5f, (top + bottom) * 0.5f + 80.0f, 0.7f);
    }
    
    if (isDefusing) {
        float progress = defuseProgress / 4.0f; // 4 seconds to defuse
        if (progress > 1.0f) progress = 1.0f;
        
        // Background bar
        glm::mat4 progBg = glm::mat4(1.0f);
        progBg = glm::translate(progBg, glm::vec3((left + right) * 0.5f, (top + bottom) * 0.5f + 60.0f, 0.0f));
        progBg = glm::scale(progBg, glm::vec3(160.0f, 14.0f, 1.0f));
        renderer.drawColoredQuad(progBg, glm::vec3(0.1f, 0.1f, 0.1f));
        
        // Fill bar
        glm::mat4 progFill = glm::mat4(1.0f);
        progFill = glm::translate(progFill, glm::vec3((left + right) * 0.5f - 80.0f + 80.0f * progress, (top + bottom) * 0.5f + 60.0f, 0.0f));
        progFill = glm::scale(progFill, glm::vec3(160.0f * progress, 14.0f, 1.0f));
        renderer.drawColoredQuad(progFill, glm::vec3(0.0f, 0.6f, 1.0f)); // Blue
        
        renderer.drawTextCentered("DEFUSING BOMB...", (left + right) * 0.5f, (top + bottom) * 0.5f + 80.0f, 0.7f);
    }
    
    // ── Nearby site / bomb prompt ──
    if (!bomb.planted && !isPlanting && !isDead) {
        int nearbySite = getNearbyBombSite();
        if (nearbySite >= 0 && playerTeam == attackingTeam) {
            char promptStr[48];
            char siteLabel = bombSites[nearbySite].label;
            snprintf(promptStr, sizeof(promptStr), "[E] Plant Bomb at Site %c", siteLabel);
            renderer.drawTextCentered(promptStr, (left + right) * 0.5f, (top + bottom) * 0.5f + 40.0f, 0.7f);
        }
    }
    
    if (bomb.planted && !bomb.defused && bomb.active && !isDefusing && !isDead) {
        if (isNearBomb() && playerTeam != bomb.plantingTeam) {
            renderer.drawTextCentered("[E] Defuse Bomb", (left + right) * 0.5f, (top + bottom) * 0.5f + 40.0f, 0.7f);
        }
    }
    
    // ── Distance markers ──
    {
        // Show distance to nearest bomb site if not carrying bomb
        if (!bomb.planted) {
            float minDist = 999.0f;
            int nearestSite = -1;
            for (int i = 0; i < activeBombSites; i++) {
                if (bombSites[i].active) {
                    float d = glm::length(playerPos - bombSites[i].position);
                    if (d < minDist) { minDist = d; nearestSite = i; }
                }
            }
            if (nearestSite >= 0 && minDist > 5.0f && !isDead) {
                char distStr[32];
                snprintf(distStr, sizeof(distStr), "Site %c  %.0fm", bombSites[nearestSite].label, minDist);
                renderer.drawText(distStr, left + 14.0f, bottom - 104.0f, 0.5f);
            }
        } else if (bomb.planted && !bomb.defused && bomb.active && !isDead) {
            float dist = glm::length(playerPos - bomb.position);
            if (dist > 3.0f) {
                char bombDistStr[32];
                snprintf(bombDistStr, sizeof(bombDistStr), "Bomb %.0fm", dist);
                renderer.drawText(bombDistStr, left + 14.0f, bottom - 104.0f, 0.5f);
            }
        }
    }
    
}