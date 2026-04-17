#include "game.hpp"
#include "../ui/ui_helpers.hpp"
#include <glm/gtc/matrix_transform.hpp>

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

        // Draw white overlay
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(centerX, centerY, 0.0f));
        model = glm::scale(model, glm::vec3(w, h, 1.0f));
        renderer.drawCube(model, glm::vec3(alpha, alpha, alpha)); // White with alpha

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
        float centerY = (top + bottom) * 0.5f;
        float h = (bottom - top);
        
        // Calculate damage effect alpha (fade out over time)
        float alpha = damageEffectTime / 0.5f; // 0.5 second duration
        
        // Draw subtle red side overlays for damage effect
        float sideWidth = 150.0f; // Width of side effects
        float sideAlpha = alpha * 0.3f; // Much more subtle
        
        // Left side red tint
        glm::mat4 leftModel = glm::mat4(1.0f);
        leftModel = glm::translate(leftModel, glm::vec3(left + sideWidth/2.0f, centerY, 0.0f));
        leftModel = glm::scale(leftModel, glm::vec3(sideWidth, h, 1.0f));
        glm::vec3 leftColor = glm::vec3(0.8f, 0.0f, 0.0f) * sideAlpha; // Subtle red
        renderer.drawColoredQuad(leftModel, leftColor);
        
        // Right side red tint
        glm::mat4 rightModel = glm::mat4(1.0f);
        rightModel = glm::translate(rightModel, glm::vec3(right - sideWidth/2.0f, centerY, 0.0f));
        rightModel = glm::scale(rightModel, glm::vec3(sideWidth, h, 1.0f));
        glm::vec3 rightColor = glm::vec3(0.8f, 0.0f, 0.0f) * sideAlpha; // Subtle red
        renderer.drawColoredQuad(rightModel, rightColor);
    }
}

void Game::renderDeathScreen() {
    // Render death screen with red background
    glm::mat4 backgroundModel = glm::mat4(1.0f);
    backgroundModel = glm::translate(backgroundModel, glm::vec3(400.0f, 300.0f, 0.0f));
    backgroundModel = glm::scale(backgroundModel, glm::vec3(800.0f, 600.0f, 1.0f));
    renderer.drawColoredQuad(backgroundModel, glm::vec3(0.5f, 0.0f, 0.0f)); // Dark red background
    
    // Render "YOU DIED" text (using colored quads)
    float textY = 250.0f;
    float textScale = 40.0f;
    float spacing = 50.0f;
    
    // Simple representation of "YOU DIED" using colored blocks
    glm::vec3 textColor = glm::vec3(1.0f, 1.0f, 1.0f); // White text
    
    // Y
    glm::mat4 y1 = glm::mat4(1.0f);
    y1 = glm::translate(y1, glm::vec3(200.0f, textY, 0.0f));
    y1 = glm::scale(y1, glm::vec3(textScale, textScale * 3, 1.0f));
    renderer.drawColoredQuad(y1, textColor);
    
    glm::mat4 y2 = glm::mat4(1.0f);
    y2 = glm::translate(y2, glm::vec3(200.0f + textScale, textY + textScale * 2, 0.0f));
    y2 = glm::scale(y2, glm::vec3(textScale * 2, textScale, 1.0f));
    renderer.drawColoredQuad(y2, textColor);
    
    // O
    glm::mat4 o = glm::mat4(1.0f);
    o = glm::translate(o, glm::vec3(200.0f + spacing * 2, textY + textScale, 0.0f));
    o = glm::scale(o, glm::vec3(textScale * 2, textScale * 2, 1.0f));
    renderer.drawColoredQuad(o, textColor);
    
    // U
    glm::mat4 u1 = glm::mat4(1.0f);
    u1 = glm::translate(u1, glm::vec3(200.0f + spacing * 4, textY, 0.0f));
    u1 = glm::scale(u1, glm::vec3(textScale, textScale * 3, 1.0f));
    renderer.drawColoredQuad(u1, textColor);
    
    glm::mat4 u2 = glm::mat4(1.0f);
    u2 = glm::translate(u2, glm::vec3(200.0f + spacing * 5, textY, 0.0f));
    u2 = glm::scale(u2, glm::vec3(textScale, textScale * 3, 1.0f));
    renderer.drawColoredQuad(u2, textColor);
    
    glm::mat4 u3 = glm::mat4(1.0f);
    u3 = glm::translate(u3, glm::vec3(200.0f + spacing * 4, textY + textScale * 2, 0.0f));
    u3 = glm::scale(u3, glm::vec3(textScale * 2, textScale, 1.0f));
    renderer.drawColoredQuad(u3, textColor);
    
    // D
    glm::mat4 d1 = glm::mat4(1.0f);
    d1 = glm::translate(d1, glm::vec3(200.0f + spacing * 6.5f, textY, 0.0f));
    d1 = glm::scale(d1, glm::vec3(textScale, textScale * 3, 1.0f));
    renderer.drawColoredQuad(d1, textColor);
    
    glm::mat4 d2 = glm::mat4(1.0f);
    d2 = glm::translate(d2, glm::vec3(200.0f + spacing * 7.5f, textY, 0.0f));
    d2 = glm::scale(d2, glm::vec3(textScale, textScale * 2, 1.0f));
    renderer.drawColoredQuad(d2, textColor);
    
    glm::mat4 d3 = glm::mat4(1.0f);
    d3 = glm::translate(d3, glm::vec3(200.0f + spacing * 7.5f, textY + textScale * 2, 0.0f));
    d3 = glm::scale(d3, glm::vec3(textScale, textScale, 1.0f));
    renderer.drawColoredQuad(d3, textColor);
    
    glm::mat4 d4 = glm::mat4(1.0f);
    d4 = glm::translate(d4, glm::vec3(200.0f + spacing * 8.5f, textY + textScale, 0.0f));
    d4 = glm::scale(d4, glm::vec3(textScale, textScale, 1.0f));
    renderer.drawColoredQuad(d4, textColor);
    
    // I
    glm::mat4 i = glm::mat4(1.0f);
    i = glm::translate(i, glm::vec3(200.0f + spacing * 10, textY, 0.0f));
    i = glm::scale(i, glm::vec3(textScale, textScale * 3, 1.0f));
    renderer.drawColoredQuad(i, textColor);
    
    // E
    glm::mat4 e1 = glm::mat4(1.0f);
    e1 = glm::translate(e1, glm::vec3(200.0f + spacing * 11.5f, textY, 0.0f));
    e1 = glm::scale(e1, glm::vec3(textScale * 2, textScale, 1.0f));
    renderer.drawColoredQuad(e1, textColor);
    
    glm::mat4 e2 = glm::mat4(1.0f);
    e2 = glm::translate(e2, glm::vec3(200.0f + spacing * 11.5f, textY + textScale, 0.0f));
    e2 = glm::scale(e2, glm::vec3(textScale * 1.5f, textScale, 1.0f));
    renderer.drawColoredQuad(e2, textColor);
    
    glm::mat4 e3 = glm::mat4(1.0f);
    e3 = glm::translate(e3, glm::vec3(200.0f + spacing * 11.5f, textY + textScale * 2, 0.0f));
    e3 = glm::scale(e3, glm::vec3(textScale * 2, textScale, 1.0f));
    renderer.drawColoredQuad(e3, textColor);
    
    // D
    glm::mat4 d5 = glm::mat4(1.0f);
    d5 = glm::translate(d5, glm::vec3(200.0f + spacing * 13.5f, textY, 0.0f));
    d5 = glm::scale(d5, glm::vec3(textScale, textScale * 3, 1.0f));
    renderer.drawColoredQuad(d5, textColor);
    
    glm::mat4 d6 = glm::mat4(1.0f);
    d6 = glm::translate(d6, glm::vec3(200.0f + spacing * 14.5f, textY, 0.0f));
    d6 = glm::scale(d6, glm::vec3(textScale, textScale * 2, 1.0f));
    renderer.drawColoredQuad(d6, textColor);
    
    glm::mat4 d7 = glm::mat4(1.0f);
    d7 = glm::translate(d7, glm::vec3(200.0f + spacing * 14.5f, textY + textScale * 2, 0.0f));
    d7 = glm::scale(d7, glm::vec3(textScale, textScale, 1.0f));
    renderer.drawColoredQuad(d7, textColor);
    
    glm::mat4 d8 = glm::mat4(1.0f);
    d8 = glm::translate(d8, glm::vec3(200.0f + spacing * 15.5f, textY + textScale, 0.0f));
    d8 = glm::scale(d8, glm::vec3(textScale, textScale, 1.0f));
    renderer.drawColoredQuad(d8, textColor);
}