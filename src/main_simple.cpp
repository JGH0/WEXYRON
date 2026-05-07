#include <iostream>
#include <GLFW/glfw3.h>

int main() {
    std::cout << "Simple GLFW Window Test" << std::endl;
    
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW!" << std::endl;
        return -1;
    }
    
    // Create window
    GLFWwindow* window = glfwCreateWindow(800, 600, "Simple Window", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window!" << std::endl;
        glfwTerminate();
        return -1;
    }
    
    // Make context current
    glfwMakeContextCurrent(window);
    
    std::cout << "Window created successfully!" << std::endl;
    std::cout << "Press ESC or close window to exit." << std::endl;
    
    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // Process events
        glfwPollEvents();
        
        // Check for ESC key
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        
        // Swap buffers
        glfwSwapBuffers(window);
    }
    
    // Cleanup
    glfwDestroyWindow(window);
    glfwTerminate();
    
    std::cout << "Window closed successfully!" << std::endl;
    return 0;
}
