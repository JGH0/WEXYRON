#ifndef UI_HPP
#define UI_HPP

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "renderer.hpp"

/**
 * @brief UI rendering system for menus and HUD
 */
class UI {
public:
    enum GameState {
        MENU,
        PLAYING,
        MAP_SELECTION,
        SETTINGS,
        PAUSED,
        GAME_OVER
    };

private:
    Renderer* renderer;
    int menuSelection;
    int mapSelection;
    int settingsSelection;
    bool infiniteAmmoMode;

public:
    UI(Renderer* renderer);
    
    // Main menu
    void renderMainMenu(int selection);
    void processMainMenuInput(GLFWwindow* window, int& selection, GameState& gameState);
    
    // Map selection
    void renderMapSelection(int selection);
    void processMapSelectionInput(GLFWwindow* window, int& selection, GameState& gameState);
    
    // Settings
    void renderSettings(int selection, bool infiniteAmmo);
    void processSettingsInput(GLFWwindow* window, int& selection, bool& infiniteAmmo);
    
    // HUD
    void renderHUD(int health, int maxHealth, int ammo, const char* weaponName);
    
    // Getters
    int getMenuSelection() const { return menuSelection; }
    int getMapSelection() const { return mapSelection; }
    int getSettingsSelection() const { return settingsSelection; }
    
    // Setters
    void setMenuSelection(int selection) { menuSelection = selection; }
    void setMapSelection(int selection) { mapSelection = selection; }
    void setSettingsSelection(int selection) { settingsSelection = selection; }
    void setInfiniteAmmoMode(bool enabled) { infiniteAmmoMode = enabled; }
};

#endif
