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
#include <cstring>
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
    void endFrame(float deltaTime);  ///< Frame-end bookkeeping (FPS counter)
    void frameWait();  ///< Spin-wait to cap FPS based on maxFps
    
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
     * @brief Create a new projectile
     *
     * @param position Starting position
     * @param velocity Initial velocity
     * @param lifetime How long the projectile exists
     * @param owner Owner ID (0 for local player)
     * @param type Projectile type (0=pistol, 1=grenade, 2=flashbang, 3=shotgun)
     * @return true if projectile was created, false if max projectiles reached
     */
    bool createProjectile(const glm::vec3& position, const glm::vec3& velocity, float lifetime, uint32_t owner, int type = 0);
    int getActiveProjectiles() const { return activeProjectiles; }
    const Camera& getCamera() const { return camera; }

    /// Game mode type
    enum GameMode {
        DEATHMATCH,
        TEAM_VS_TEAM,
        BOMB_DEFUSAL
    };

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
    /// Mouse position in framebuffer coordinates (updated each frame when in GUI states)
    float mouseGUIPosX, mouseGUIPosY;
    /// Whether LMB has transitioned from pressed to released this frame (edge trigger)
    bool mouseClicked;
    /// Previous-frame LMB state for edge detection
    bool prevMouseDown;
    /// Hand cursor for hover-over-clickable items
    GLFWcursor* handCursor;
    
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

    /// Death state
    bool isDead;
    
    /// Map selection
    int mapSelection = 0;

    /// Game mode selection
    int gameModeSelection = 0; // 0 = Deathmatch, 1 = Team vs Team

    /// Map player counts (received from server)
    std::vector<int> mapPlayerCounts; // Player count for each map

    /// Dynamic map list loaded from assets/maps folder
    std::vector<std::string> mapFiles;    // Actual filenames (e.g., "simple.map")
    std::vector<std::string> mapDisplayNames; // Display names (e.g., "Simple")

    /// Menu state tracking
    bool menuJustEntered = true; // Reset key states when entering menu

    // ---------------------------------------------------------------
    // All per-frame key-state booleans live here so they can be reset
    // atomically when the game changes state – the root cause of the
    // "stuck navigation after visiting server menu" bug.
    // ---------------------------------------------------------------
    struct InputState {
        // Shared navigation (MENU / MAP_SELECTION / SETTINGS / SERVER_CONNECT)
        bool escapePressed  = false;
        bool upPressed      = false;
        bool downPressed    = false;
        bool enterPressed   = false;
        bool leftPressed    = false;
        bool rightPressed   = false;

        // Server-connect specific
        bool tabPressed       = false;
        bool backspacePressed = false;
        bool periodPressed    = false;
        bool rPressed         = false;
        bool digitPressed[10];

        // Editor specific
        bool gPressed         = false;
        bool mPressed         = false;
        bool shapeResetPressed = false;
        bool nPressed         = false;
        bool f9Pressed        = false;
        bool texKeyPressed[10]; // Now used for texture (1-4) keys
        bool rmbPressed       = false;
        bool qPressed         = false;
        bool ePressed         = false;
        bool lbracketPressed  = false;
        bool rbracketPressed  = false;
        bool upArrowPressed   = false;
        bool downArrowPressed = false;
        bool leftArrowPressed = false;
        bool rightArrowPressed= false;
        bool f1Pressed = false, f2Pressed = false;
        bool f3Pressed = false, f4Pressed = false, f5Pressed = false, f6Pressed = false;
        double lastPlaceTime  = 0.0;
        float sidewaysOffset  = 0.0f;

        // Gameplay specific
        bool gameplayGPressed  = false;

        InputState() {
            memset(digitPressed,  0, sizeof(digitPressed));
            memset(texKeyPressed, 0, sizeof(texKeyPressed));
        }

        /// Reset navigation keys used by menus. Call whenever switching state.
        /// NOTE: enterPressed is intentionally NOT reset here - it must stay true
        /// until the key is physically released so destination states cannot react
        /// to a still-held ENTER (prevents instant map/option selection on entry).
        /// NOTE: escapePressed is intentionally NOT reset here – it has its own
        /// release handler in processInput and must stay true until the key is
        /// physically released, otherwise the menu opens and immediately closes.
        void resetNavKeys() {
            upPressed     = false;
            downPressed   = false;
            leftPressed   = false;
            rightPressed  = false;
            shapeResetPressed = false;
            gameplayGPressed = false;
        }
    } inp;

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

    /// A single menu button with its bounding rect (in virtual UI coords).
    /// Populated each frame by renderMenu(), consumed by processInput().
    struct MenuButton {
        float x0, y0, x1, y1;
        int actionIndex; // enum-like index matching the switch in processInput
    };
    std::vector<MenuButton> menuButtons;
    
    /// Projectile structure for bullets
    struct Projectile {
        glm::vec3 position;
        glm::vec3 velocity;
        float lifetime;
        bool active;
        bool detonated; // Prevents double detonation
        uint32_t owner; // 0 = local player, other = remote player ID
        int type; // 0=pistol bullet, 1=grenade, 2=flashbang, 3=shotgun pellet
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
    
    /// Bomb structure for bomb defusal mode
    struct Bomb {
        glm::vec3 position;
        bool planted;
        bool defused;
        float plantTime;
        float explodeTime; // 20 seconds after planting
        float defuseTime; // 4 seconds to defuse
        float defuseProgress; // Progress of defusing (0 to 4)
        int plantingTeam; // Which team planted it
        bool active;
    };
    
    /// Bomb site structure (plant locations)
    struct BombSite {
        glm::vec3 position;
        glm::vec3 size;
        bool active;
        char label; // 'A' or 'B'
    };
    
    /// Maximum number of projectiles
    static const int MAX_PROJECTILES = 50;
    /// Maximum number of bullet holes
    static const int MAX_BULLET_HOLES = 100;
    /// Maximum number of enemies
    static const int MAX_ENEMIES = 10;
    /// Maximum number of explosions
    static const int MAX_EXPLOSIONS = 10;
    /// Maximum number of bomb sites
    static const int MAX_BOMB_SITES = 4;
    
    /// Array of active projectiles
    Projectile projectiles[MAX_PROJECTILES];
    /// Swept-collision tracking: previous position per slot (reset on create)
    glm::vec3 projectilePrevPos[MAX_PROJECTILES];
    /// Whether projectilePrevPos has been initialised for this slot
    bool projectilePrevPosInitialized[MAX_PROJECTILES];
    /// Array of bullet holes on walls
    BulletHole bulletHoles[MAX_BULLET_HOLES];
    /// Array of enemies
    Enemy enemies[MAX_ENEMIES];
    /// Array of explosions
    Explosion explosions[MAX_EXPLOSIONS];
    /// Array of bomb sites
    BombSite bombSites[MAX_BOMB_SITES];
    /// Bomb for bomb defusal mode
    Bomb bomb;
    /// Number of active projectiles
    int activeProjectiles;
    /// Number of bullet holes
    int bulletHoleCount;
    /// Number of active enemies
    int activeEnemies;
    /// Number of bomb sites
    int activeBombSites;
    
    /// Player health
    float playerHealth;
    float playerMaxHealth;
    float playerDamageCooldown;
    
    /// Screen damage effect
    float damageEffectTime; // Time remaining for damage screen effect
    
    /// Network multiplayer
    NetworkClient* networkClient;
    bool multiplayerMode;
    int playerCount; // Number of connected players including self
    
    /// Remote players (for multiplayer)
    struct RemotePlayer {
        uint32_t id;
        glm::vec3 position;
        glm::vec3 velocity;
        glm::vec3 renderPosition; // For smooth interpolation
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
    socket_t discoverySocketFd;
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
    float editorBlockRotation = 0.0f;  // Y-axis rotation for blocks
    float editorBlockPitch = 0.0f;      // X-axis rotation (pitch) for blocks

    /// Editor spawn point placement mode
    enum EditorSpawnMode {
        SPAWN_NONE,          // Normal block placement
        SPAWN_DEATHMATCH,     // Place DM spawn points
        SPAWN_TEAM_A,        // Place Team A (Red) spawn points
        SPAWN_TEAM_B,        // Place Team B (Blue) spawn points
        SPAWN_BOMB_SITE_A,   // Place Bomb Site A
        SPAWN_BOMB_SITE_B    // Place Bomb Site B
    };
    EditorSpawnMode editorSpawnMode = SPAWN_NONE;
    
    /// Bomb Defusal Mode
    bool bombDefusalMode; // Whether bomb defusal mode is active
    int attackingTeam; // Which team is planting (0 = Team A, 1 = Team B)
    bool roundActive; // Whether a round is currently in progress
    float roundTime; // Time elapsed in current round
    float roundEndTimer; // Countdown before next round starts after bomb explodes
    int teamADeaths; // Number of deaths for Team A
    int teamBDeaths; // Number of deaths for Team B
    bool spectatorMode; // Whether player is in spectator mode
    int spectatorTarget; // Index of player being spectated (-1 = free cam)
    bool isPlanting; // Whether player is currently planting
    bool isDefusing; // Whether player is currently defusing
    float plantProgress; // Progress of planting (0 to 3)
    float defuseProgress; // Progress of defusing (0 to 4)

    /// Editor mode (block placement vs shape creation)
    enum EditorMode {
        MODE_BLOCK,      // Place individual blocks
        MODE_SHAPE       // Create shapes by selecting points
    };
    EditorMode editorMode = MODE_BLOCK;

    /// Shape creation state
    struct ShapeCreation {
        int selectedPoints = 0;  // Number of points selected (0-3 for corners)
        glm::vec3 corners[3] = {glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f)};    // Corner positions
        std::vector<glm::vec3> nodes;  // Control points for curvature
        bool editingNodes = false;     // Whether editing nodes after corners selected
    };
    ShapeCreation shapeCreation;

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
    bool showFps;
    int  maxFps;
    bool vsyncEnabled;
    int  fpsFrameCount;
    float fpsTimer;
    int  fpsDisplayValue;
    GameMode currentGameMode;
    int playerTeam; // 0 = Team A/Red, 1 = Team B/Blue (for Team vs Team mode)
    
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
     * @brief Check projectile collision with players (PVP)
     */
    void checkProjectilePlayerCollision();
    
    /**
     * @brief Damage local player
     */
    void damagePlayer(float damage);
    
    /**
     * @brief Check player vs player collision (PVP)
     */
    void checkPlayerPlayerCollision();
    
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
     * @brief Render bomb sites and bomb
     */
    void renderBombSites();

    /**
     * @brief Render bomb defusal HUD (team, bomb status, progress)
     */
    void renderBombDefusalHUD();
    
    /**
     * @brief Update bomb state (timer, explosion, etc.)
     * @param deltaTime Time since last frame
     */
    void updateBomb(float deltaTime);
    
    /**
     * @brief Start planting bomb at current location
     */
    void startPlanting();
    
    /**
     * @brief Start defusing bomb
     */
    void startDefusing();
    
    /**
     * @brief Check if player is near a bomb site
     * @return Index of bomb site, -1 if not near any
     */
    int getNearbyBombSite();
    
    /**
     * @brief Check if player is near the planted bomb
     * @return true if near bomb, false otherwise
     */
    bool isNearBomb();
    
    /**
     * @brief End current round and prepare for next
     * @param winner Winning team (0 = Team A, 1 = Team B, -1 = draw)
     */
    void endRound(int winner);
    
    /**
     * @brief Start a new round
     */
    void startRound();
    
    /**
     * @brief Update spectator camera
     */
    void updateSpectator();
    
    /**
     * @brief Update round end timer (delay before next round)
     * @param deltaTime Time since last frame
     */
    void updateRoundEndTimer(float deltaTime);
    
    /**
     * @brief Render damage screen effect when player takes damage
     */
    void renderDamageEffect();
    void renderHealthEffect();
    
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
     * @brief Execute the action for a settings item (called by ENTER or mouse click)
     */
    void actOnSettingsItem(int index);

    /**
     * @brief Save key bindings to file
     */
    void saveKeyBindings();
    
    /**
     * @brief Load key bindings from file
     */
    void loadKeyBindings();

    /**
     * @brief Scan assets/maps folder for .map files and populate map lists
     */
    void scanMapsFolder();

    /**
     * @brief Format a map filename for display (e.g., "simple.map" -> "Simple")
     */
    std::string formatMapDisplayName(const std::string& filename);
};

#endif