/**
 * @file render_window.hpp
 * @brief Create the game window for a graphics API.
 *
 * Each API needs different GLFW window hints: OpenGL wants a context (and its
 * functions loaded), Vulkan and Direct3D 12 want a window without one. The
 * caller owns GLFW itself (glfwInit / glfwTerminate) and the window afterwards.
 */

#ifndef CUBE_ENGINE_RENDER_WINDOW_HPP
#define CUBE_ENGINE_RENDER_WINDOW_HPP

struct GLFWwindow;

/// Graphics API a window (and its render backend) is created for.
enum class RenderApi {
    OpenGL,  ///< 3.3 core; compatibility profile as a fallback
    Vulkan,  ///< no context; the Vulkan backend creates its own surface
};

/**
 * @brief Create a window set up for `api`.
 *
 * OpenGL: requests a 3.3 core context (retries once with any profile for
 * drivers that only offer a compatibility context), makes it current on the
 * calling thread and loads the GL functions.
 *
 * Vulkan: a window without a client API. Fails when GLFW finds no Vulkan
 * loader (glfwVulkanSupported), e.g. a Mac without MoltenVK.
 *
 * @return the window, or nullptr if no usable window/context could be created
 *         (the caller may then try a fallback driver)
 */
GLFWwindow* createRenderWindow(RenderApi api, int width, int height, const char* title);

#endif // CUBE_ENGINE_RENDER_WINDOW_HPP
