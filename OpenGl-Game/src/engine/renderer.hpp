#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Forward declarations
class Shader;
class Texture;
class Mesh;
class Camera;
class Light;
class Material;

/**
 * @brief Rendering system
 * 
 * Handles all rendering operations including:
 * - OpenGL state management
 * - Shader management
 * - Texture management
 * - Geometry rendering
 * - Lighting calculations
 * - Post-processing effects
 */
class Renderer {
public:
    struct RenderStats {
        int drawCalls = 0;
        int triangles = 0;
        int vertices = 0;
        float frameTime = 0.0f;
        float fps = 0.0f;
    };
    
    struct RenderConfig {
        bool enableDepthTest = true;
        bool enableBlending = false;
        bool enableCulling = true;
        glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
        int maxLights = 8;
        bool enableShadows = false;
        bool enablePostProcessing = false;
    };

private:
    RenderConfig config;
    RenderStats stats;
    
    // Default resources
    std::shared_ptr<Shader> defaultShader;
    std::shared_ptr<Shader> texturedShader;
    std::shared_ptr<Shader> litShader;
    
    // Rendering state
    glm::mat4 projectionMatrix;
    glm::mat4 viewMatrix;
    glm::mat4 modelMatrix;
    
    // Lighting
    std::vector<std::shared_ptr<Light>> lights;
    
    // Post-processing
    unsigned int framebuffer;
    unsigned int colorTexture;
    unsigned int depthTexture;
    unsigned int renderQuadVAO;
    
    bool isInitialized = false;

public:
    Renderer();
    ~Renderer();
    
    // Renderer lifecycle
    bool initialize(int width, int height);
    void shutdown();
    void beginFrame();
    void endFrame();
    
    // Configuration
    void setConfig(const RenderConfig& newConfig);
    const RenderConfig& getConfig() const { return config; }
    const RenderStats& getStats() const { return stats; }
    
    // View/Projection
    void setProjectionMatrix(const glm::mat4& projection);
    void setViewMatrix(const glm::mat4& view);
    void setModelMatrix(const glm::mat4& model);
    
    const glm::mat4& getProjectionMatrix() const { return projectionMatrix; }
    const glm::mat4& getViewMatrix() const { return viewMatrix; }
    const glm::mat4& getModelMatrix() const { return modelMatrix; }
    
    // Camera
    void setCamera(const Camera& camera);
    
    // Lighting
    void addLight(std::shared_ptr<Light> light);
    void removeLight(std::shared_ptr<Light> light);
    void clearLights();
    void updateLighting(Shader* shader);
    
    // Rendering operations
    void clear(const glm::vec4& color = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
    void setViewport(int x, int y, int width, int height);
    
    // Geometry rendering
    void renderMesh(const Mesh& mesh, const Material& material);
    void renderQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
    void renderCube(const glm::vec3& position, const glm::vec3& size, const glm::vec4& color);
    void renderSphere(const glm::vec3& position, float radius, const glm::vec4& color);
    void renderText(const std::string& text, const glm::vec2& position, float scale, const glm::vec4& color);
    
    // Advanced rendering
    void renderWithShader(const Mesh& mesh, Shader* shader);
    void renderInstanced(const Mesh& mesh, const std::vector<glm::mat4>& transforms, const Material& material);
    void renderSkybox(std::shared_ptr<Texture> skybox);
    
    // Post-processing
    void enablePostProcessing(bool enable);
    void addPostProcessEffect(const std::string& effectName);
    void removePostProcessEffect(const std::string& effectName);
    
    // Utility
    void resetStats();
    void enableWireframe(bool enable);
    void enableDepthTest(bool enable);
    void enableBlending(bool enable);
    void enableCulling(bool enable);
    
    // Debug rendering
    void renderDebugLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color);
    void renderDebugBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);
    void renderDebugSphere(const glm::vec3& center, float radius, const glm::vec4& color);

private:
    void setupDefaultShaders();
    void setupPostProcessing();
    void cleanupPostProcessing();
    void renderPostProcessQuad();
    void updateStats();
};

#endif
