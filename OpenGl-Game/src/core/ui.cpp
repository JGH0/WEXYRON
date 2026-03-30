#include <iostream>
#include <cstring>
#include "ui.hpp"

UI::UI(Renderer* renderer) 
    : renderer(renderer), menuSelection(0), mapSelection(0), settingsSelection(0), infiniteAmmoMode(false) {
}

void UI::renderMainMenu(int selection) {
    // Render main menu options as colored rectangles
    float menuWidth = 300.0f;
    float menuHeight = 50.0f;
    float menuX = (800.0f - menuWidth) / 2.0f; // Center horizontally
    float startY = 200.0f;
    float spacing = 70.0f;
    
    const char* menuItems[] = {"Play Game", "Map Selection", "Settings", "Exit"};
    const int numItems = 4;
    
    for (int i = 0; i < numItems; i++) {
        float y = startY + i * spacing;
        
        // Create background rectangle for this menu item
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(menuX + menuWidth/2.0f, y + menuHeight/2.0f, 0.0f));
        model = glm::scale(model, glm::vec3(menuWidth, menuHeight, 1.0f));
        
        // Use different color for selected item
        if (i == selection) {
            renderer->drawColoredQuad(model, glm::vec3(0.2f, 0.8f, 0.2f)); // Green when selected
        } else {
            renderer->drawColoredQuad(model, glm::vec3(0.3f, 0.5f, 0.7f)); // Blue when not selected
        }
        
        // Draw selection indicator
        if (i == selection) {
            glm::mat4 indicatorModel = glm::mat4(1.0f);
            indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 15.0f, y + menuHeight/2.0f, 0.0f));
            indicatorModel = glm::scale(indicatorModel, glm::vec3(10.0f, 10.0f, 1.0f));
            renderer->drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f)); // White indicator
        }
        
        // Draw text
        float textX = menuX + 50.0f;
        float textY = y + menuHeight/2.0f - 10.0f;
        renderer->drawText(menuItems[i], textX, textY, 1.5f);
    }
    
    // Draw title
    renderer->drawText("OPENGL GAME", 250.0f, 100.0f, 2.5f);
    
    // Draw instructions
    renderer->drawText("Use UP/DOWN to select, ENTER to confirm, ESC to resume", 200.0f, 550.0f, 1.0f);
}

void UI::processMainMenuInput(GLFWwindow* window, int& selection, GameState& gameState) {
    static bool upPressed = false;
    static bool downPressed = false;
    static bool enterPressed = false;
    
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !upPressed) {
        upPressed = true;
        selection = (selection - 1 + 4) % 4; // Wrap around
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        upPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !downPressed) {
        downPressed = true;
        selection = (selection + 1) % 4; // Wrap around
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        downPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterPressed) {
        enterPressed = true;
        switch (selection) {
            case 0: gameState = MAP_SELECTION; break;
            case 1: gameState = MAP_SELECTION; break;
            case 2: gameState = SETTINGS; break;
            case 3: glfwSetWindowShouldClose(window, GLFW_TRUE); break;
        }
    } else if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
        enterPressed = false;
    }
}

void UI::renderMapSelection(int selection) {
    // Dark background
    glm::mat4 bgModel = glm::mat4(1.0f);
    bgModel = glm::translate(bgModel, glm::vec3(400.0f, 300.0f, 0.0f));
    bgModel = glm::scale(bgModel, glm::vec3(800.0f, 600.0f, 1.0f));
    renderer->drawColoredQuad(bgModel, glm::vec3(0.05f, 0.05f, 0.15f)); // Very dark blue
    
    // Title
    renderer->drawText("SELECT MAP", 300.0f, 120.0f, 2.0f);
    
    // Map data
    const char* mapNames[] = {"SIMPLE", "LARGE", "ARENA", "SNIPER", "COMPLEX"};
    glm::vec3 mapColors[] = {
        glm::vec3(0.3f, 0.8f, 0.3f),  // Green - Simple
        glm::vec3(0.3f, 0.5f, 1.0f),  // Blue - Large
        glm::vec3(1.0f, 0.6f, 0.2f),  // Orange - Arena
        glm::vec3(0.8f, 0.3f, 0.8f),  // Purple - Sniper
        glm::vec3(0.5f, 0.8f, 1.0f)   // Cyan - Complex
    };
    
    float menuWidth = 350.0f;
    float menuHeight = 60.0f;
    float menuX = (800.0f - menuWidth) / 2.0f;
    float startY = 200.0f;
    float spacing = 80.0f;
    
    for (int i = 0; i < 5; i++) {
        float y = startY + i * spacing;
        
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(menuX + menuWidth/2.0f, y + menuHeight/2.0f, 0.0f));
        model = glm::scale(model, glm::vec3(menuWidth, menuHeight, 1.0f));
        
        if (i == selection) {
            renderer->drawColoredQuad(model, mapColors[i] * 1.5f);
        } else {
            renderer->drawColoredQuad(model, mapColors[i]);
        }
        
        if (i == selection) {
            glm::mat4 indicatorModel = glm::mat4(1.0f);
            indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 15.0f, y + menuHeight/2.0f, 0.0f));
            indicatorModel = glm::scale(indicatorModel, glm::vec3(15.0f, 15.0f, 1.0f));
            renderer->drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
        }
        
        float textX = menuX + 50.0f;
        float textY = y + menuHeight/2.0f - 10.0f;
        renderer->drawText(mapNames[i], textX, textY, 1.5f);
    }
    
    renderer->drawText("UP/DOWN = change, ENTER = load, ESC = back", 250.0f, 550.0f, 1.0f);
}

void UI::processMapSelectionInput(GLFWwindow* window, int& selection, GameState& gameState) {
    static bool upPressed = false;
    static bool downPressed = false;
    static bool enterPressed = false;
    
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !upPressed) {
        upPressed = true;
        selection = (selection - 1 + 5) % 5;
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        upPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !downPressed) {
        downPressed = true;
        selection = (selection + 1) % 5;
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        downPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterPressed) {
        enterPressed = true;
        // Map loading will be handled by game class
        gameState = PLAYING;
    } else if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
        enterPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        gameState = MENU;
    }
}

void UI::renderSettings(int selection, bool infiniteAmmo) {
    // Render settings options
    float menuWidth = 400.0f;
    float menuHeight = 40.0f;
    float menuX = (800.0f - menuWidth) / 2.0f;
    float startY = 150.0f;
    float spacing = 50.0f;
    
    const char* settingItems[] = {"Forward", "Backward", "Left", "Right", "Jump", "Menu", "Infinite Ammo"};
    const int numSettings = 7;
    
    for (int i = 0; i < numSettings; i++) {
        float y = startY + i * spacing;
        
        glm::mat4 bgModel = glm::mat4(1.0f);
        bgModel = glm::translate(bgModel, glm::vec3(menuX + menuWidth/2.0f, y + menuHeight/2.0f, 0.0f));
        bgModel = glm::scale(bgModel, glm::vec3(menuWidth, menuHeight, 1.0f));
        
        glm::vec3 bgColor;
        if (i == selection) {
            bgColor = glm::vec3(0.6f, 0.8f, 0.2f); // Bright green when selected
        } else if (i == 6) { // Infinite ammo - special color
            bgColor = infiniteAmmo ? glm::vec3(0.8f, 0.8f, 0.2f) : glm::vec3(0.4f, 0.4f, 0.4f);
        } else {
            bgColor = glm::vec3(0.3f, 0.5f, 0.7f); // Blue for controls
        }
        
        renderer->drawColoredQuad(bgModel, bgColor);
        
        float textX = menuX + 20.0f;
        renderer->drawText(settingItems[i], textX, y + menuHeight/2.0f - 8.0f, 1.2f);
        
        if (i == selection) {
            glm::mat4 indicatorModel = glm::mat4(1.0f);
            indicatorModel = glm::translate(indicatorModel, glm::vec3(menuX + 10.0f, y + menuHeight/2.0f, 0.0f));
            indicatorModel = glm::scale(indicatorModel, glm::vec3(8.0f, 8.0f, 1.0f));
            renderer->drawColoredQuad(indicatorModel, glm::vec3(1.0f, 1.0f, 1.0f));
        }
    }
    
    renderer->drawText("UP/DOWN = change, ENTER = toggle, ESC = back", 250.0f, 500.0f, 1.0f);
}

void UI::processSettingsInput(GLFWwindow* window, int& selection, bool& infiniteAmmo) {
    static bool upPressed = false;
    static bool downPressed = false;
    static bool enterPressed = false;
    
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS && !upPressed) {
        upPressed = true;
        selection = (selection - 1 + 7) % 7;
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        upPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS && !downPressed) {
        downPressed = true;
        selection = (selection + 1) % 7;
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        downPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterPressed) {
        enterPressed = true;
        if (selection == 6) { // Infinite ammo toggle
            infiniteAmmo = !infiniteAmmo;
        }
    } else if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
        enterPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        // Return to main menu
    }
}

void UI::renderHUD(int health, int maxHealth, int ammo, const char* weaponName) {
    // Health bar
    float healthPercent = (float)health / maxHealth;
    glm::mat4 healthBg = glm::mat4(1.0f);
    healthBg = glm::translate(healthBg, glm::vec3(110.0f, 570.0f, 0.0f));
    healthBg = glm::scale(healthBg, glm::vec3(200.0f, 20.0f, 1.0f));
    renderer->drawColoredQuad(healthBg, glm::vec3(0.2f, 0.2f, 0.2f)); // Dark background
    
    glm::mat4 healthBar = glm::mat4(1.0f);
    healthBar = glm::translate(healthBar, glm::vec3(10.0f, 575.0f, 0.0f));
    healthBar = glm::scale(healthBar, glm::vec3(200.0f * healthPercent, 10.0f, 1.0f));
    renderer->drawColoredQuad(healthBar, glm::vec3(0.8f, 0.2f, 0.2f)); // Red health bar
    
    // Ammo and weapon info
    char ammoText[64];
    sprintf(ammoText, "%s: %d", weaponName, ammo);
    renderer->drawText(ammoText, 650.0f, 570.0f, 1.2f);
}
