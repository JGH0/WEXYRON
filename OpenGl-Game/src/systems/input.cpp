#include "input.hpp"
#include <iostream>

Input::Input() 
    : forwardPressed(false), backwardPressed(false), leftPressed(false), rightPressed(false),
      jumpPressed(false), shootPressed(false), menuPressed(false),
      weapon1Pressed(false), weapon2Pressed(false), weapon3Pressed(false), weapon4Pressed(false),
      enterPressed(false), upPressed(false), downPressed(false), escPressed(false) {
    initKeyBindings();
}

void Input::initKeyBindings() {
    keyBindings.forward = GLFW_KEY_W;
    keyBindings.backward = GLFW_KEY_S;
    keyBindings.left = GLFW_KEY_A;
    keyBindings.right = GLFW_KEY_D;
    keyBindings.jump = GLFW_KEY_SPACE;
    keyBindings.shoot = GLFW_MOUSE_BUTTON_LEFT;
    keyBindings.menu = GLFW_KEY_ESCAPE;
    keyBindings.weapon1 = GLFW_KEY_1;
    keyBindings.weapon2 = GLFW_KEY_2;
    keyBindings.weapon3 = GLFW_KEY_3;
    keyBindings.weapon4 = GLFW_KEY_4;
}

void Input::processInput(GLFWwindow* window) {
    // Movement keys
    if (glfwGetKey(window, keyBindings.forward) == GLFW_PRESS) {
        forwardPressed = true;
    } else if (glfwGetKey(window, keyBindings.forward) == GLFW_RELEASE) {
        forwardPressed = false;
    }
    
    if (glfwGetKey(window, keyBindings.backward) == GLFW_PRESS) {
        backwardPressed = true;
    } else if (glfwGetKey(window, keyBindings.backward) == GLFW_RELEASE) {
        backwardPressed = false;
    }
    
    if (glfwGetKey(window, keyBindings.left) == GLFW_PRESS) {
        leftPressed = true;
    } else if (glfwGetKey(window, keyBindings.left) == GLFW_RELEASE) {
        leftPressed = false;
    }
    
    if (glfwGetKey(window, keyBindings.right) == GLFW_PRESS) {
        rightPressed = true;
    } else if (glfwGetKey(window, keyBindings.right) == GLFW_RELEASE) {
        rightPressed = false;
    }
    
    // Action keys
    if (glfwGetKey(window, keyBindings.jump) == GLFW_PRESS) {
        jumpPressed = true;
    } else if (glfwGetKey(window, keyBindings.jump) == GLFW_RELEASE) {
        jumpPressed = false;
    }
    
    if (glfwGetMouseButton(window, keyBindings.shoot) == GLFW_PRESS) {
        shootPressed = true;
    } else if (glfwGetMouseButton(window, keyBindings.shoot) == GLFW_RELEASE) {
        shootPressed = false;
    }
    
    if (glfwGetKey(window, keyBindings.menu) == GLFW_PRESS) {
        menuPressed = true;
    } else if (glfwGetKey(window, keyBindings.menu) == GLFW_RELEASE) {
        menuPressed = false;
    }
    
    // Weapon switching
    if (glfwGetKey(window, keyBindings.weapon1) == GLFW_PRESS) {
        weapon1Pressed = true;
    } else if (glfwGetKey(window, keyBindings.weapon1) == GLFW_RELEASE) {
        weapon1Pressed = false;
    }
    
    if (glfwGetKey(window, keyBindings.weapon2) == GLFW_PRESS) {
        weapon2Pressed = true;
    } else if (glfwGetKey(window, keyBindings.weapon2) == GLFW_RELEASE) {
        weapon2Pressed = false;
    }
    
    if (glfwGetKey(window, keyBindings.weapon3) == GLFW_PRESS) {
        weapon3Pressed = true;
    } else if (glfwGetKey(window, keyBindings.weapon3) == GLFW_RELEASE) {
        weapon3Pressed = false;
    }
    
    if (glfwGetKey(window, keyBindings.weapon4) == GLFW_PRESS) {
        weapon4Pressed = true;
    } else if (glfwGetKey(window, keyBindings.weapon4) == GLFW_RELEASE) {
        weapon4Pressed = false;
    }
    
    // Menu navigation
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS) {
        enterPressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
        enterPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        upPressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_RELEASE) {
        upPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        downPressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_RELEASE) {
        downPressed = false;
    }
    
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        escPressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_RELEASE) {
        escPressed = false;
    }
}
