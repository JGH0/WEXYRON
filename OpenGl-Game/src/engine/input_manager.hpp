#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <unordered_map>
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <vector>

/**
 * @brief Input management system
 * 
 * Handles all input operations including:
 * - Keyboard input
 * - Mouse input
 * - Gamepad input
 * - Input mapping and actions
 * - Input event handling
 */
class InputManager {
public:
    // Input types
    enum class InputType {
        Key,
        MouseButton,
        MouseMove,
        MouseScroll,
        GamepadButton,
        GamepadAxis
    };
    
    // Input actions
    enum class Action {
        Press,
        Release,
        Repeat,
        Move,
        Scroll
    };
    
    // Key codes (simplified)
    enum class Key {
        Unknown = -1,
        Space = 32,
        Apostrophe = 39,
        Comma = 44,
        Minus = 45,
        Period = 46,
        Slash = 47,
        Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
        Semicolon = 59,
        Equal = 61,
        A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        LeftBracket = 91,
        Backslash = 92,
        RightBracket = 93,
        GraveAccent = 96,
        World1 = 161, World2 = 162,
        Escape = 256,
        Enter, Tab, Backspace, Insert, Delete,
        Right, Left, Down, Up,
        PageUp, PageDown, Home, End,
        CapsLock, ScrollLock, NumLock,
        PrintScreen, Pause,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
        KP0, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9,
        KPDecimal, KPDivide, KPMultiply, KPSubtract, KPAdd,
        KPEnter, KPEqual,
        LeftShift, LeftControl, LeftAlt, LeftSuper,
        RightShift, RightControl, RightAlt, RightSuper,
        Menu
    };
    
    // Mouse buttons
    enum class MouseButton {
        Left = 0,
        Right = 1,
        Middle = 2,
        Button4 = 3,
        Button5 = 4,
        Button6 = 5,
        Button7 = 6,
        Button8 = 7
    };
    
    // Input mapping
    struct InputMapping {
        std::string actionName;
        InputType inputType;
        int inputCode;
        float scale = 1.0f;
        bool isAxis = false;
    };

private:
    // Key states
    std::unordered_map<int, bool> keyStates;
    std::unordered_map<int, bool> keyStatesPrevious;
    
    // Mouse states
    std::unordered_map<int, bool> mouseButtonStates;
    std::unordered_map<int, bool> mouseButtonStatesPrevious;
    glm::vec2 mousePosition;
    glm::vec2 mousePositionPrevious;
    glm::vec2 mouseDelta;
    glm::vec2 scrollDelta;
    
    // Input mappings
    std::unordered_map<std::string, std::vector<InputMapping>> actionMappings;
    std::unordered_map<std::string, float> actionValues;
    
    // Event callbacks
    std::unordered_map<std::string, std::function<void()>> actionCallbacks;
    std::unordered_map<std::string, std::function<void(float)>> axisCallbacks;
    
    bool isInitialized = false;

public:
    InputManager();
    ~InputManager();
    
    // Input manager lifecycle
    bool initialize();
    void shutdown();
    void update();
    
    // Key input
    bool isKeyPressed(Key key) const;
    bool isKeyJustPressed(Key key) const;
    bool isKeyJustReleased(Key key) const;
    
    // Mouse input
    bool isMouseButtonPressed(MouseButton button) const;
    bool isMouseButtonJustPressed(MouseButton button) const;
    bool isMouseButtonJustReleased(MouseButton button) const;
    
    glm::vec2 getMousePosition() const { return mousePosition; }
    glm::vec2 getMouseDelta() const { return mouseDelta; }
    glm::vec2 getScrollDelta() const { return scrollDelta; }
    
    // Input mapping
    void mapAction(const std::string& actionName, Key key);
    void mapAction(const std::string& actionName, MouseButton button);
    void mapAxis(const std::string& axisName, Key positiveKey, Key negativeKey);
    
    // Action checking
    bool isActionPressed(const std::string& actionName) const;
    bool isActionJustPressed(const std::string& actionName) const;
    bool isActionJustReleased(const std::string& actionName) const;
    float getActionValue(const std::string& actionName) const;
    
    // Callbacks
    void setActionCallback(const std::string& actionName, std::function<void()> callback);
    void setAxisCallback(const std::string& axisName, std::function<void(float)> callback);
    
    // Raw input events (for window callbacks)
    void onKeyEvent(int key, int scancode, int action, int mods);
    void onMouseButtonEvent(int button, int action, int mods);
    void onMouseMoveEvent(double xpos, double ypos);
    void onScrollEvent(double xoffset, double yoffset);
    
    // Utility
    void clearMappings();
    void setMousePosition(const glm::vec2& position);
    void setCursorMode(int mode);

private:
    void updateActionValues();
    void processActionCallbacks();
    int convertKeyToCode(Key key) const;
    int convertMouseButtonToCode(MouseButton button) const;
};

#endif
