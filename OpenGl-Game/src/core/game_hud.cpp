#include "game.hpp"
#include "../ui/ui_helpers.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>

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