#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

/**
 * @brief Game state management system
 */
class GameStateManager {
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
    GameState currentState;
    GameState previousState;
    int menuSelection;
    int mapSelection;
    int settingsSelection;
    bool infiniteAmmoMode;

public:
    GameStateManager();
    
    // State management
    void setState(GameState newState);
    GameState getState() const { return currentState; }
    GameState getPreviousState() const { return previousState; }
    
    // Menu selections
    int getMenuSelection() const { return menuSelection; }
    void setMenuSelection(int selection) { menuSelection = selection; }
    
    int getMapSelection() const { return mapSelection; }
    void setMapSelection(int selection) { mapSelection = selection; }
    
    int getSettingsSelection() const { return settingsSelection; }
    void setSettingsSelection(int selection) { settingsSelection = selection; }
    
    // Game settings
    bool isInfiniteAmmoMode() const { return infiniteAmmoMode; }
    void setInfiniteAmmoMode(bool enabled) { infiniteAmmoMode = enabled; }
    
    // State checks
    bool isMenu() const { return currentState == MENU; }
    bool isPlaying() const { return currentState == PLAYING; }
    bool isMapSelection() const { return currentState == MAP_SELECTION; }
    bool isSettings() const { return currentState == SETTINGS; }
    bool isPaused() const { return currentState == PAUSED; }
    bool isGameOver() const { return currentState == GAME_OVER; }
};

#endif
