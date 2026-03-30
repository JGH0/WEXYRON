/**
 * @file renderer.hpp
 * @brief Renderer class for OpenGL rendering operations
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include "shader.hpp"
#include "map.hpp"

/**
 * @brief Renderer class handling OpenGL rendering operations
 * 
 * Manages shader programs, vertex data, and rendering of 3D objects.
 * Provides methods for drawing cubes with colors and textures.
 */
class Renderer {
public:
    /**
     * @brief Construct a new Renderer object
     */
    Renderer();
    
    /**
     * @brief Destructor - cleans up OpenGL resources
     */
    ~Renderer();

    /**
     * @brief Initialize renderer resources (shaders, VAOs, VBOs)
     */
    void init();
    
    /**
     * @brief Draw a colored cube with the given model matrix
     * 
     * @param model Model transformation matrix
     * @param color RGB color vector
     */
    void drawCube(const glm::mat4& model, const glm::vec3& color);
    
    /**
     * @brief Draw a textured cube with the given model matrix
     * 
     * @param model Model transformation matrix
     * @param texture Pointer to texture to use
     */
    void drawTexturedCube(const glm::mat4& model, Texture* texture);
    
    /**
     * @brief Draw a colored 2D quad with the given model matrix
     * 
     * @param model Model transformation matrix
     * @param color RGB color vector
     */
    void drawColoredQuad(const glm::mat4& model, const glm::vec3& color);
    
    /**
     * @brief Draw text at specified position
     * 
     * @param text Text to render
     * @param x X position in screen coordinates
     * @param y Y position in screen coordinates
     * @param scale Text scale factor
     */
    void drawText(const std::string& text, float x, float y, float scale = 1.0f);
    
    /**
     * @brief Render an entire map
     * 
     * @param map Pointer to the map to render
     */
    void renderMap(Map* map);
    
    /**
     * @brief Create a quad model matrix for 2D rendering
     * 
     * @param x X position
     * @param y Y position  
     * @param width Width of the quad
     * @param height Height of the quad
     * @return Model transformation matrix
     */
    glm::mat4 createQuadModel(float x, float y, float width, float height);
    
    /**
     * @brief Set the view and projection matrices
     * 
     * @param view View matrix from camera
     * @param projection Projection matrix
     */
    void setViewProjection(const glm::mat4& view, const glm::mat4& projection);
    
    /**
     * @brief Set the OpenGL viewport
     * 
     * @param x X coordinate of viewport
     * @param y Y coordinate of viewport
     * @param width Width of viewport
     * @param height Height of viewport
     */
    void setViewport(int x, int y, int width, int height);
    
    /**
     * @brief Initialize the renderer
     * 
     * @param width Window width
     * @param height Window height
     * @return True if initialization succeeded
     */
    bool initialize(int width, int height);
    
    /**
     * @brief Begin the current frame
     */
    void beginFrame();
    
    /**
     * @brief End the current frame
     */
    void endFrame();
    
    /**
     * @brief Clear the screen with background color
     */
    void clear();
    
    /**
     * @brief Shutdown the renderer and clean up resources
     */
    void shutdown();

private:
    /// Vertex Array Object for cube geometry
    unsigned int cubeVAO;
    /// Vertex Buffer Object for cube geometry
    unsigned int cubeVBO;
    /// Shader program for colored rendering
    Shader* shader;
    /// Shader program for textured rendering
    Shader* texturedShader;
    /// Current view matrix
    glm::mat4 currentView;
    /// Current projection matrix
    glm::mat4 currentProjection;
};

#endif