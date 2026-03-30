#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <memory>
#include <string>
#include <vector>
#include <functional>

// Forward declarations
class Window;
class Renderer;
class InputManager;
class SceneManager;
class ResourceManager;
class PhysicsEngine;
class AudioManager;
class NetworkManager;

/**
 * @brief Core Engine class - provides foundation for all applications
 * 
 * The Engine is the central hub that manages all core systems.
 * It follows a modular design where each system is independent
 * but can communicate through the Engine.
 */
class Engine {
public:
    struct EngineConfig {
        int windowWidth;
        int windowHeight;
        std::string windowTitle;
        bool fullscreen;
        bool vsync;
        int targetFPS;
        bool enableAudio;
        bool enablePhysics;
        bool enableNetworking;
        
        EngineConfig() : windowWidth(800), windowHeight(600), windowTitle("OpenGL Engine"),
                         fullscreen(false), vsync(true), targetFPS(60),
                         enableAudio(true), enablePhysics(true), enableNetworking(false) {}
    };

private:
    std::unique_ptr<Window> window;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<InputManager> inputManager;
    std::unique_ptr<SceneManager> sceneManager;
    std::unique_ptr<ResourceManager> resourceManager;
    std::unique_ptr<PhysicsEngine> physicsEngine;
    std::unique_ptr<AudioManager> audioManager;
    std::unique_ptr<NetworkManager> networkManager;
    
    EngineConfig config;
    bool isRunning;
    float deltaTime;
    float totalTime;
    int frameCount;
    
    // Engine loop callbacks
    std::function<void()> onInitialize;
    std::function<void(float)> onUpdate;
    std::function<void(float)> onRender;
    std::function<void()> onShutdown;
    
public:
    Engine();
    ~Engine();
    
    // Engine lifecycle
    bool initialize(const EngineConfig& config);
    void run();
    void shutdown();
    
    // System access
    Window* getWindow() const { return window.get(); }
    Renderer* getRenderer() const { return renderer.get(); }
    InputManager* getInputManager() const { return inputManager.get(); }
    SceneManager* getSceneManager() const { return sceneManager.get(); }
    ResourceManager* getResourceManager() const { return resourceManager.get(); }
    PhysicsEngine* getPhysicsEngine() const { return physicsEngine.get(); }
    AudioManager* getAudioManager() const { return audioManager.get(); }
    NetworkManager* getNetworkManager() const { return networkManager.get(); }
    
    // Engine state
    bool getIsRunning() const { return isRunning; }
    float getDeltaTime() const { return deltaTime; }
    float getTotalTime() const { return totalTime; }
    int getFrameCount() const { return frameCount; }
    
    // Configuration
    const EngineConfig& getConfig() const { return config; }
    void setConfig(const EngineConfig& newConfig);
    
    // Callback registration
    void setInitializeCallback(std::function<void()> callback) { onInitialize = callback; }
    void setUpdateCallback(std::function<void(float)> callback) { onUpdate = callback; }
    void setRenderCallback(std::function<void(float)> callback) { onRender = callback; }
    void setShutdownCallback(std::function<void()> callback) { onShutdown = callback; }
    
    // Utility methods
    void stop() { isRunning = false; }
    void requestShutdown() { isRunning = false; }
    
private:
    bool initializeSystems();
    void updateEngine(float dt);
    void renderEngine(float dt);
    void shutdownSystems();
    void calculateDeltaTime();
    void registerEngineCallbacks();
};

#endif
