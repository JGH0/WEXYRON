#ifndef GAME_HPP
#define GAME_HPP

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <glm/glm.hpp>

// Forward declarations
class Renderer;
class Camera;
class Map;
class Input;
class UI;
class Weapon;
class Physics;
class Enemy;

/**
 * @brief Main game class - now simplified and modular
 */
class Game {
public:
    enum GameState {
        MENU,
        PLAYING,
        MAP_SELECTION,
        SETTINGS,
        PAUSED,
        GAME_OVER
    };

private:
    // Core systems
    GLFWwindow* window;
    Renderer* renderer;
    Camera* camera;
    Map* currentMap;
    Input* input;
    UI* ui;
    Weapon* currentWeapon;
    Physics* physics;
    Enemy* enemy;
    
    // Game state
    GameState gameState;
    int windowWidth, windowHeight;
    bool firstMouse;
    float lastX, lastY;
    
    // Player data
    glm::vec3 playerPos;
    int playerHealth;
    int playerMaxHealth;
    bool infiniteAmmoMode;
    
    // Projectiles
    static const int MAX_PROJECTILES = 100;
    struct Projectile {
        glm::vec3 position;
        glm::vec3 velocity;
        float lifetime;
        bool active;
    };
    Projectile projectiles[MAX_PROJECTILES];
    int activeProjectiles;

public:
    Game(int width, int height);
    ~Game();
    
    // Main game loop
    void run();
    bool shouldClose() const;
    
    // Game systems
    void update(float deltaTime);
    void render();
    void processInput();
    
    // Game state management
    void setGameState(GameState state);
    GameState getGameState() const { return gameState; }
    
    // Weapon system access
    const glm::vec3& getCameraPosition() const;
    const glm::vec3& getCameraFront() const;
    bool createProjectile(const glm::vec3& position, const glm::vec3& velocity, float lifetime);
    int getActiveProjectiles() const { return activeProjectiles; }
    
    // Map management
    void loadMap(const char* filename);
    Map* getCurrentMap();
    
    // Player management
    const glm::vec3& getPlayerPosition() const { return playerPos; }
    int getPlayerHealth() const { return playerHealth; }
    int getPlayerMaxHealth() const { return playerMaxHealth; }
    bool isInfiniteAmmoMode() const { return infiniteAmmoMode; }
    
    // Projectile access for enemy system
    const Projectile* getProjectiles() const { return projectiles; }
    int getMaxProjectiles() const { return MAX_PROJECTILES; }
};

#endif
