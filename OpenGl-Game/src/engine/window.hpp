#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <string>
#include <functional>
#include <GLFW/glfw3.h>

/**
 * @brief Window management system
 * 
 * Handles window creation, management, and events.
 * Provides abstraction over GLFW for window operations.
 */
class Window {
public:
    struct WindowConfig {
        int width;
        int height;
        std::string title;
        bool fullscreen;
        bool vsync;
        bool resizable;
        bool decorated;
        
        WindowConfig() : width(800), height(600), title("OpenGL Window"),
                        fullscreen(false), vsync(true), resizable(true), decorated(true) {}
    };
    
    // Event callbacks
    std::function<void(int, int)> onResize;
    std::function<void(int, int, int, int)> onKey;
    std::function<void(double, double)> onMouseMove;
    std::function<void(int, int, int)> onMouseButton;
    std::function<void(double, double)> onScroll;
    std::function<void(bool)> onFocus;
    std::function<void()> onClose;

private:
    GLFWwindow* glfwWindow;
    WindowConfig config;
    bool hasFocus;
    
    static Window* currentWindow;

public:
    Window();
    ~Window();
    
    // Window lifecycle
    bool create(const WindowConfig& config);
    void destroy();
    void update();
    
    // Window state
    bool shouldClose() const;
    void setShouldClose(bool shouldClose);
    void swapBuffers();
    
    // Window properties
    int getWidth() const;
    int getHeight() const;
    void setSize(int width, int height);
    std::string getTitle() const;
    void setTitle(const std::string& title);
    bool isFullscreen() const;
    void setFullscreen(bool fullscreen);
    bool isFocused() const { return hasFocus; }
    
    // Window operations
    void show();
    void hide();
    void minimize();
    void maximize();
    void restore();
    void focus();
    
    // Cursor management
    void setCursorMode(int mode);
    void setCursorPosition(double x, double y);
    void getCursorPosition(double& x, double& y) const;
    
    // Input state
    int getKey(int key) const;
    int getMouseButton(int button) const;
    
    // Utility
    GLFWwindow* getGLFWWindow() const { return glfwWindow; }
    bool isValid() const { return glfwWindow != nullptr; }

private:
    void setupCallbacks();
    static void windowResizeCallback(GLFWwindow* window, int width, int height);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseMoveCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void focusCallback(GLFWwindow* window, int focused);
    static void closeCallback(GLFWwindow* window);
};

#endif
