#include "engine/engine.hpp"
#include <iostream>

int main() {
    // Create engine instance
    Engine engine;
    
    // Configure engine
    Engine::EngineConfig config;
    config.windowWidth = 800;
    config.windowHeight = 600;
    config.windowTitle = "OpenGL Game Engine";
    config.fullscreen = false;
    config.vsync = true;
    config.targetFPS = 60;
    config.enableAudio = true;
    config.enablePhysics = true;
    config.enableNetworking = false;
    
    // Initialize engine
    if (!engine.initialize(config)) {
        std::cerr << "Failed to initialize engine!" << std::endl;
        return -1;
    }
    
    std::cout << "Engine initialized successfully!" << std::endl;
    std::cout << "Press ESC or close window to exit." << std::endl;
    
    // Run engine main loop
    engine.run();
    
    // Engine automatically shuts down when run() returns
    std::cout << "Engine shutdown complete!" << std::endl;
    
    return 0;
}
