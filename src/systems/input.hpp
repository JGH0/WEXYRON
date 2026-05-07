#ifndef INPUT_HPP
#define INPUT_HPP

#include <GLFW/glfw3.h>

// Forward declaration
class Game;

/**
 * @brief Input handling system for the game
 */
class Input {
public:
    struct KeyBindings {
        int forward;
        int backward;
        int left;
        int right;
        int jump;
        int shoot;
        int menu;
        int weapon1;
        int weapon2;
        int weapon3;
        int weapon4;
    };

private:
    KeyBindings keyBindings;
    
    bool forwardPressed;
    bool backwardPressed;
    bool leftPressed;
    bool rightPressed;
    bool jumpPressed;
    bool shootPressed;
    bool menuPressed;
    bool weapon1Pressed;
    bool weapon2Pressed;
    bool weapon3Pressed;
    bool weapon4Pressed;
    bool enterPressed;
    bool upPressed;
    bool downPressed;
    bool escPressed;

public:
    Input();
    
    // Initialize default key bindings
    void initKeyBindings();
    
    // Process input for a window
    void processInput(GLFWwindow* window);
    
    // Getters for key states
    bool isForwardPressed() const { return forwardPressed; }
    bool isBackwardPressed() const { return backwardPressed; }
    bool isLeftPressed() const { return leftPressed; }
    bool isRightPressed() const { return rightPressed; }
    bool isJumpPressed() const { return jumpPressed; }
    bool isShootPressed() const { return shootPressed; }
    bool isMenuPressed() const { return menuPressed; }
    bool isWeapon1Pressed() const { return weapon1Pressed; }
    bool isWeapon2Pressed() const { return weapon2Pressed; }
    bool isWeapon3Pressed() const { return weapon3Pressed; }
    bool isWeapon4Pressed() const { return weapon4Pressed; }
    bool isEnterPressed() const { return enterPressed; }
    bool isUpPressed() const { return upPressed; }
    bool isDownPressed() const { return downPressed; }
    bool isEscPressed() const { return escPressed; }
    
    // Get key bindings
    const KeyBindings& getKeyBindings() const { return keyBindings; }
    void setKeyBindings(const KeyBindings& bindings) { keyBindings = bindings; }
};

#endif
