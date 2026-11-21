/**
 * @file render_window.cpp
 * @brief Window creation per graphics API (see render_window.hpp)
 */

#include "engine/render_window.hpp"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <iostream>

namespace {

GLFWwindow* createGlWindow(int width, int height, const char* title) {
    // The shaders are GLSL 330, so ask for a real 3.3 core context. Some
    // drivers reject #version 330 core in a compatibility context, and others
    // have no compatibility profile at all.
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    // Ask the driver to prefer the dedicated GPU on hybrid laptops.
    glfwWindowHint(GLFW_COCOA_GRAPHICS_SWITCHING, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) {
        // Older/virtual drivers may only offer a compatibility profile, which
        // still runs the GLSL 330 shaders: try that once.
        std::cerr << "Core profile window failed, retrying with a "
                     "compatibility profile..." << std::endl;
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
        window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    }
    if (!window) return nullptr;

    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "Failed to load the OpenGL functions (GLAD)" << std::endl;
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
        return nullptr;
    }
    return window;
}

GLFWwindow* createVulkanWindow(int width, int height, const char* title) {
    if (!glfwVulkanSupported()) {
        std::cerr << "Vulkan: no Vulkan loader found" << std::endl;
        return nullptr;
    }
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    return glfwCreateWindow(width, height, title, nullptr, nullptr);
}

} // namespace

GLFWwindow* createRenderWindow(RenderApi api, int width, int height, const char* title) {
    switch (api) {
        case RenderApi::OpenGL:
            return createGlWindow(width, height, title);
        case RenderApi::Vulkan:
            return createVulkanWindow(width, height, title);
    }
    return nullptr;
}
