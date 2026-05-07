#include "game.hpp"
#include "../ui/ui_helpers.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <GLFW/glfw3.h>

void Game::renderFlashbangEffect() {
    if (isFlashed && flashEffectTime > 0.0f) {
        // Create a white overlay effect for flash
        float alpha = flashEffectTime / 3.0f; // Normalize to 0-1 range
        if (alpha > 1.0f) alpha = 1.0f;

        UIHelpers::setProjection(renderer, window);
        auto _b = UIHelpers::getBounds(window); float left=_b.left, right=_b.right, top=_b.top, bottom=_b.bottom;
        float centerX = (left + right) * 0.5f;
        float centerY = (top + bottom) * 0.5f;
        float w = (right - left);
        float h = (bottom - top);

        // Ensure overlay is visible over the 3D scene
        glDisable(GL_DEPTH_TEST);

        // Enable blending for transparency
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Draw white overlay with proper alpha
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(centerX, centerY, 0.0f));
        model = glm::scale(model, glm::vec3(w, h, 1.0f));
        renderer.drawColoredQuad(model, glm::vec3(alpha, alpha, alpha)); // White with alpha

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }
}

void Game::renderEnemies() {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].alive) {
            // Create a larger red cube for the enemy
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(enemies[i].x, enemies[i].y, enemies[i].z));
            model = glm::scale(model, glm::vec3(0.8f, 1.2f, 0.8f)); // Much larger size
            renderer.drawCube(model, glm::vec3(1.0f, 0.0f, 0.0f)); // Red color
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

void Game::renderDamageEffect() {
    if (damageEffectTime > 0.0f) {
        UIHelpers::setProjection(renderer, window);

        auto _b = UIHelpers::getBounds(window); float left=_b.left, right=_b.right, top=_b.top, bottom=_b.bottom;
        float centerX = (left + right) * 0.5f;
        float centerY = (top + bottom) * 0.5f;
        float w = (right - left);
        float h = (bottom - top);

        // Calculate damage effect alpha (fade out over time)
        float alpha = damageEffectTime / 0.5f; // 0.5 second duration
        if (alpha > 1.0f) alpha = 1.0f;

        // Disable depth test and enable blending for transparency
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Draw full-screen red pulse
        glm::mat4 overlayModel = glm::mat4(1.0f);
        overlayModel = glm::translate(overlayModel, glm::vec3(centerX, centerY, 0.0f));
        overlayModel = glm::scale(overlayModel, glm::vec3(w, h, 1.0f));
        glm::vec3 overlayColor = glm::vec3(1.0f, 0.0f, 0.0f) * alpha * 0.5f; // Red with alpha
        renderer.drawColoredQuad(overlayModel, overlayColor);

        // Restore state
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }
}

void Game::renderHealthEffect() {
    if (playerHealth < playerMaxHealth && playerHealth > 0.0f && window) {
        UIHelpers::setProjection(renderer, window);

        auto _b = UIHelpers::getBounds(window); float left=_b.left, right=_b.right, top=_b.top, bottom=_b.bottom;
        float centerX = (left + right) * 0.5f;
        float centerY = (top + bottom) * 0.5f;
        float w = (right - left);
        float h = (bottom - top);

        // Calculate health percentage (0.0 to 1.0)
        float healthPercent = playerHealth / playerMaxHealth;
        
        // Calculate red intensity based on health (lower health = more red)
        // Full HP = 0.0 intensity, Low HP = 0.3 intensity
        float redIntensity = (1.0f - healthPercent) * 0.3f;
        
        // Calculate pulse speed based on health (lower health = faster pulse)
        // Full HP = 0.0 speed, Low HP = 8.0 speed
        float pulseSpeed = (1.0f - healthPercent) * 8.0f;
        
        // Get current time for pulsing
        double currentTime = glfwGetTime();
        
        // Calculate pulse using sine wave (0.0 to 1.0)
        float pulse = static_cast<float>((sin(currentTime * pulseSpeed) + 1.0f) * 0.5f);
        
        // Combine base intensity with pulse
        float finalAlpha = redIntensity * (0.5f + pulse * 0.5f);
        
        // Disable depth test and enable blending for transparency
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Draw full-screen red overlay
        glm::mat4 overlayModel = glm::mat4(1.0f);
        overlayModel = glm::translate(overlayModel, glm::vec3(centerX, centerY, 0.0f));
        overlayModel = glm::scale(overlayModel, glm::vec3(w, h, 1.0f));
        glm::vec3 overlayColor = glm::vec3(1.0f, 0.0f, 0.0f) * finalAlpha;
        renderer.drawColoredQuad(overlayModel, overlayColor);

        // Restore state
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }
}

void Game::renderDeathScreen() {
    UIHelpers::setProjection(renderer, window);
    
    auto _b = UIHelpers::getBounds(window); float left=_b.left, right=_b.right, top=_b.top, bottom=_b.bottom;
    float centerX = (left + right) * 0.5f;
    float centerY = (top + bottom) * 0.5f;
    float w = (right - left);
    float h = (bottom - top);

    // Disable depth test and enable blending for transparency
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Render strong red background
    glm::mat4 backgroundModel = glm::mat4(1.0f);
    backgroundModel = glm::translate(backgroundModel, glm::vec3(centerX, centerY, 0.0f));
    backgroundModel = glm::scale(backgroundModel, glm::vec3(w, h, 1.0f));
    renderer.drawColoredQuad(backgroundModel, glm::vec3(0.8f, 0.0f, 0.0f)); // Strong red background
    
    // Render "YOU DIED" text centered on screen
    renderer.drawTextCentered("YOU DIED", centerX, centerY, 3.0f); // Big centered text

    // Restore state
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
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
    
    // Render planted bomb (BIG and prominent)
    if (bomb.planted && !bomb.defused) {
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
        float blinkSpeed = (bombTimeLeft < 5.0f) ? 10.0f : 6.0f;
        float blink = static_cast<float>((sin(currentTime * blinkSpeed) + 1.0f) * 0.5f);
        
        glm::mat4 lightMarker = glm::mat4(1.0f);
        lightMarker = glm::translate(lightMarker, bombPos + glm::vec3(0.0f, 0.45f, 0.0f));
        lightMarker = glm::scale(lightMarker, glm::vec3(0.15f, 0.08f, 0.15f));
        renderer.drawCube(lightMarker, glm::vec3(1.0f, blink * 0.2f, blink * 0.2f)); // Bright red pulse
        
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

void Game::renderBombDefusalHUD() {
    if (!bombDefusalMode) return;
    
    UIHelpers::setProjection(renderer, window);
    auto _b = UIHelpers::getBounds(window);
    float left=_b.left, right=_b.right, top=_b.top, bottom=_b.bottom;
    double currentTime = glfwGetTime();
    
    // Disable depth test and enable blending for overlay
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
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
        
        // Role string
        std::string roleStr;
        if (isDead || spectatorMode) {
            roleStr = "SPECTATOR";
        } else if (playerTeam == attackingTeam) {
            roleStr = "ATTACKING";
        } else {
            roleStr = "DEFENDING";
        }
        teamName += roleStr + "]";
        
        // Background bar
        glm::mat4 teamBg = glm::mat4(1.0f);
        teamBg = glm::translate(teamBg, glm::vec3(left + 7.0f, bottom - 20.0f, 0.0f));
        teamBg = glm::scale(teamBg, glm::vec3(200.0f, 22.0f, 1.0f));
        glm::vec3 bgColor = teamColor * 0.3f;
        bgColor += glm::vec3(0.1f); // Slight brightness
        renderer.drawColoredQuad(teamBg, bgColor);
        
        // Team text
        renderer.drawText(teamName, left + 10.0f, bottom - 18.0f, 0.8f);
    }
    
    // ── Bomb status indicator (top-center) ──
    if (bomb.planted && !bomb.defused) {
        float bombTimeLeft = bomb.explodeTime - bomb.plantTime;
        
        // Draw status box background
        glm::mat4 bombStatusBg = glm::mat4(1.0f);
        bombStatusBg = glm::translate(bombStatusBg, glm::vec3(right - 160.0f, bottom - 20.0f, 0.0f));
        bombStatusBg = glm::scale(bombStatusBg, glm::vec3(155.0f, 22.0f, 1.0f));
        renderer.drawColoredQuad(bombStatusBg, glm::vec3(0.4f, 0.0f, 0.0f));
        
        // Timer text with color based on remaining time
        glm::vec3 timerColor = (bombTimeLeft < 5.0f)
            ? glm::vec3(1.0f, 0.2f, 0.2f)  // Red when < 5s
            : glm::vec3(1.0f, 0.8f, 0.2f); // Yellow otherwise
        
        char bombStatus[32];
        snprintf(bombStatus, sizeof(bombStatus), "BOMB  %.0f", bombTimeLeft);
        renderer.drawText(bombStatus, right - 155.0f, bottom - 18.0f, 0.8f);
        
        // Bomb icon (small blinking square)
        float blink = static_cast<float>((sin(currentTime * 6.0f) + 1.0f) * 0.5f);
        glm::mat4 bombIcon = glm::mat4(1.0f);
        bombIcon = glm::translate(bombIcon, glm::vec3(right - 15.0f, bottom - 10.0f, 0.0f));
        bombIcon = glm::scale(bombIcon, glm::vec3(8.0f, 8.0f, 1.0f));
        renderer.drawColoredQuad(bombIcon, timerColor * (0.5f + blink * 0.5f));
    } else if (bomb.defused) {
        // Show "DEFUSED" notification
        glm::mat4 defusedBg = glm::mat4(1.0f);
        defusedBg = glm::translate(defusedBg, glm::vec3(right - 160.0f, bottom - 20.0f, 0.0f));
        defusedBg = glm::scale(defusedBg, glm::vec3(155.0f, 22.0f, 1.0f));
        renderer.drawColoredQuad(defusedBg, glm::vec3(0.0f, 0.3f, 0.0f));
        renderer.drawText("BOMB DEFUSED!", right - 155.0f, bottom - 18.0f, 0.8f);
    }
    
    // ── Round end overlay ──
    if (roundEndTimer > 0.0f) {
        // Semi-transparent dark overlay
        glm::mat4 overlay = glm::mat4(1.0f);
        overlay = glm::translate(overlay, glm::vec3((left + right) * 0.5f, (top + bottom) * 0.5f, 0.0f));
        overlay = glm::scale(overlay, glm::vec3(right - left, bottom - top, 1.0f));
        renderer.drawColoredQuad(overlay, glm::vec3(0.0f, 0.0f, 0.0f) * 0.4f);
        
        // Big centered message
        char roundEndMsg[64];
        if (bomb.defused) {
            snprintf(roundEndMsg, sizeof(roundEndMsg), "BOMB DEFUSED! Team %s wins!",
                     (bomb.plantingTeam == 0 ? "B (Blue)" : "A (Red)"));
        } else {
            snprintf(roundEndMsg, sizeof(roundEndMsg), "BOMB EXPLODED! Team %s wins!",
                     (bomb.plantingTeam == 0 ? "A (Red)" : "B (Blue)"));
        }
        renderer.drawTextCentered(roundEndMsg, (left + right) * 0.5f, (top + bottom) * 0.5f, 1.2f);
        
        // Countdown text
        char countdownMsg[32];
        snprintf(countdownMsg, sizeof(countdownMsg), "Next round in %d...", static_cast<int>(ceilf(roundEndTimer)));
        renderer.drawTextCentered(countdownMsg, (left + right) * 0.5f, (top + bottom) * 0.5f - 40.0f, 0.7f);
    }
    
    // ── Round timer (top-center) ──
    if (roundActive) {
        char roundTimeStr[16];
        int mins = static_cast<int>(roundTime) / 60;
        int secs = static_cast<int>(roundTime) % 60;
        snprintf(roundTimeStr, sizeof(roundTimeStr), "%02d:%02d", mins, secs);
        renderer.drawTextCentered(roundTimeStr, (left + right) * 0.5f, bottom - 18.0f, 1.0f);
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
    
    if (bomb.planted && !bomb.defused && !isDefusing && !isDead) {
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
                renderer.drawText(distStr, left + 10.0f, bottom - 45.0f, 0.6f);
            }
        } else if (bomb.planted && !bomb.defused && !isDead) {
            float dist = glm::length(playerPos - bomb.position);
            if (dist > 3.0f) {
                char bombDistStr[32];
                snprintf(bombDistStr, sizeof(bombDistStr), "Bomb %.0fm", dist);
                renderer.drawText(bombDistStr, left + 10.0f, bottom - 45.0f, 0.6f);
            }
        }
    }
    
    // Restore state
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}