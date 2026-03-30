#ifndef GAME_APPLICATION_HPP
#define GAME_APPLICATION_HPP

#include "../engine/engine.hpp"
#include "../engine/scene_manager.hpp"
#include "../engine/resource_manager.hpp"
#include <memory>

// Forward declarations
class GameScene;
class MenuScene;

/**
 * @brief Game application class
 * 
 * This is the main application class that sets up the engine
 * and creates the game-specific scenes and systems.
 * It demonstrates how to use the core engine for a game.
 */
class GameApplication {
private:
    std::unique_ptr<Engine> engine;
    std::shared_ptr<MenuScene> menuScene;
    std::shared_ptr<GameScene> gameScene;
    
    bool isInitialized = false;

public:
    GameApplication();
    ~GameApplication();
    
    // Application lifecycle
    bool initialize();
    void run();
    void shutdown();
    
    // Engine access
    Engine* getEngine() const { return engine.get(); }
    SceneManager* getSceneManager() const;
    ResourceManager* getResourceManager() const;
    
    // Game-specific setup
    void setupGameScenes();
    void setupInputMappings();
    void loadGameResources();

private:
    void setupEngineConfig();
    void registerEngineCallbacks();
};

/**
 * @brief Game scene implementation
 * 
 * This is where the actual game logic goes.
 * It uses the engine systems to handle rendering, input, etc.
 */
class GameScene : public Scene {
private:
    // Game-specific objects would go here
    // These would be created using the engine's systems
    
public:
    explicit GameScene(const std::string& name);
    
    // Scene lifecycle
    void onLoad() override;
    void onUnload() override;
    void onEnter() override;
    void onExit() override;
    
    // Update and render
    void update(float deltaTime) override;
    void render(float deltaTime) override;
    void renderUI(float deltaTime) override;
    
private:
    void setupGameObjects();
    void cleanupGameObjects();
};

/**
 * @brief Menu scene implementation
 * 
 * Main menu scene for the game
 */
class MenuScene : public Scene {
private:
    // Menu-specific state
    int selectedOption = 0;
    std::vector<std::string> menuOptions;
    
public:
    explicit MenuScene(const std::string& name);
    
    // Scene lifecycle
    void onLoad() override;
    void onEnter() override;
    
    // Update and render
    void update(float deltaTime) override;
    void render(float deltaTime) override;
    void renderUI(float deltaTime) override;
    
private:
    void handleInput();
    void renderMenu();
};

#endif
