#include "game_application.hpp"
#include "../engine/window.hpp"
#include "../engine/renderer.hpp"
#include "../engine/input_manager.hpp"
#include <iostream>

GameApplication::GameApplication() {
    std::cout << "GameApplication: Creating..." << std::endl;
}

GameApplication::~GameApplication() {
    shutdown();
}

bool GameApplication::initialize() {
    std::cout << "GameApplication: Initializing..." << std::endl;
    
    // Create engine
    engine.reset(new Engine());
    
    // Setup engine configuration
    setupEngineConfig();
    
    // Register engine callbacks
    registerEngineCallbacks();
    
    // Initialize engine
    if (!engine->initialize(engine->getConfig())) {
        std::cerr << "GameApplication: Failed to initialize engine!" << std::endl;
        return false;
    }
    
    // Setup game-specific systems
    setupGameScenes();
    setupInputMappings();
    loadGameResources();
    
    isInitialized = true;
    std::cout << "GameApplication: Initialization complete!" << std::endl;
    return true;
}

void GameApplication::run() {
    if (!isInitialized) {
        std::cerr << "GameApplication: Cannot run - not initialized!" << std::endl;
        return;
    }
    
    std::cout << "GameApplication: Starting game..." << std::endl;
    
    // Load initial scene (menu)
    if (auto sceneMgr = getSceneManager()) {
        sceneMgr->loadScene("Menu");
    }
    
    // Run engine main loop
    engine->run();
    
    std::cout << "GameApplication: Game ended." << std::endl;
}

void GameApplication::shutdown() {
    if (!isInitialized) return;
    
    std::cout << "GameApplication: Shutting down..." << std::endl;
    
    if (engine) {
        engine->shutdown();
        engine.reset();
    }
    
    isInitialized = false;
    std::cout << "GameApplication: Shutdown complete!" << std::endl;
}

SceneManager* GameApplication::getSceneManager() const {
    return engine ? engine->getSceneManager() : nullptr;
}

ResourceManager* GameApplication::getResourceManager() const {
    return engine ? engine->getResourceManager() : nullptr;
}

void GameApplication::setupEngineConfig() {
    Engine::EngineConfig config;
    config.windowWidth = 800;
    config.windowHeight = 600;
    config.windowTitle = "OpenGL Game";
    config.fullscreen = false;
    config.vsync = true;
    config.targetFPS = 60;
    config.enableAudio = true;
    config.enablePhysics = true;
    config.enableNetworking = false;
    
    engine->setConfig(config);
}

void GameApplication::registerEngineCallbacks() {
    // Set up engine callbacks if needed
    engine->setInitializeCallback([this]() {
        std::cout << "GameApplication: Engine initialized!" << std::endl;
    });
    
    engine->setUpdateCallback([this](float deltaTime) {
        // Game-wide update logic can go here
    });
    
    engine->setRenderCallback([this](float deltaTime) {
        // Game-wide rendering logic can go here
    });
    
    engine->setShutdownCallback([this]() {
        std::cout << "GameApplication: Engine shutting down!" << std::endl;
    });
}

void GameApplication::setupGameScenes() {
    auto sceneMgr = getSceneManager();
    if (!sceneMgr) return;
    
    // Create game scenes
    menuScene = std::make_shared<MenuScene>("Menu");
    gameScene = std::make_shared<GameScene>("Game");
    
    // Add scenes to scene manager
    sceneMgr->addScene(menuScene);
    sceneMgr->addScene(gameScene);
    
    std::cout << "GameApplication: Game scenes created!" << std::endl;
}

void GameApplication::setupInputMappings() {
    auto inputMgr = engine ? engine->getInputManager() : nullptr;
    if (!inputMgr) return;
    
    // Map game actions
    inputMgr->mapAction("MoveForward", InputManager::Key::W);
    inputMgr->mapAction("MoveBackward", InputManager::Key::S);
    inputMgr->mapAction("MoveLeft", InputManager::Key::A);
    inputMgr->mapAction("MoveRight", InputManager::Key::D);
    inputMgr->mapAction("Jump", InputManager::Key::Space);
    inputMgr->mapAction("Shoot", InputManager::MouseButton::Left);
    inputMgr->mapAction("Menu", InputManager::Key::Escape);
    
    // Set up input callbacks
    inputMgr->setActionCallback("Menu", [this]() {
        auto sceneMgr = getSceneManager();
        if (sceneMgr) {
            auto currentScene = sceneMgr->getCurrentScene();
            if (currentScene && currentScene->getName() == "Game") {
                sceneMgr->loadScene("Menu");
            } else if (currentScene && currentScene->getName() == "Menu") {
                sceneMgr->loadScene("Game");
            }
        }
    });
    
    std::cout << "GameApplication: Input mappings set up!" << std::endl;
}

void GameApplication::loadGameResources() {
    auto resourceMgr = getResourceManager();
    if (!resourceMgr) return;
    
    // Add resource search paths
    resourceMgr->addSearchPath("assets/textures");
    resourceMgr->addSearchPath("assets/shaders");
    resourceMgr->addSearchPath("assets/models");
    resourceMgr->addSearchPath("assets/audio");
    
    // Load essential resources
    // These would be actual game resources
    // auto texture = resourceMgr->loadTexture("player.png");
    // auto shader = resourceMgr->loadShader("basic.vert", "basic.frag");
    
    std::cout << "GameApplication: Resources loaded!" << std::endl;
}

// GameScene implementation
GameScene::GameScene(const std::string& name) : Scene(name) {
    std::cout << "GameScene: Creating scene '" << name << "'" << std::endl;
}

void GameScene::onLoad() {
    std::cout << "GameScene: Loading scene '" << getName() << "'" << std::endl;
    setupGameObjects();
}

void GameScene::onUnload() {
    std::cout << "GameScene: Unloading scene '" << getName() << "'" << std::endl;
    cleanupGameObjects();
}

void GameScene::onEnter() {
    std::cout << "GameScene: Entering scene '" << getName() << "'" << std::endl;
}

void GameScene::onExit() {
    std::cout << "GameScene: Exiting scene '" << getName() << "'" << std::endl;
}

void GameScene::update(float deltaTime) {
    // Update game logic here
    // This would use the engine's systems
}

void GameScene::render(float deltaTime) {
    // Render game objects here
    // This would use the engine's renderer
}

void GameScene::renderUI(float deltaTime) {
    // Render UI here
    // This would use the engine's renderer
}

void GameScene::setupGameObjects() {
    // Create game objects using engine systems
    std::cout << "GameScene: Setting up game objects..." << std::endl;
}

void GameScene::cleanupGameObjects() {
    // Clean up game objects
    std::cout << "GameScene: Cleaning up game objects..." << std::endl;
}

// MenuScene implementation
MenuScene::MenuScene(const std::string& name) : Scene(name) {
    menuOptions = {"Play Game", "Settings", "Exit"};
    std::cout << "MenuScene: Creating menu scene '" << name << "'" << std::endl;
}

void MenuScene::onLoad() {
    std::cout << "MenuScene: Loading menu..." << std::endl;
}

void MenuScene::onEnter() {
    std::cout << "MenuScene: Entering menu..." << std::endl;
    selectedOption = 0;
}

void MenuScene::update(float deltaTime) {
    handleInput();
}

void MenuScene::render(float deltaTime) {
    // Render menu background
    auto engine = getEngine();
    if (engine && engine->getRenderer()) {
        engine->getRenderer()->clear(glm::vec4(0.1f, 0.1f, 0.2f, 1.0f));
    }
}

void MenuScene::renderUI(float deltaTime) {
    renderMenu();
}

void MenuScene::handleInput() {
    auto engine = getEngine();
    if (!engine) return;
    
    auto inputMgr = engine->getInputManager();
    if (!inputMgr) return;
    
    // Handle menu navigation
    if (inputMgr->isActionJustPressed("MoveUp")) {
        selectedOption = (selectedOption - 1 + menuOptions.size()) % menuOptions.size();
    }
    
    if (inputMgr->isActionJustPressed("MoveDown")) {
        selectedOption = (selectedOption + 1) % menuOptions.size();
    }
    
    if (inputMgr->isActionJustPressed("Shoot")) {
        switch (selectedOption) {
            case 0: // Play Game
                if (auto sceneMgr = getSceneManager()) {
                    sceneMgr->loadScene("Game");
                }
                break;
            case 1: // Settings
                // TODO: Implement settings scene
                break;
            case 2: // Exit
                if (engine) {
                    engine->requestShutdown();
                }
                break;
        }
    }
}

void MenuScene::renderMenu() {
    // This would render the actual menu UI
    // For now, we'll just print to console
    std::cout << "=== MAIN MENU ===" << std::endl;
    for (size_t i = 0; i < menuOptions.size(); ++i) {
        std::cout << (i == selectedOption ? "> " : "  ") << menuOptions[i] << std::endl;
    }
}
