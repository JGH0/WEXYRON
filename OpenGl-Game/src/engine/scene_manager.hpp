#ifndef SCENE_MANAGER_HPP
#define SCENE_MANAGER_HPP

#include <memory>
#include <vector>
#include <string>
#include <stack>

// Forward declarations
class Scene;
class Engine;

/**
 * @brief Scene management system
 * 
 * Manages game scenes, transitions, and lifecycle.
 * Provides a simple way to organize different game states
 * like menus, levels, cutscenes, etc.
 */
class SceneManager {
public:
    enum class TransitionType {
        None,
        Fade,
        Slide,
        Zoom,
        Custom
    };
    
    struct Transition {
        TransitionType type = TransitionType::None;
        float duration = 1.0f;
        float progress = 0.0f;
        bool isTransitioning = false;
        std::shared_ptr<Scene> fromScene;
        std::shared_ptr<Scene> toScene;
    };

private:
    std::vector<std::shared_ptr<Scene>> scenes;
    std::stack<std::shared_ptr<Scene>> sceneStack;
    std::shared_ptr<Scene> currentScene;
    std::shared_ptr<Scene> nextScene;
    
    Transition transition;
    Engine* engine;
    
    bool isInitialized = false;

public:
    explicit SceneManager(Engine* engine);
    ~SceneManager();
    
    // Scene manager lifecycle
    bool initialize();
    void shutdown();
    void update(float deltaTime);
    void render(float deltaTime);
    
    // Scene management
    void addScene(std::shared_ptr<Scene> scene);
    void removeScene(const std::string& sceneName);
    
    // Scene navigation
    void loadScene(const std::string& sceneName);
    void loadScene(std::shared_ptr<Scene> scene);
    void reloadCurrentScene();
    
    // Scene stack (for overlays, menus, etc.)
    void pushScene(const std::string& sceneName);
    void pushScene(std::shared_ptr<Scene> scene);
    void popScene();
    void popToRootScene();
    
    // Scene transitions
    void loadSceneWithTransition(const std::string& sceneName, TransitionType type, float duration = 1.0f);
    void loadSceneWithTransition(std::shared_ptr<Scene> scene, TransitionType type, float duration = 1.0f);
    
    // Scene access
    std::shared_ptr<Scene> getCurrentScene() const;
    std::shared_ptr<Scene> getScene(const std::string& sceneName) const;
    std::shared_ptr<Scene> getNextScene() const;
    
    // Engine access
    Engine* getEngine() const;
    
    // State queries
    bool isTransitioning() const;
    bool hasScene(const std::string& sceneName) const;
    size_t getSceneCount() const;
    size_t getStackSize() const;
    
    // Utility
    void clearAllScenes();
    std::vector<std::string> getSceneNames() const;

private:
    void performSceneTransition();
    void updateTransition(float deltaTime);
    void renderTransition();
    void switchToScene(std::shared_ptr<Scene> newScene);
    void startTransition(std::shared_ptr<Scene> from, std::shared_ptr<Scene> to, TransitionType type, float duration);
};

/**
 * @brief Base scene class
 * 
 * All game scenes should inherit from this class.
 * Provides basic scene functionality and lifecycle methods.
 */
class Scene {
private:
    std::string name;
    bool isActive = false;
    bool isLoaded = false;
    SceneManager* sceneManager;

public:
    explicit Scene(const std::string& name);
    virtual ~Scene() = default;
    
    // Scene lifecycle
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onLoad() {}
    virtual void onUnload() {}
    virtual void onActivate() {}
    virtual void onDeactivate() {}
    
    // Update and render
    virtual void update(float deltaTime) {}
    virtual void render(float deltaTime) {}
    virtual void renderUI(float deltaTime) {}
    
    // Scene access
    const std::string& getName() const { return name; }
    bool getIsActive() const { return isActive; }
    bool getIsLoaded() const { return isLoaded; }
    SceneManager* getSceneManager() const { return sceneManager; }
    
    // Scene state (used by SceneManager)
    void setActive(bool active);
    void setLoaded(bool loaded);
    void setSceneManager(SceneManager* manager) { sceneManager = manager; }
    
    // Utility
    virtual void onResize(int width, int height) {}
    virtual void onPause() {}
    virtual void onResume() {}

protected:
    // Helper methods for scene management
    Engine* getEngine() const;
    void loadScene(const std::string& sceneName);
    void pushScene(const std::string& sceneName);
    void popScene();
};

#endif
