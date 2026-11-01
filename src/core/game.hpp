/**
 * @file game.hpp
 * @brief Game class managing the main game loop, window, and game state
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef GAME_HPP
#define GAME_HPP

// glad must come before GLFW (it owns the GL headers), and GLFW must not pull
// in the system GL headers again.
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vector>
#include <glm/glm.hpp>
#include <unordered_map>
#include <filesystem>
#include <cstring>
#include "camera.hpp"
#include "renderer.hpp"
#include "map.hpp"
#include "map_loader.hpp"
#include "thread_pool.hpp"
#include <memory>
#include "../ui/menu.hpp"
#include "../ui/menu_pages.hpp"
#include "settings.hpp"
#include "../systems/weapon.hpp"
#include "../systems/physics.hpp"
#include "../systems/input.hpp"
#include "../systems/enemy.hpp"
#include "../network/network.hpp"
#include "../engine/audio_manager.hpp"

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
     * @brief Begin loading a map in the background (non-blocking)
     *
     * The old map keeps rendering until the new one is ready, then it is
     * swapped in on the main thread. Falls back to a blocking load when a
     * worker GL context can't be created.
     */
    void requestMapLoad(const std::string& mapFile);

    /// @return true while a background map load is in progress
    bool isMapLoading() const { return mapLoader_.isLoading(); }

    /// Finish a background map load if it is ready (call once per frame).
    void pollMapLoad();

    /// @return number of helper threads in the task pool
    unsigned int workerThreadCount() const { return threadPool_.threadCount(); }

    /// Performance profile shown in Settings, mapped onto the renderer and the
    /// background loader. Defaults to Balanced so the game never hammers a
    /// machine unless the player asks for it.
    void setPerformanceLevel(int level);
    int  performanceLevel() const { return perfLevel; }

    /// Live settings (loaded from assets/settings.cfg at startup).
    settings::GameSettings& gameSettings() { return gameSettings_; }
    const settings::GameSettings& gameSettings() const { return gameSettings_; }

    // ── Menu framework glue (used by the free functions in game_input.cpp) ──
    /// Key-edge query: true once per press (edges stored in Game::inp).
    bool menuKeyPressed(int key, GLFWwindow* window);
    /// Forget menu key edges (after a key-binding capture ends).
    void resetMenuKeyEdges(GLFWwindow* window);
    /// Re-read the menu key edges from the keyboard after a screen change, so
    /// a key that is still held is not seen as a new press on the next frame.
    void syncMenuEdges(GLFWwindow* window);

    /**
     * @brief Developer hook: drive the active menu from tests/scripts.
     *
     * Keys are handled exactly like a physical press (no GLFW needed), so UI
     * integration tests can navigate menus on a headless machine:
     *   up down left right enter space escape   - as the key named
     *   capture:<glfwKeyCode>                   - finish a key-binding capture
     * Returns false when no menu is active.
     */
    bool debugMenuKey(const std::string& key);

    /// Print the active menu page (selection, values) to stdout.
    void debugMenuDump() const;

    /// Select the row with this exact label on the active page.
    bool debugMenuSelect(const std::string& label);

    /// Show `page` in the settings view (framework tests).
    void debugShowPage(menu::Page page);
    /// The active view / page / window, for framework tests.
    menu::MenuView* debugMenuView() { return &activeMenuView(); }
    menu::Page* debugMenuPage();
    GLFWwindow* windowHandle() const { return window; }
    int debugGameMode() const { return static_cast<int>(currentGameMode); }

    int debugMenuSelection() const { return activeMenuViewConst().stack.selection(); }
    bool debugMainMenuReady() const { return mainMenu_.stack.current() != nullptr; }
    /// Client id of the live session (0 when not connected).
    uint32_t debugClientId() const {
        return networkClient ? networkClient->getClientId() : 0;
    }
    /// Set the address the connect screen will use (network tests).
    void serverAddressForTest(const std::string& address, uint16_t port) {
        serverAddress = address;
        serverPort = port;
        refreshServerRows();
    }
    int debugShootButton() const { return keyBindings.shootButton; }
    int debugShootKey() const { return keyBindings.shootKey; }
    /// Re-resolve bindings from the current settings (framework tests).
    void applySettingsForTest() { applySettings(); }
    size_t debugMenuDepth() const { return activeMenuViewConst().stack.depth(); }

    /// Scripted run: play `frames` frames of the current state ("game:N").
    void debugRunFrames(int frames);
    /// Audio smoke test: fire the held weapon and step a few frames.
    void debugShootAndStepForTest();
    /// Number of sound effects currently playing (audio smoke test).
    int debugPlayingSoundCount() const;
    /// One frame of the current state with input routing but without drawing
    /// (network tests drive the connect screen with it).
    void debugPumpFrame();
    int debugState() const { return (int)gameState; }
    /// Open a menu screen by name ("main", "settings", "maps", "server").
    void debugOpenMenu(const std::string& name);

    /// Write the current settings to the settings file.
    bool saveSettings();

    /**
     * @brief Pick up changes made to the settings file while the game runs.
     *
     * Called once a second: compares the file's modification time and applies
     * the values whose text changed (so editing assets/settings.cfg takes
     * effect immediately, like reading it at startup).
     */
    void pollSettingsFile();
    /// Apply the current settings to the engine (vsync, performance, bindings).
    void applySettings();
    
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

    /// True while a connection attempt is in progress.
    bool isConnectingToServer() const;

    /// Pump the network session (handshake, packets, loss detection) in any
    /// state, including the connect screen. Called once per frame.
    void updateNetwork();

    /// Set when connecting fails or a live session is lost; shown on the
    /// connect screen until the next attempt. Cleared by connectToServer().
    const std::string& getServerConnectError() const { return serverConnectError_; }
    /// Address/port of the current (or last attempted) session.
    std::string getServerAddress() const;
    uint16_t getServerPort() const;
    
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

    // ── Sound (used by the weapon classes and the menus) ─────────────────
    /// Play a sound effect at a world position (attenuated + panned from the
    /// camera). A no-op while the audio device is unavailable.
    void playSoundEffect(const std::string& name, const glm::vec3& position,
                         float volume = 1.0f, float pitch = 1.0f);
    /// Play an interface sound (2D, no distance falloff).
    void playUiSound(const std::string& name);
    /// Fire sound of the player's own weapon, with a slight random pitch so
    /// repeated shots never sound identical.
    void playGunshot(const std::string& name, float volume = 1.0f);
    /// Trigger click when a weapon with no ammo is fired.
    void playDryFire();
    /// Switch the music track (main menu vs. in-game); called per frame.
    void updateMusicForState();

    /// Game mode type
    enum GameMode {
        DEATHMATCH,
        TEAM_VS_TEAM,
        BOMB_DEFUSAL
    };

private:
    /// Take ownership of a loaded map and make it the active one (GL thread).
    void installMap(std::unique_ptr<Map> loaded);

    /// Shared map-start logic for the map selection menu (ENTER / click).
    void startSelectedMap(const std::string& mapFile);

    // ── Audio ────────────────────────────────────────────────────────────
    /// Load assets/audio, open the device + apply the volume settings.
    void initAudio();
    /// Push the listener (camera) into the mixer; once per frame.
    void updateAudio(float deltaTime);
    /// Footstep sounds while walking, landing after a fall or a jump.
    void updateFootsteps(float deltaTime);
    /// Decide whether the player's head is under water (muffles footsteps).
    void updateUnderwater();

    /// The sound engine (3D positional audio, music, volumes).
    AudioManager audio;
    /// Per-sound volume multipliers applied after loading (see initAudio).
    void applyAudioMix();

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
    
    /// Menu selection (index of the selected row in the main menu page)
    int menuSelection;

    /// Death state
    bool isDead;
    
    /// Map selection (map gameModeRow_ picks a mode instead of a map)
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

    /// True once a match has started (the main menu then offers RESUME).
    bool gameInProgress_ = false;

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

        /// Set when the menu edges must be re-read from the keyboard (after a
        /// screen change). Processed once at the start of the next frame by
        /// Game::syncMenuEdges(), never in the middle of a menu's input pass.
        bool menuEdgesNeedSync = true;

        // Repeat support for the menu navigation keys: how long the key has
        // been held and when it last fired. Holding UP/DOWN scrolls a list.
        double upHeld = 0.0, downHeld = 0.0, leftHeld = 0.0, rightHeld = 0.0;
        double upLast = 0.0, downLast = 0.0, leftLast = 0.0, rightLast = 0.0;

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
        bool gameplayWeaponPressed[5] = {false, false, false, false, false}; // indices 1-4

        InputState() {
            memset(digitPressed,  0, sizeof(digitPressed));
            memset(texKeyPressed, 0, sizeof(texKeyPressed));
        }

        /// Longest frame step, so held keys do not fire spuriously after a stall.
        static constexpr double kMaxHeldStep = 0.1;
        /// Repeat delay and interval for held navigation keys (seconds).
        static constexpr double kRepeatDelay = 0.4;
        static constexpr double kRepeatRate = 0.10;

        /// Forget the pressed-state of the navigation keys. NEVER clears a key
        /// that is still physically held: that would make the framework see a
        /// new press next frame and repeat the action every frame while the
        /// key is held (the key-edge bookkeeping must follow the physical key,
        /// see Game::menuKeyPressed()).
        /// Forget the navigation keys' press state when switching screens.
        ///
        /// This clears *consumed* edges only. The menu-edge bookkeeping is run
        /// by Game::menuKeyPressed() from the physical key state before this is
        /// used; a key that is still held will be marked as already handled on
        /// the next frame, so the action is not repeated every frame (the bug
        /// where holding ENTER changed a setting again and again).
        void resetNavKeys() {
            menuEdgesNeedSync = true;   // edges are re-read at the next frame
            upPressed        = true;
            downPressed      = true;
            leftPressed      = true;
            rightPressed     = true;
            shapeResetPressed = false;
            gameplayGPressed = false;
            for (int i = 0; i < 5; i++) gameplayWeaponPressed[i] = false;
        }
    } inp;

    /// Bindings resolved from `gameSettings_` (GLFW key / mouse codes).
    struct KeyBindings {
        int forward;
        int backward;
        int left;
        int right;
        int jump;
        int menu;
        int shoot;        ///< shoot as a key code (-1 when bound to the mouse)
        int shootKey;     ///< same as `shoot`, explicit name
        int shootButton;  ///< mouse button for shooting (-1 when bound to a key)
        int grenade;
    } keyBindings;

    /// Live settings (bindings, video, gameplay) and the settings file.
    settings::GameSettings gameSettings_;
    std::vector<settings::SettingDef> settingDefs_;
    /// Text of every setting as last read from / written to the file, so
    /// external edits can be detected per setting.
    std::vector<std::string> settingFileText_;
    /// Remember every setting's current file text (after a load or save).
    void rememberSettingsText();
    /// When the settings file was last seen (modification time).
    std::filesystem::file_time_type settingsFileTime_{};
    double nextSettingsPoll_ = 0.0;

    /// Menu screens built by game_menus.cpp. All of them are framework views;
    /// an "active" view is the one belonging to the current menu state.
    menu::MenuView mainMenu_;     ///< main menu (bottom page of the stack)
    menu::MenuView settingsMenu_; ///< settings page, controls pushed on top
    menu::MenuView mapMenu_;      ///< map selection
    menu::MenuView serverMenu_;   ///< connect to server
    /// Set while the current state shows one of the menu views.
    bool menuActive_ = false;

    /// Show `page` in `view` (resets selection/scroll).
    void openMenuView(menu::MenuView& view, menu::Page page);
    /// The view of the current menu state.
    menu::MenuView& activeMenuView();
    const menu::MenuView& activeMenuViewConst() const;
    /// Open the settings screen (values + controls sub-page).
    void openSettings();
    /// Append every key binding row (grouped) to `page`.
    void appendBindingRows(menu::Page& page);
    /// Restore the default key bindings (controls page "RESET BINDINGS").
    void resetBindingsToDefaults();
    /// Rebuild the current server page (list/status/address values change).
    void refreshServerRows();
    /// Poll LAN discovery and type into the IP/port row (SERVER_CONNECT).
    void processServerConnectTextInput();
    /// Open the map selection screen.
    void openMapSelection();
    /// Draw the connect-to-server screen (framework view + text fields).
    void renderServerConnect();
    
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

    // Footstep state (see updateFootsteps)
    float footstepTimer = 0.0f;      ///< time since the last step
    int   footstepIndex = 0;         ///< cycles footstep_1..4
    bool  wasOnGround = true;        ///< ground contact on the previous frame
    float fallTime = 0.0f;           ///< seconds spent falling (landing volume)
    float footstepPitch = 1.0f;      ///< slows down when wading in water
    bool  underwater = false;        ///< camera under the water surface
    float gunshotPitch = 1.0f;       ///< random pitch per shot (not identical)
    std::string activeMusic_;        ///< music track requested last ("" none)
    
    /// Network multiplayer
    NetworkClient* networkClient;
    bool multiplayerMode;

    /// Connect-screen feedback: why the last attempt failed / session dropped.
    std::string serverConnectError_;
    /// Drop a live session that receives nothing for this long (seconds).
    static constexpr float kServerTimeoutSeconds = 5.0f;
    /// Report a lost server from the last network pump.
    void connectionLossDetected();
    /// Address/port of the current (or last attempted) session.
    bool hasServerSession() const;

    /// Background map loading + CPU task pool (sized from the hardware).
    MapLoader mapLoader_;
    ThreadPool threadPool_;

    /// Performance profile. 0 = Low (minimal GPU/CPU work), 1 = Balanced
    /// (default), 2 = High (prepared on load, smaller CPU spikes).
    int perfLevel = 1;
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
     * @brief Switch to the given weapon (no-op if already holding it)
     */
    void switchWeapon(WeaponType weapon);
    
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
     * @brief Draw the active menu (main menu, settings, maps, server list)
     */
    void menu();

    /**
     * @brief Route one frame of input to the active menu
     */
    void processMenuInput();

    /**
     * @brief Open the main menu (rebuilds its rows for the current state)
     */
    void openMainMenu();

    /**
     * @brief Open the "connect to server" screen
     */
    void openServerConnect();

    /**
     * @brief Close the current menu back to the game (ESC on a menu page
     *        that was opened from the game).
     */
    void closeMenuToGame();
    
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
     * @brief Load the settings file (creating it with defaults when missing)
     * and apply it to the engine.
     */
    void loadSettings();

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