#include "game_state.hpp"

GameStateManager::GameStateManager() 
    : currentState(MENU), previousState(MENU), 
      menuSelection(0), mapSelection(0), settingsSelection(0), 
      infiniteAmmoMode(false) {
}

void GameStateManager::setState(GameState newState) {
    previousState = currentState;
    currentState = newState;
}
