/**
 * @file game.hpp
 * @brief Game class managing the main game loop, window, and game state
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef GAME_HPP
#define GAME_HPP

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <glm/glm.hpp>
#include <unordered_map>
#include "camera.hpp"
#include "renderer.hpp"
#include "map.hpp"
#include "../ui/menu.hpp"
#include "../systems/weapon.hpp"
#include "../systems/physics.hpp"
#include "../systems/input.hpp"
#include "../systems/enemy.hpp"
#include "../network/network.hpp"

/**
 * @brief Main game class managing the application lifecycle
 * 
 * Handles window creation, input processing, game loop,
 * rendering, and game state management.
 */
class Game {
public:
    /**
     * @brief Construct a new Game object
     * 
     * @param width Window width in pixels
     * @param height Window height in pixels
     */
    Game(int width, int height);
    
    /**
     * @brief Destructor - cleans up GLFW resources
     */
    ~Game();

    /**
     * @brief Initialize game systems and resources
     */
    void init();
    
    /**
     * @brief Process user input
     * 
     * @param deltaTime Time delta for frame-independent input processing
     */
    void processInput(float deltaTime);
    
    /**
     * @brief Update game state
     * 
     * @param deltaTime Time delta for frame-independent updates
     */
    void update(float deltaTime);
    
    /**
     * @brief Render the current frame
     */
    void render();
    
    /**
     * @brief Clean up game resources
     */
    void cleanup();
    
    /**
     * @brief Load a new map
     * 
     * @param mapFile Path to the map file
     * @return true if loaded successfully, false otherwise
     */
    bool loadMap(const std::string& mapFile);
    
    /**
     * @brief Get the current map
     * 
     * @return Map* Pointer to the current map
     */
    Map* getCurrentMap();

    /**
     * @brief Check if the game should close
     * 
     * @return true if the window should close, false otherwise
     */
    bool shouldClose() const;

    /**
     * @brief Connect to multiplayer server
     * @param address Server IP address (e.g., "127.0.0.1" or "192.168.1.5")
     * @param port Server port
     * @return true if connected successfully
     */
    bool connectToServer(const std::string& address, uint16_t port = 7777);
    
    /**
     * @brief Disconnect from server
     */
    void disconnectFromServer();
    
    /**
     * @brief Check if connected to multiplayer server
     */
    bool isConnectedToServer() const;
    
    /**
     * @brief Enable/disable multiplayer mode
     */
    void setMultiplayerMode(bool enabled) { multiplayerMode = enabled; }
    
    /**
     * @brief Check if in multiplayer mode
     */
    bool isMultiplayerMode() const { return multiplayerMode; }

// Weapon system access
    const glm::vec3& getCameraPosition() const { return camera.Position; }
    const glm::vec3& getCameraFront() const { return camera.Front; }
    bool createProjectile(const glm::vec3& position, const glm::vec3& velocity, float lifetime);
    int getActiveProjectiles() const { return activeProjectiles; }
    
private:
    /// Current game map
    Map* currentMap;
    /// Main game window
    GLFWwindow* window;
    /// Camera for scene navigation
    Camera camera;
    /// Renderer for drawing objects
    Renderer renderer;
    
    // New weapon system
    Weapon* currentWeaponInstance;
    Pistol* pistol;
    PumpShotgun* shotgun;
    Grenade* grenade;
    Flashbang* flashbang;
    
    // Physics system
    Physics physics;
    /// Player position
    glm::vec3 playerPos;
    /// Player velocity
    glm::vec3 playerVelocity;
    /// Is player currently jumping
    bool isJumping;
    /// Jump cooldown
    float jumpCooldown;
    /// Current score
    int score;
    /// Last frame time for delta time calculation
    float lastFrame;
    /// First mouse movement flag
    bool firstMouse;
    /// Last mouse X position
    float lastX, lastY;
    
    /// Game state (playing, menu, settings)
    enum GameState {
        MENU,
        PLAYING,
        SETTINGS,
        MAP_SELECTION,
        SERVER_CONNECT,
        MAP_EDITOR,
        DEAD
    } gameState;
    
    /// Weapon types
    enum WeaponType {
        PISTOL,
        PUMP_SHOTGUN,
        GRENADE,
        FLASHBANG
    };
    
    /// Current weapon
    WeaponType currentWeapon;
    
    /// Menu selection
    int menuSelection;
    
    /// Settings selection
    int settingsSelection;
    
    /// Map selection
    int mapSelection = 0;
    
    /// Key bindings
    struct KeyBindings {
        int forward;
        int backward;
        int left;
        int right;
        int jump;
        int menu;
        int shoot;
    } keyBindings;
    
    /// Projectile structure for bullets
    struct Projectile {
        glm::vec3 position;
        glm::vec3 velocity;
        float lifetime;
        bool active;
    };
    
    /// Bullet hole structure for wall impacts
    struct BulletHole {
        glm::vec3 position;
        glm::vec3 normal;
        float size;
        float lifetime;
    };

    /// Enemy structure for AI opponents (simplified for debugging)
    struct Enemy {
        float x, y, z;  // Position
        float health;
        bool active;
        bool alive;
    };
    
    /// Explosion structure for grenade effects
    struct Explosion {
        glm::vec3 position;
        float scale;
        float maxScale;
        float lifetime;
        bool active;
        glm::vec3 color;
    };
    
    /// Maximum number of projectiles
    static const int MAX_PROJECTILES = 50;
    /// Maximum number of bullet holes
    static const int MAX_BULLET_HOLES = 100;
    /// Maximum number of enemies
    static const int MAX_ENEMIES = 10;
    /// Maximum number of explosions
    static const int MAX_EXPLOSIONS = 10;
    
    /// Array of active projectiles
    Projectile projectiles[MAX_PROJECTILES];
    /// Array of bullet holes on walls
    BulletHole bulletHoles[MAX_BULLET_HOLES];
    /// Array of enemies
    Enemy enemies[MAX_ENEMIES];
    /// Array of explosions
    Explosion explosions[MAX_EXPLOSIONS];
    /// Number of active projectiles
    int activeProjectiles;
    /// Number of bullet holes
    int bulletHoleCount;
    /// Number of active enemies
    int activeEnemies;
    
    /// Player health
    float playerHealth;
    float playerMaxHealth;
    float playerDamageCooldown;
    
    /// Screen damage effect
    float damageEffectTime; // Time remaining for damage screen effect
    
    /// Network multiplayer
    NetworkClient* networkClient;
    bool multiplayerMode;
    
    /// Remote players (for multiplayer)
    struct RemotePlayer {
        uint32_t id;
        glm::vec3 position;
        glm::vec3 velocity;
        float yaw, pitch;
        float health;
        bool isDead;
        bool active;
    };
    static const int MAX_REMOTE_PLAYERS = 8;
    RemotePlayer remotePlayers[MAX_REMOTE_PLAYERS];
    
    /// Server connection
    std::string serverAddress;
    uint16_t serverPort;
    bool enteringServerAddress;
    int serverConnectSelection;

    /// LAN server discovery (UDP broadcast)
    struct DiscoveredServer {
        std::string name;
        std::string ip;
        uint16_t port;
        float lastSeenSeconds;
    };
    int discoverySocketFd;
    float discoveryTimeSeconds;
    float discoverySendCooldown;
    std::vector<DiscoveredServer> discoveredServers;
    int discoveredSelection;
    bool focusServerList;
    std::unordered_map<std::string, size_t> discoveredIndexByKey;

    /// Map Editor
    bool editorNoclip = true;
    float editorSpeed = 15.0f;
    glm::vec3 editorGridPos = glm::vec3(0.0f);
    glm::vec3 editorBlockSize = glm::vec3(2.0f, 2.0f, 2.0f);
    int editorSelectedTexture = 1;
    bool editorShowGrid = true;
    float editorGridSize = 1.0f;
    float editorBlockOffset = 5.0f;  // Distance in front of player for block placement
    
    /// Death state
    bool isDead;
    
    /// Shooting cooldown
    float shootCooldown;
    
    /// Weapon system
    float pistolCooldown;
    float shotgunCooldown;
    
    /// Ammo system
    int pistolAmmo;
    int shotgunAmmo;
    int grenadeCount;
    int flashbangCount;
    
    /// Simple grenade system (using projectile array)
    float grenadeCooldown;
    float flashbangCooldown;
    
    /// Flashbang effect system
    float flashEffectTime;
    bool isFlashed;
    
    /// Game modes
    bool infiniteAmmoMode;
    
    /**
     * @brief Update player physics including gravity and collisions
     * 
     * @param deltaTime Time since last frame
     */
    void updatePlayerPhysics(float deltaTime);
    
    /**
     * @brief Handle player jumping
     * 
     * Sets upward velocity when player is on ground and not already jumping
     */
    void handleJump();
    
    /**
     * @brief Shoot projectile from current weapon
     */
    void shoot();
    
    /**
     * @brief Switch weapon based on key input
     */
    void switchWeapon(int weaponNumber);
    
    /**
     * @brief Shoot specific weapon types
     */
    void shootPistol();
    void shootShotgun();
    void throwGrenade();
    void throwFlashbang();
    
    /**
     * @brief Create simple explosion at position
     */
    void createSimpleExplosion(glm::vec3 position);
    
    /**
     * @brief Create flashbang effect at position
     */
    void createFlashbangEffect(glm::vec3 position);
    
    /**
     * @brief Update flashbang effect
     */
    void updateFlashbangEffect(float deltaTime);
    
    /**
     * @brief Render flashbang effect
     */
    void renderFlashbangEffect();
    
    /**
     * @brief Update all active explosions
     * 
     * @param deltaTime Time since last frame
     */
    void updateExplosions(float deltaTime);
    
    /**
     * @brief Render all active explosions
     */
    void renderExplosions();
    
    /**
     * @brief Update all active projectiles
     * 
     * @param deltaTime Time since last frame
     */
    void updateProjectiles(float deltaTime);
    
    /**
     * @brief Check projectile collision with walls
     * 
     * @param projectile The projectile to check
     * @return true if hit wall, false otherwise
     */
    bool checkProjectileCollision(Projectile& projectile);
    
    /**
     * @brief Create a bullet hole on a wall
     * 
     * @param position Impact position
     * @param normal Wall normal vector
     */
    void createBulletHole(const glm::vec3& position, const glm::vec3& normal);
    
    /**
     * @brief Render all bullet holes
     */
    void renderBulletHoles();
    
    /**
     * @brief Spawn an enemy at the given position
     *
     * @param position Spawn position
     */
    void spawnEnemy(const glm::vec3& position);
    
    /**
     * @brief Update all active enemies
     *
     * @param deltaTime Time since last frame
     */
    void updateEnemies(float deltaTime);
    
    /**
     * @brief Check if projectile hits an enemy
     *
     * @param projectile The projectile to check
     * @return true if hit enemy, false otherwise
     */
    bool checkProjectileEnemyCollision(Projectile& projectile);
    
    /**
     * @brief Damage an enemy
     *
     * @param enemyIndex Index of enemy to damage
     * @param damage Amount of damage to deal
     */
    void damageEnemy(int enemyIndex, float damage);
    
    /**
     * @brief Check enemy collision with player and deal damage
     *
     * @param deltaTime Time since last frame
     */
    void checkEnemyPlayerCollision(float deltaTime);
    
    /**
     * @brief Render all enemies
     */
    void renderEnemies();
    
    /**
     * @brief Render crosshair in center of screen
     */
    void renderCrosshair();
    
    /**
     * @brief Render damage screen effect when player takes damage
     */
    void renderDamageEffect();
    
    /**
     * @brief Render death screen when player dies
     */
    void renderDeathScreen();
    
    /**
     * @brief Reset game to initial state after death
     */
    void resetGame();
    
    /**
     * @brief GLFW framebuffer size callback
     * 
     * @param window GLFW window pointer
     * @param width New framebuffer width
     * @param height New framebuffer height
     */
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    
    /**
     * @brief GLFW mouse position callback
     * 
     * @param window GLFW window pointer
     * @param xpos Mouse X position
     * @param ypos Mouse Y position
     */
    static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
    
    /**
     * @brief GLFW scroll callback
     * 
     * @param window GLFW window pointer
     * @param xoffset Scroll X offset
     * @param yoffset Scroll Y offset
     */
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    
    /**
     * @brief Process game-specific input (WASD movement)
     * 
     * @param deltaTime Time delta for frame-independent movement
     */
    void processGameInput(float deltaTime);
    
    /**
     * @brief Check for collectible collisions
     */
    void checkCollectibles();
    
    /**
     * @brief Spawn a new collectible at a random position
     */
    void spawnCollectible();
    
    /**
     * @brief Initialize key bindings to defaults
     */
    void initKeyBindings();
    
    /**
     * @brief Render main menu
     */
    void renderMenu();
    
    /**
     * @brief Render settings menu
     */
    void renderSettings();
    
    /**
     * @brief Render map selection menu
     */
    void renderMapSelection();
    
    /**
     * @brief Process menu input
     */
    void processMenuInput();
    
    /**
     * @brief Process settings input
     */
    void processSettingsInput();
    
    /**
     * @brief Process map selection input
     */
    void processMapSelectionInput();
    
    /**
     * @brief Render server connection menu
     */
    void renderServerConnect();
    
    /**
     * @brief Process server connection input
     */
    void processServerConnectInput();
    
    /**
     * @brief Render map editor
     */
    void renderEditor();
    
    /**
     * @brief Process map editor input
     */
    void processEditorInput();
    
    /**
     * @brief Save current map to file
     */
    void saveMap(const std::string& filename);
    
    /**
     * @brief Save key bindings to file
     */
    void saveKeyBindings();
    
    /**
     * @brief Load key bindings from file
     */
    void loadKeyBindings();
};

#endif