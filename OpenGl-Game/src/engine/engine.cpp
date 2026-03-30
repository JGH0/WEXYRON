#include "engine.hpp"
#include "window.hpp"
#include "renderer.hpp"
#include "input_manager.hpp"
#include "scene_manager.hpp"
#include "resource_manager.hpp"
#include "physics_engine.hpp"
#include "audio_manager.hpp"
#include "network_manager.hpp"
#include <glad/glad.h>

// Forward declarations for incomplete types
class PhysicsWorld {
public:
    PhysicsWorld() = default;
    ~PhysicsWorld() = default;
};

// GLAD function pointer declaration
extern "C" {
    int gladLoadGLLoader(void* (*load)(const char*));
}
#include <iostream>
#include <chrono>
#include <thread>

Engine::Engine() {
    std::cout << "Engine: Initializing..." << std::endl;
}

Engine::~Engine() {
    shutdown();
}

bool Engine::initialize(const EngineConfig& engineConfig) {
    config = engineConfig;
    
    std::cout << "Engine: Starting initialization..." << std::endl;
    
    // Initialize systems
    if (!initializeSystems()) {
        std::cerr << "Engine: Failed to initialize systems!" << std::endl;
        return false;
    }
    
    // Set up engine callbacks
    registerEngineCallbacks();
    
    // Call user initialize callback
    if (onInitialize) {
        onInitialize();
    }
    
    isRunning = true;
    std::cout << "Engine: Initialization complete!" << std::endl;
    return true;
}

void Engine::run() {
    if (!isRunning) {
        std::cerr << "Engine: Cannot run - not initialized!" << std::endl;
        return;
    }
    
    std::cout << "Engine: Starting main loop..." << std::endl;
    
    auto lastTime = std::chrono::high_resolution_clock::now();
    
    while (isRunning && !window->shouldClose()) {
        calculateDeltaTime();
        
        // Update engine systems
        updateEngine(deltaTime);
        
        // Call user update callback
        if (onUpdate) {
            onUpdate(deltaTime);
        }
        
        // Render engine systems
        renderEngine(deltaTime);
        
        // Call user render callback
        if (onRender) {
            onRender(deltaTime);
        }
        
        // Update window
        window->update();
        
        frameCount++;
        
        // Frame rate limiting
        if (config.targetFPS > 0) {
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration<float>(currentTime - lastTime).count();
            float targetFrameTime = 1.0f / config.targetFPS;
            
            if (elapsed < targetFrameTime) {
                auto sleepTime = std::chrono::duration<float>(targetFrameTime - elapsed);
                std::this_thread::sleep_for(std::chrono::duration_cast<std::chrono::milliseconds>(sleepTime));
            }
            
            lastTime = std::chrono::high_resolution_clock::now();
        }
    }
    
    std::cout << "Engine: Main loop ended." << std::endl;
}

void Engine::shutdown() {
    if (!isRunning) return;
    
    std::cout << "Engine: Shutting down..." << std::endl;
    
    // Call user shutdown callback
    if (onShutdown) {
        onShutdown();
    }
    
    // Shutdown systems
    shutdownSystems();
    
    isRunning = false;
    std::cout << "Engine: Shutdown complete!" << std::endl;
}

bool Engine::initializeSystems() {
    // Initialize window
    window.reset(new Window());
    Window::WindowConfig windowConfig;
    windowConfig.width = config.windowWidth;
    windowConfig.height = config.windowHeight;
    windowConfig.title = config.windowTitle;
    windowConfig.fullscreen = config.fullscreen;
    windowConfig.vsync = config.vsync;
    
    if (!window->create(windowConfig)) {
        std::cerr << "Engine: Failed to create window!" << std::endl;
        return false;
    }
    
    // Initialize GLAD (load OpenGL function pointers)
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Engine: Failed to initialize GLAD!" << std::endl;
        return false;
    }
    
    std::cout << "OpenGL initialized successfully!" << std::endl;
    
    // Initialize renderer
    renderer.reset(new Renderer());
    if (!renderer->initialize(config.windowWidth, config.windowHeight)) {
        std::cerr << "Engine: Failed to initialize renderer!" << std::endl;
        return false;
    }
    
    // Initialize input manager
    inputManager.reset(new InputManager());
    if (!inputManager->initialize()) {
        std::cerr << "Engine: Failed to initialize input manager!" << std::endl;
        return false;
    }
    
    // Initialize scene manager
    sceneManager.reset(new SceneManager(this));
    if (!sceneManager->initialize()) {
        std::cerr << "Engine: Failed to initialize scene manager!" << std::endl;
        return false;
    }
    
    // Initialize resource manager
    resourceManager.reset(new ResourceManager());
    if (!resourceManager->initialize()) {
        std::cerr << "Engine: Failed to initialize resource manager!" << std::endl;
        return false;
    }
    
    // Initialize physics engine (if enabled)
    if (config.enablePhysics) {
        physicsEngine.reset(new PhysicsEngine());
        PhysicsEngine::PhysicsConfig physicsConfig;
        if (!physicsEngine->initialize(physicsConfig)) {
            std::cerr << "Engine: Failed to initialize physics engine!" << std::endl;
            return false;
        }
    }
    
    // Initialize audio manager (if enabled)
    if (config.enableAudio) {
        audioManager.reset(new AudioManager());
        AudioManager::AudioConfig audioConfig;
        if (!audioManager->initialize(audioConfig)) {
            std::cerr << "Engine: Failed to initialize audio manager!" << std::endl;
            return false;
        }
    }
    
    // Initialize network manager (if enabled)
    if (config.enableNetworking) {
        networkManager.reset(new NetworkManager());
        NetworkManager::NetworkConfig networkConfig;
        if (!networkManager->initialize(networkConfig)) {
            std::cerr << "Engine: Failed to initialize network manager!" << std::endl;
            return false;
        }
    }
    
    return true;
}

void Engine::updateEngine(float dt) {
    // Update input manager
    inputManager->update();
    
    // Update scene manager
    sceneManager->update(dt);
    
    // Update physics engine (if enabled)
    if (physicsEngine) {
        physicsEngine->update(dt);
    }
    
    // Update audio manager (if enabled)
    if (audioManager) {
        audioManager->update(dt);
    }
    
    // Update network manager (if enabled)
    if (networkManager) {
        networkManager->update(dt);
    }
    
    totalTime += dt;
}

void Engine::renderEngine(float dt) {
    // Begin frame
    renderer->beginFrame();
    
    // Render scene manager
    sceneManager->render(dt);
    
    // End frame
    renderer->endFrame();
    
    // Swap buffers
    window->swapBuffers();
}

void Engine::shutdownSystems() {
    // Shutdown in reverse order
    if (networkManager) {
        networkManager->shutdown();
        networkManager.reset();
    }
    
    if (audioManager) {
        audioManager->shutdown();
        audioManager.reset();
    }
    
    if (physicsEngine) {
        physicsEngine->shutdown();
        physicsEngine.reset();
    }
    
    if (resourceManager) {
        resourceManager->shutdown();
        resourceManager.reset();
    }
    
    if (sceneManager) {
        sceneManager->shutdown();
        sceneManager.reset();
    }
    
    if (inputManager) {
        inputManager->shutdown();
        inputManager.reset();
    }
    
    if (renderer) {
        renderer->shutdown();
        renderer.reset();
    }
    
    if (window) {
        window->destroy();
        window.reset();
    }
}

void Engine::calculateDeltaTime() {
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
    
    // Clamp delta time to prevent large jumps
    if (deltaTime > 0.1f) {
        deltaTime = 0.1f;
    }
}

void Engine::registerEngineCallbacks() {
    // Connect window events to input manager
    window->onKey = [this](int key, int scancode, int action, int mods) {
        inputManager->onKeyEvent(key, scancode, action, mods);
    };
    
    window->onMouseButton = [this](int button, int action, int mods) {
        inputManager->onMouseButtonEvent(button, action, mods);
    };
    
    window->onMouseMove = [this](double xpos, double ypos) {
        inputManager->onMouseMoveEvent(xpos, ypos);
    };
    
    window->onScroll = [this](double xoffset, double yoffset) {
        inputManager->onScrollEvent(xoffset, yoffset);
    };
    
    window->onResize = [this](int width, int height) {
        renderer->setViewport(0, 0, width, height);
        config.windowWidth = width;
        config.windowHeight = height;
    };
}

void Engine::setConfig(const EngineConfig& newConfig) {
    config = newConfig;
    
    // Apply config changes to systems
    if (window) {
        window->setSize(config.windowWidth, config.windowHeight);
        window->setTitle(config.windowTitle);
    }
    
    if (renderer) {
        renderer->setViewport(0, 0, config.windowWidth, config.windowHeight);
    }
}
