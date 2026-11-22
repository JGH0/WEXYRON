/**
 * @file game.hpp
 * @brief Game class managing the main game loop, window, and game state
 * @author OpenGL Game Developer
 * @date 2025
 */

#ifndef GAME_HPP
#define GAME_HPP

// Game code uses GLFW for windowing and input only; all drawing goes through
// the engine Renderer, so no graphics API headers are needed here.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vector>
#include <glm/glm.hpp>
#include <unordered_map>
#include <filesystem>
#include <chrono>
#include <cstring>
#include "engine/camera.hpp"
#include "engine/renderer.hpp"
#include "engine/render_window.hpp"
#include "engine/gltf_model.hpp"
#include "humanoid.hpp"
#include "engine/map.hpp"
#include "engine/map_loader.hpp"
#include "engine/thread_pool.hpp"
#include <memory>
#include "../ui/menu.hpp"
#include "../ui/menu_pages.hpp"
#include "settings.hpp"
#include "../systems/weapon.hpp"
#include "../systems/physics.hpp"
#include "../systems/input.hpp"
#include "../systems/enemy.hpp"
#include "../network/network.hpp"
#include "engine/audio_manager.hpp"

class LocalBotHost;

/// One human player in an open pre-match setup (from the server's lobby state).
struct LobbyPlayerView {
    uint32_t id = 0;
    std::string name;
    uint8_t team = 0;
    bool ready = false;
};

/// State of one map's pre-match setup, mirrored from the server. The map
/// selection lists these as joinable matches and the setup screen renders the
/// roster from it.
struct LobbyView {
    std::string mapName;
    int gameMode = 0;
    int matchFormat = 0;
    int botPool = 0;
    int botsTeamA = 0;
    int botsTeamB = 0;
    int botAccuracy = 0;
    int botMaxAccuracy = 100;
    int readyCount = 0;
    std::vector<LobbyPlayerView> players;

    bool empty() const { return players.empty(); }
};

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
    void beginFrame();               ///< Frame start, before input (latency markers)
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
    int debugGameState() const { return static_cast<int>(gameState); }
    int debugGameMode() const { return static_cast<int>(currentGameMode); }
    void debugSetGameModeForTest(int mode) {
        currentGameMode = static_cast<GameMode>(mode);
    }
    int debugMatchFormat() const { return static_cast<int>(matchFormat_); }
    void debugSetMatchFormatForTest(int format) {
        matchFormat_ = static_cast<MatchFormat>(format);
    }
    int debugTeamAScore() const { return teamAScore_; }
    int debugTeamBScore() const { return teamBScore_; }
    bool debugMatchOver() const { return matchOver_; }
    int debugLocalBotCount() const;
    /// Force the round-end sequence (tests: movement window / popup timer).
    void debugEndRoundForTest(int winner, int reason) {
        endRound(winner, static_cast<RoundEndReason>(reason));
    }
    float debugRoundEndTimerForTest() const { return roundEndTimer; }

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
    /// Open a menu screen by name ("main", "settings", "gameplay", "video",
    /// "audio", "framegen", "maps", "server").
    void debugOpenMenu(const std::string& name);

    // ── Spawn / movement tests ───────────────────────────────────────────────
    /// Start a real match on `mapFile` exactly like the map menu does
    /// (installMap -> resetGame -> PLAYING).
    bool debugStartMapForTest(const std::string& mapFile);
    /// Step real gameplay frames without input, with the current map loaded.
    void debugStepForTest(int frames, float dt);
    /// Put the camera at a fixed eye position / yaw / pitch (degrees) so a
    /// rendered frame is reproducible (render capture / backend parity tests).
    void debugSetViewForTest(const glm::vec3& eye, float yawDeg, float pitchDeg);
    /// The live camera (read-only, for tests).
    const Camera& debugCamera() const { return camera; }
    /// The renderer (tests: frame capture, active backend).
    Renderer& debugRenderer() { return renderer; }
    /// Start the single-player bots (default setup) on the map started with
    /// debugStartMapForTest, so tests also draw skinned glTF bodies.
    void debugStartLocalBotsForTest(const std::string& mapFile) {
        activeMapFile_ = mapFile;
        startLocalBots();
    }
    /// Press the movement/jump keys for `frames` frames (real input path).
    void debugStepWithInputForTest(int frames, float dt, float forward, float strafe);
    /// Player feet position, velocity and on-ground flag.
    void debugPlayerState(glm::vec3& feet, glm::vec3& velocity, bool& onGround) const;
    /// Live map pointer (nullptr when a map is loading).
    Map* debugCurrentMap() const { return currentMap; }
    /// True when the player body overlaps solid geometry (stuck).
    bool debugPlayerStuck() const;
    /// The normal "died and came back" path (resetGame).
    void debugRespawnForTest() { resetGame(); }
    /// Install a hand-built map (body collision tests) and enter PLAYING.
    bool debugInstallMapForTest(std::unique_ptr<Map> map);
    /// Enter PLAYING with the current map without loading a file (and take the
    /// player out of the match menu, or every ESC key would reopen it).
    void debugEnterPlayingForTest() {
        gameState = PLAYING;
        gameInProgress_ = true;
        menuJustEntered = false;
    }
    /// Renderer diagnostics for the rendering test: map draws in the last frame.
    int debugWorldDrawsLastFrame() const { return renderer.debugWorldDrawsLastFrame(); }
    size_t debugMapInstancesLastFrame() const { return renderer.debugMapInstancesLastFrame(); }
    /// Faces submitted for the held weapon viewmodel in the last frame.
    size_t debugViewmodelInstancesLastFrame() const {
        return renderer.debugViewmodelInstancesLastFrame();
    }
    size_t debugViewmodelModelsLastFrame() const {
        return renderer.debugViewmodelModelsLastFrame();
    }
    /// Press jump exactly like a key press does: a fresh press that starts the
    /// jump (a test helper, so several jumps can follow each other).
    void debugJumpForTest() { isJumping = false; handleJump(); }
    /// Detonate a grenade at `at` exactly like a grenade would (blast + damage).
    void debugExplodeForTest(const glm::vec3& at);
    /// Current player health (grenade test).
    float debugPlayerHealth() const { return playerHealth; }
    /// HUD match statistics (tests can verify kills/deaths and round counters).
    int debugKills() const { return kills_; }
    int debugDeaths() const { return deaths_; }
    int debugRoundNumber() const { return roundNumber_; }
    /// Current held weapon id (see Game::WeaponType).
    int debugCurrentWeapon() const { return static_cast<int>(currentWeapon); }
    /// Ammo of the held weapon (weapon-system value, not the legacy mirror).
    int debugCurrentAmmo() const {
        return currentWeaponInstance ? currentWeaponInstance->getAmmo() : 0;
    }
    /// Switch the held weapon like pressing 1-5 (viewmodel screenshots/tests).
    void debugSelectWeaponForTest(int index) {
        switchWeapon(static_cast<WeaponType>(index % 5));
    }
    /// Force the sniper scope on/off for screenshots and tests (bypasses RMB).
    void debugSetScopedForTest(bool scoped) {
        debugScopeOverride_ = true;
        debugScopedValue_ = scoped;
    }
    /// Current bindings (key test).
    int debugForwardKey() const { return keyBindings.forward; }
    int debugJumpKey() const { return keyBindings.jump; }
    int debugSprintKey() const { return keyBindings.sprint; }
    int debugSneakKey() const { return keyBindings.sneak; }
    int debugProneKey() const { return keyBindings.prone; }
    int debugGrenadeKey() const { return keyBindings.grenade; }
    /// Local stance (0 standing, 1 crouching, 2 prone) and eye height for tests.
    int debugStanceForTest() const { return static_cast<int>(stance_); }
    float debugEyeHeightForTest() const { return eyeHeightCurrent_; }
    /// Queue wheel notches exactly like scrollCallback (tests).
    void debugScrollWeaponForTest(int direction) { pendingWeaponScroll_ = direction; }
    /// Held weapon index (0 pistol, 1 shotgun, 2 sniper, 3 grenade, 4 flash).
    int debugWeaponForTest() const { return static_cast<int>(currentWeapon); }
    /// Infinite-ammo flag actually used by the held weapon.
    bool debugWeaponInfiniteForTest() const {
        return currentWeaponInstance && currentWeaponInstance->isInfiniteAmmo();
    }
    /// True when the held weapon would allow another shot right now.
    bool debugWeaponCanShootForTest() const {
        return currentWeaponInstance && currentWeaponInstance->canShoot();
    }
    /// Run one frame of real gameplay input holding exactly these keys (a
    /// keyboard press, as far as processGameInput can tell).
    void debugStepWithKeysForTest(float dt, const std::vector<int>& keys);
    /// Player feet Y (key test: did the key press jump?).
    float debugFeetY() const { return playerPos.y; }
    /// Vertical speed (key test: was the key held jump a real take-off?).
    float debugVelocityY() const { return playerVelocity.y; }
    /// Teleport for physics tests: place the feet and set the vertical speed.
    void debugSetFeetForTest(const glm::vec3& feet) { playerPos = feet; }
    void debugSetVerticalVelocityForTest(float vy) { playerVelocity.y = vy; }
    /// Face the camera in a known direction for movement tests (yaw in degrees).
    void debugSetYawForTest(float yaw) { camera.Yaw = yaw; camera.updateCameraVectors(); }
    /// True when the player stands on a floor right now (feet on a surface).
    bool debugStandingOnFloorForTest() const { return isOnGround(); }
    /// Keys held during debugStepWithKeysForTest (empty = real keyboard).
    std::vector<int> debugKeysHeldForTest;
    /// Is `key` pressed? Either the real keyboard or a test's scripted keys.
    bool debugKeyDownForTest(int key) const;
    /// Feet position of a remote player as this client believes it.
    bool debugRemotePlayerFeetForTest(uint32_t id, glm::vec3& feet) const {
        return getRemotePlayerFeet(id, feet);
    }


    /// First active projectile position (map/projectile boundary regression test).
    bool debugFirstProjectileForTest(glm::vec3& position, int& type) const {
        for (int i = 0; i < MAX_PROJECTILES; ++i) {
            if (!projectiles[i].active) continue;
            position = projectiles[i].position;
            type = projectiles[i].type;
            return true;
        }
        return false;
    }

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
     * @param type Projectile type (0=pistol, 1=grenade, 2=flashbang, 3=shotgun, 4=sniper)
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
    /// Queue the bolt-action rattle (delay lets the gunshot ring out first).
    void scheduleSniperBolt(float delay);
    /// Trigger click when a weapon with no ammo is fired.
    void playDryFire();
    /// Switch the music track (main menu vs. in-game); called per frame.
    void updateMusicForState();

    /// Game mode type
    enum GameMode {
        DEATHMATCH,      ///< free-for-all deathmatch
        TEAM_VS_TEAM,    ///< team deathmatch: deathmatch rules with two teams
        BOMB_DEFUSAL     ///< the only round-based mode
    };

    /// Match length / win-condition preset. Free play keeps the old endless
    /// session; normal and swift add score limits and half-time side switches.
    enum MatchFormat {
        MATCH_FREE_PLAY = 0, ///< endless (old behaviour)
        MATCH_NORMAL    = 1, ///< first to 13 rounds / 50 kills
        MATCH_SWIFT     = 2  ///< first to 7 rounds / 25 kills
    };

private:
    /// Take ownership of a loaded map and make it the active one (GL thread).
    void installMap(std::unique_ptr<Map> loaded);

    /// Publish the current menu / match / editor state to Discord. No-op when
    /// Rich Presence is disabled or Discord is not running.
    void updateDiscordPresence();

    /// Hide/grab the cursor for gameplay (PLAYING / MAP_EDITOR): focus the
    /// window, enable raw motion where supported, hide the cursor and forget
    /// the last mouse position so the first move does not spin the camera.
    /// Safe to call on every PLAYING frame via capturePlayCursor(false) from
    /// processInput; the reset-on-transition path calls it once.
    void capturePlayCursor(bool resetLook = true);

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
    /// True when this launch requested Vulkan but had to fall back to OpenGL
    /// (shown on VIDEO so a restart that changed nothing is explained).
    bool vulkanFallback_ = false;
    
    // New weapon system
    Weapon* currentWeaponInstance;
    Pistol* pistol;
    PumpShotgun* shotgun;
    Sniper* sniper;
    Grenade* grenade;
    Flashbang* flashbang;

    /// Sniper scope (held SCOPE binding, right mouse by default): zooms the
    /// camera and draws the scope overlay while the sniper is held.
    bool sniperScoped_ = false;
    /// Screenshot/test override for the scope state (see debugSetScopedForTest).
    bool debugScopeOverride_ = false;
    bool debugScopedValue_ = false;
    static constexpr float kDefaultFov = 45.0f;   ///< hip-fire / normal FOV
    static constexpr float kSniperFov  = 14.0f;   ///< scoped FOV (approx. 3.2x)

    // Bolt-action sniper: the rattle is delayed until after the shot, and
    // switching to the rifle requires racking it before it can fire.
    bool  sniperBoltSoundPending_ = false;
    float sniperBoltSoundDelay_ = 0.0f;
    static constexpr float kSniperRackTime = 0.85f;
    
    // Physics system
    Physics physics;
    /// Player position
    glm::vec3 playerPos;
    /// Player velocity
    glm::vec3 playerVelocity;
    /// Is player currently jumping
    bool isJumping;
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
        TEAM_SETUP,
        MAP_EDITOR,
        DEAD
    } gameState;
    
    /// Weapon types
    enum WeaponType {
        PISTOL,
        PUMP_SHOTGUN,
        SNIPER,
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
    int gameModeSelection = 0; // 0 = Deathmatch, 1 = Team Deathmatch

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

        /// Jump key held: holding it keeps jumping on every landing.
        bool jumpHeld         = false;

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
        bool gameplayPronePressed = false;
        bool gameplayWeaponPressed[5] = {false, false, false, false, false}; // indices 0-4

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
            gameplayPronePressed = false;
            for (int i = 0; i < 5; i++) gameplayWeaponPressed[i] = false;
        }
    } inp;

    /// Keyboard state for a binding, false for unbound (-1) keys and for
    /// mouse-button codes.
    bool keyDown(int key) const;
    /// Mouse-button state for a binding, false for unbound (-1) / invalid.
    bool mouseDown(int button) const;
    /// Either kind of binding: keyboard code or encoded mouse button.
    bool bindingDown(int code) const;

    /// Bindings resolved from `gameSettings_` (GLFW key / mouse codes).
    struct KeyBindings {
        int forward;
        int backward;
        int left;
        int right;
        int jump;
        int sprint;
        int sneak;        ///< crouch (hold)
        int prone;        ///< lie down / crawl (toggle)
        int menu;
        int shoot;        ///< shoot as a key code (-1 when bound to the mouse)
        int shootKey;     ///< same as `shoot`, explicit name
        int shootButton;  ///< mouse button for shooting (-1 when bound to a key)
        int scopeKey;     ///< scope as a key code (-1 when bound to the mouse)
        int scopeButton;  ///< mouse button for scoping (-1 when bound to a key)
        int weaponSlot[5];///< select pistol/shotgun/sniper/grenade/flashbang
        int grenade;
        int use;          ///< plant / defuse
        int unstuck;
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
    menu::MenuView teamMenu_;     ///< pre-match team / bot setup
    /// Set while the current state shows one of the menu views.
    bool menuActive_ = false;

    /// Show `page` in `view` (resets selection/scroll).
    void openMenuView(menu::MenuView& view, menu::Page page);
    /// The view of the current menu state.
    menu::MenuView& activeMenuView();
    const menu::MenuView& activeMenuViewConst() const;
    /// Open the settings screen (values + controls sub-page).
    void openSettings();
    /// One settings group ("GAMEPLAY", "VIDEO", "AUDIO", ...) as a sub-screen.
    void openSettingsGroup(const std::string& group);
    /// Key bindings sub-screen.
    void openControlsSettings();
    /// FRAME GENERATION sub-screen (opened from VIDEO).
    void openFrameGenSettings();
    /// Update that page's ACTIVE row from the renderer (every frame).
    void refreshFrameGenStatus();
    /// Apply + save after a row changed, with a feedback blip.
    void onSettingRowChanged();
    /// Restore the default key bindings (controls page "RESET BINDINGS").
    void resetBindingsToDefaults();
    /// Rebuild the current server page (list/status/address values change).
    void refreshServerRows();
    /// Poll LAN discovery and type into the IP/port row (SERVER_CONNECT).
    void processServerConnectTextInput();
    /// Open the map selection screen (and rebuild its open-lobby rows).
    void openMapSelection();
    /// Rebuild the map selection rows without resetting the menu selection.
    void refreshMapSelectionRows();
    /// Draw the connect-to-server screen (framework view + text fields).
    void renderServerConnect();

    // ── Pre-match team / bot setup ──────────────────────────────────────────
    /// Open the setup screen for the map selected in the map menu.
    void openTeamSetup();
    /// Rebuild the setup rows (bot pool / values may have changed).
    void refreshTeamSetupRows();
    /// Push the current setup (map, team, bots, accuracy, ready) to a connected
    /// server. `publishLobby` false only updates the match settings; it is used
    /// after the server started the match so the client does not rejoin.
    void sendLobbySetup(bool freshMatch = false, bool publishLobby = true);
    /// Leave the open pre-match setup (back to map selection / disconnect).
    void leaveLobby();
    /// Server published the roster / ready state of one map's setup.
    void handleLobbyState(const LobbyStatePacket& packet);
    /// Every player in our setup pressed READY: start the agreed match.
    void handleLobbyStart(const LobbyStartPacket& packet);
    /// Find the mirrored setup for a map file, or null when there is none.
    const LobbyView* findLobby(const std::string& mapFile) const;
    /// Local player's roster name (Discord username, or "playerX").
    std::string playerDisplayName() const;
    /// Start the pending map with the current setup.
    void beginMatch();
    /// Bot counts for the current mode. Deathmatch has one shared pool, so
    /// every bot is started on side A (the AI ignores teams there); team modes
    /// use the Team A / Team B split from the setup screen.
    int setupBotsTeamA() const;
    int setupBotsTeamB() const;
    /// The old startSelectedMap() body: install the map and enter PLAYING.
    void startMatchNow(const std::string& mapFile);
    /// Apply a lobby state received from the server.
    void applyLobbyState(uint8_t team, uint8_t pool, uint8_t botsA,
                         uint8_t botsB, uint8_t accuracy, uint8_t maxAccuracy,
                         uint8_t matchFormat);
    /// True when a local (single-player) bot host is running.
    bool localBotsActive() const { return localBots_ != nullptr; }
    /// Create the single-player bot host on the freshly installed map.
    void startLocalBots();
    void stopLocalBots();
    /// Advance the local bot AI, copy bodies to remotePlayers and update the
    /// mode's team kill counters.
    void updateLocalBots(float deltaTime);
    /// Resolve damage from the player's shot/blast against a local bot.
    bool damageLocalBot(uint32_t id, float damage);

    // ── Match length / win conditions ───────────────────────────────────────
    int matchKillTarget() const;
    int matchRoundTarget() const;
    /// True when the current mode/format combination is a team score mode.
    bool matchUsesTeamScore() const;
    /// Evaluate the win condition after a kill or a round result. Sets
    /// matchOver_/matchWinner_ once and freezes the final state.
    void checkMatchEnd();
    /// Restart the current map with a reset scoreboard.
    void restartMatch();
    /// True while the round/gameplay timers must stay frozen.
    bool matchFrozen() const { return matchOver_; }
    
    /// Projectile structure for bullets
    struct Projectile {
        glm::vec3 position;
        glm::vec3 velocity;
        float lifetime;
        bool active;
        bool detonated; // Prevents double detonation
        uint32_t owner; // 0 = local player, other = remote player ID
        int type; // 0=pistol bullet, 1=grenade, 2=flashbang, 3=shotgun pellet, 4=sniper
    };
    
    /// Bullet hole structure for wall impacts
    struct BulletHole {
        glm::vec3 position;
        glm::vec3 normal;
        float size;
        float lifetime;
    };

    /// Enemy structure for AI opponents (practice targets)
    struct Enemy {
        float x, y, z;  // feet position
        float health;
        bool active;
        bool alive;
        float yaw = 0.0f;               ///< facing direction (camera convention)
        float deathTimer = 0.0f;        ///< > 0: dead but still collapsing on screen
        humanoid::Anim anim;            ///< walk / flinch / attack animation
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

    // ── Match statistics (HUD) ─────────────────────────────────────────────
    /// Kills credited to the local player: practice bots plus remote players
    /// this client finished off. Reset when a map/match is installed.
    int kills_ = 0;
    /// Deaths of the local player in the current match.
    int deaths_ = 0;
    /// Round currently being played (1-based) and round wins per team.
    /// In deathmatch modes the team scores are kill counters.
    int roundNumber_ = 1;
    int teamAScore_ = 0;
    int teamBScore_ = 0;

    // ── Match format / pre-match setup ─────────────────────────────────────
    MatchFormat matchFormat_ = MATCH_FREE_PLAY;
    bool matchOver_ = false;
    int matchWinner_ = -1;        ///< 0/1 team, or the local player (DM) when 2
    bool matchEnterHeld_ = false; ///< edge guard for ENTER on the result screen
    std::string pendingMapFile_;  ///< map chosen in the map menu
    /// Team / bot setup (also the last values sent to a server). Team modes
    /// split the bots, deathmatch uses one shared bot count.
    int botSetupTeamA_ = 2;
    int botSetupTeamB_ = 2;
    int botSetupBots_ = 4;        ///< bots in non-team modes
    int botSetupAccuracy_ = 70;   ///< percent
    int serverBotPool_ = 0;       ///< bots the server has available
    int serverBotMaxAccuracy_ = 100;
    bool lobbyDirty_ = false;     ///< rows need a refresh after a server state
    bool lobbyReady_ = false;     ///< local READY toggle in the setup screen
    /// Every open map setup published by the server (map file -> roster).
    std::unordered_map<std::string, LobbyView> lobbyViews_;
    /// Single-player bot host (null in multiplayer / when bots are off).
    std::unique_ptr<LocalBotHost> localBots_;
    bool localBotsPending_ = false; ///< start after the requested map loads

    // ── First-person weapon viewmodel ──────────────────────────────────────
    float weaponBobTime_ = 0.0f;     ///< walk-cycle phase
    float weaponBobAmount_ = 0.0f;   ///< 0..1, eased by movement speed
    float weaponRecoil_ = 0.0f;      ///< 0..1 kick that decays after a shot
    float weaponSwitch_ = 0.0f;      ///< 0..1 lower-the-gun switch animation
    float weaponSwayX_ = 0.0f;       ///< smoothed mouse-look sway
    float weaponSwayY_ = 0.0f;
    float lastViewYaw_ = 0.0f;       ///< previous frame's look direction
    float lastViewPitch_ = 0.0f;

    // ── Player body ───────────────────────────────────────────────────────────
    // Player positions (local, remote, bots) are feet positions; the camera
    // sits eyeHeight above them and the body box spans the whole character.
    // Bodies and hit detection share these numbers so what you see is what
    // you can hit.
    static constexpr float eyeHeight   = 1.6f;   ///< standing camera / eye above the feet
    static constexpr float bodyWidth   = 0.8f;   ///< body box width and depth
    static constexpr float bodyHeight  = 1.8f;   ///< standing body box height
    static constexpr float crouchBodyHeight = 1.2f;
    static constexpr float proneBodyHeight  = 0.65f;
    static constexpr float crouchEyeHeight  = 1.05f;
    static constexpr float proneEyeHeight   = 0.35f;

    /// The local player's stance changes the body box, the eye height and the
    /// movement speed. Sneak is held (CTRL), lie/crawl (C) is a toggle.
    enum Stance {
        STANCE_STANDING,
        STANCE_CROUCHING,
        STANCE_PRONE
    };
    Stance stance_ = STANCE_STANDING;
    /// Smoothed camera height so crouching / lying down eases instead of
    /// snapping the view to the floor.
    float eyeHeightCurrent_ = eyeHeight;

    /// Eye / body dimensions for the current stance.
    float currentEyeHeight() const;
    float currentBodyHeight() const;
    float currentBodyCenterHeight() const;
    /// Dimensions for a networked actor's advertised stance (0/1/2).
    static float bodyHeightForStance(uint8_t stance);
    static float eyeHeightForStance(uint8_t stance);
    /// True when a body of `height` fits at `feet` (used to stand up).
    bool bodyFitsHeight(const glm::vec3& feet, float height) const;
    /// True when the local player may stand up here (no low ceiling).
    bool canStandAtPlayer() const;
    /// Distance from a shot projectile to the nearest body surface below which
    /// the shot counts as a hit.
    static constexpr float bodyHitSlack = 0.15f;
    /// Height above the feet at which a shot or explosion is aimed at a body
    /// (chest height), used for hits that carry no reliable height (grenades).
    static constexpr float bodyCenterHeight = bodyHeight * 0.5f;
    /// PVP point-blank damage per second (see checkPlayerPlayerCollision).
    static constexpr float playerCollisionDps = 15.0f;

    /// Grenade blast: damage falls off linearly to 0 at the radius. A body
    /// only half a block from the blast (eye-thrown grenades land mid-air)
    /// should still take most of it, so the radius is a bit larger than the
    /// visible fireball.
    float explosionRadius_ = 4.0f;
    float explosionDamage_ = 65.0f;

    /// Bomb defusal round clock in offline play. Multiplayer uses the
    /// server's authoritative ROUND_TIME instead.
    static constexpr float kLocalBombRoundTime = 115.0f;

    /// Upward speed of a jump (m/s). With gravity 9.8 that is a 0.8 m peak.
    static constexpr float kJumpSpeed = 4.0f;
    
    /// Screen damage effect
    float damageEffectTime; // Time remaining for damage screen effect

    // Footstep state (see updateFootsteps)
    float footstepTimer = 0.0f;      ///< time since the last step
    int   footstepIndex = 0;         ///< cycles footstep_1..4
    bool  wasOnGround = true;        ///< ground contact on the previous frame
    float fallTime = 0.0f;           ///< seconds spent falling (landing volume)
    float footstepPitch = 1.0f;      ///< slows down when wading in water
    bool  underwater = false;        ///< camera under the water surface
    /// Downhill momentum on slopes steeper than the walkable limit.
    glm::vec3 slopeSlideVelocity_{0.0f};
    /// Visual-only vertical offset that softens stair/kerb step snaps.
    float stepSmoothOffset_ = 0.0f;
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
        uint8_t team = 0; ///< server-assigned team (0 = Team A, 1 = Team B)
        uint8_t stance = 0; ///< 0 standing, 1 crouching, 2 prone
        uint32_t stateSequence = 0; ///< newest applied PlayerState sequence
        humanoid::Anim anim; ///< walk / air / flinch / shooting animation
    };
    static const int MAX_REMOTE_PLAYERS = 16;
    RemotePlayer remotePlayers[MAX_REMOTE_PLAYERS];

    // ── glTF assets (player / bomb / first-person weapons) ──────────────────
    // Shared model data; each actor owns a GltfInstance with its own pose.
    std::unique_ptr<GltfModel> playerModel_;
    std::unique_ptr<GltfModel> bombModel_;
    std::unique_ptr<GltfModel> weaponModels_[5];  ///< indexed by WeaponType
    GltfInstance remoteModelAnims[MAX_REMOTE_PLAYERS];
    GltfInstance enemyModelAnims[MAX_ENEMIES];
    GltfInstance bombModelAnim_;
    GltfInstance weaponModelAnims_[5];
    int playerIdleClip_ = -1;
    int playerWalkClip_ = -1;
    int playerShootClip_ = -1;
    int playerJumpClip_ = -1;
    int playerHitClip_ = -1;
    int playerDeathClip_ = -1;
    int bombBlinkClip_ = -1;
    /// Load assets/models/*.glb and bind the per-actor animation instances.
    void initModels();
    /// Select and blend the player's idle/walk/shoot/death clips.
    void posePlayerInstance(GltfInstance& instance, const humanoid::Anim& anim,
                            uint8_t stance = 0);
    /// Bend the player skeleton into a crouch or prone pose on top of the
    /// active clip (the asset ships no stance clips).
    void applyPlayerStancePose(GltfInstance& instance, uint8_t stance);
    /// Bone node indices of playerModel_ (-1 when the asset is unavailable).
    int playerHipsNode_ = -1;
    int playerSpineNode_ = -1;
    int playerHeadNode_ = -1;
    int playerUpperLegLNode_ = -1;
    int playerUpperLegRNode_ = -1;
    int playerLowerLegLNode_ = -1;
    int playerLowerLegRNode_ = -1;
    int playerFootLNode_ = -1;
    int playerFootRNode_ = -1;

    /// Upright body box of a player at `feet` (feet = bottom centre).
    /// `height` defaults to the standing body; pass currentBodyHeight() for
    /// the local player so crouching / lying down really lowers the hit box.
    static void bodyBox(const glm::vec3& feet, glm::vec3& boxMin, glm::vec3& boxMax,
                        float height = bodyHeight);
    /// Distance from `point` to a body box at `feet` (0 = inside).
    static float distanceToBody(const glm::vec3& point, const glm::vec3& feet,
                                float height = bodyHeight);
    /// True when `point` is inside (or within `slack` of) the body at `feet`.
    static bool pointHitsBody(const glm::vec3& point, const glm::vec3& feet, float slack,
                              float height = bodyHeight);

    /// Result of sweeping a projectile's frame segment against a body box.
    struct ProjectileSweepHit {
        bool hit = false;
        float t = 0.0f;         ///< 0..1 along the segment (0 = segment start)
        glm::vec3 point{0.0f};  ///< first contact on the (fattened) body box
    };
    /// Swept projectile-vs-body test. The body box is fattened by `radius`, so
    /// a fast shot cannot tunnel between two frames and grazes still connect.
    static ProjectileSweepHit sweepProjectileAgainstBody(const glm::vec3& from,
                                                         const glm::vec3& to,
                                                         const glm::vec3& feet,
                                                         float radius,
                                                         float height = bodyHeight);

    /// Stance-aware body box in the actor's local frame. Standing / crouching
    /// use a centred upright box; prone lies forward from the feet, matching
    /// the rotated model.
    static void stanceBodyLocalBox(uint8_t stance, glm::vec3& boxMin,
                                   glm::vec3& boxMax);
    static glm::vec3 worldToBodyLocal(const glm::vec3& point, const glm::vec3& feet,
                                      float yawDeg);
    static glm::vec3 bodyLocalToWorld(const glm::vec3& local, const glm::vec3& feet,
                                      float yawDeg);
    /// Distance from `point` to a ranged player's stance-aware body box.
    static float distanceToStanceBody(const glm::vec3& point, const glm::vec3& feet,
                                      float yawDeg, uint8_t stance);
    /// Swept shot against the stance-aware, yaw-oriented remote body box.
    static ProjectileSweepHit sweepProjectileAgainstStanceBody(
        const glm::vec3& from, const glm::vec3& to, const glm::vec3& feet,
        float yawDeg, float radius, uint8_t stance);

    /// Start of a projectile's current frame segment: its last checked
    /// position (seeded with the spawn position on creation).
    glm::vec3 projectileSegmentStart(const Projectile& projectile) const;
    /// Move `feet` to the nearest position where the body does not overlap
    /// geometry and rests on a floor. Returns false if no such spot was found.
    bool placeAtSafePosition(glm::vec3& feet, float searchRadius = 1.5f) const;
    /// World-space AABB of the player body standing at `feet`.
    void bodyBounds(const glm::vec3& feet, glm::vec3& mn, glm::vec3& mx) const;
    /// True when the body standing at `feet` overlaps solid geometry.
    bool bodyBlockedAt(const glm::vec3& feet) const;
    /// Move the body for one frame: `horizontalSpeed` * dt along X/Z, and
    /// playerVelocity.y * dt vertically, colliding as a rigid box.
    void movePlayerBody(float dt, const glm::vec3& horizontalSpeed);
    /// One frame's horizontal step (X then Z), sliding along blocked axes.
    void horizontalSweep(glm::vec3& feet, glm::vec3& mn, glm::vec3& mx, float dx, float dz);
    /// One frame's vertical step, resting on / stopping at surfaces.
    void verticalSweep(glm::vec3& feet, glm::vec3& mn, glm::vec3& mx, float dy, bool falling);
    /// Highest real (possibly sloped) surface under the body footprint at
    /// `min.y`. `maxRise` looks above the feet, `maxDrop` below it.
    bool groundSurfaceUnderBox(const glm::vec3& min, const glm::vec3& max,
                               float maxDrop, float maxRise,
                               float& height, glm::vec3& normal) const;
    /// True for floors the player can walk up (normal within 40 deg of up).
    static bool isWalkableSlope(const glm::vec3& normal);
    /// Floor support: a surface at most kGroundSnap below the feet.
    bool isSupportedAt(const glm::vec3& feet) const;
    /// Can the player start a jump right now? (standing, not already rising)
    bool isOnGround() const;
    /// Largest step up/down that still counts as standing on a surface.
    static constexpr float kGroundSnap = 0.35f;
    /// Stairs/kerbs can be climbed up to this height, and followed down while
    /// walking. Larger than kGroundSnap so continuous staircases stay smooth.
    static constexpr float kStepHeight = 0.6f;
    /// Walkable slope limit: 40 degrees from vertical (cos 40 deg).
    static constexpr float kMaxWalkableSlopeCos = 0.7660444f;
    /// World position of the remote player `id` (feet), false when unknown.
    bool getRemotePlayerFeet(uint32_t id, glm::vec3& feet) const;

    /// Connection time of each local shot that may still be in flight.
    std::chrono::steady_clock::time_point lastLocalShot_;

    /// Remote-player PVP bookkeeping, indexed like remotePlayers.
    struct PvpRemote {
        float lastHitTime = 0.0f;   ///< timestamp of the last bullet/pellet hit
        float health = 100.0f;      ///< health as this client believes it
        int   damageSent = 0;       ///< damage reported but not yet confirmed
        bool  pending = false;      ///< damageSent still in flight
        bool  killSoundPlayed = false; ///< kill "bling" already played for this life
        std::chrono::steady_clock::time_point sentAt; ///< when it was sent
    };
    PvpRemote pvpRemotes_[MAX_REMOTE_PLAYERS];
    /// Time windows (seconds) of the client-side PVP checks.
    static constexpr float pvpDuplicateHitWindow = 0.05f;
    static constexpr float pvpReportReconcileWindow = 1.0f;
    static constexpr float pvpShotTravelTime = 2.0f;

    /// Player damage that is not tied to a projectile (explosions, contact).
    /// Applies the damage and resolves death on both ends of the session.
    void applyPlayerDamage(float damage);
    /// Tell the server about damage this client applied to itself (own
    /// grenade, fall, round explosion), so the authoritative death matches.
    void reportLocalDamage(float damage, uint8_t weaponId = 0);
    /// Local death: stop the player and tell the server (it respawns us).
    void dieLocally();
    /// Respawn outside of a round cycle (multiplayer hit ENTER).
    void respawnLocally();
    /// Bots are local practice targets: they do not exist in multiplayer.
    void clearBots();
    void updateRemotePlayerDeath(float deltaTime);
    /// Resolve our own shot against remote players' bodies; true on a hit.
    bool checkProjectileRemoteCollision(Projectile& projectile);

    void checkExplosionRemoteCollision(const glm::vec3& position, float radius, float damage);
    void renderRemotePlayers();
    /// Advance remote players' walk / air / hit / shooting animation state.
    void updateRemoteAnimations(float deltaTime);
    
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
    float editorSpeed = 15.0f;
    glm::vec3 editorGridPos = glm::vec3(0.0f);
    glm::vec3 editorBlockSize = glm::vec3(2.0f, 2.0f, 2.0f);
    int editorSelectedTexture = 1;
    bool editorShowGrid = true;
    float editorGridSize = 1.0f;
    float editorBlockOffset = 5.0f;  // Distance in front of player for block placement
    float editorBlockRotation = 0.0f;  // Y-axis rotation for blocks
    float editorBlockPitch = 0.0f;      // X-axis rotation (pitch) for blocks

    /// Editor tool: what the left mouse button does in the 3D view.
    enum class EditorTool {
        Build,   ///< place a block (the brush)
        Paint,   ///< recolor the aimed block with the selected texture
        Erase,   ///< delete the aimed block / spawn point
        Pick,    ///< copy the aimed block into the brush
        Spawn    ///< place spawn points / bomb sites
    };
    EditorTool editorTool = EditorTool::Build;

    /// Modal panels of the editor UI.
    enum class EditorModal {
        None,
        Save,      ///< name the map and save it
        NewMap,    ///< blank map: name, size and starter room
        OpenMap,   ///< pick an existing map file to edit
        Confirm,   ///< unsaved-changes prompt
        Help       ///< controls reference
    };
    EditorModal editorModal = EditorModal::None;

    /// Action waiting for the unsaved-changes prompt.
    enum class EditorPending { None, NewMap, OpenMap, Exit };
    EditorPending editorPending = EditorPending::None;

    // Editor UI state (the panels are drawn in game_editor.cpp).
    bool editorDirty = false;            ///< changed since the last save
    bool editorSnapToGrid = true;        ///< snap free placement to the grid
    bool editorShowSpawns = true;        ///< draw spawn markers
    std::string editorMapName = "UNTITLED";
    std::string editorFileName = "untitled";   ///< file stem, no .map
    std::string editorNameInput = "UNTITLED";  ///< text field in Save/New dialogs
    std::string activeMapFile_;          ///< file currently installed (menu/editor)
    std::string editorToast;             ///< transient message at the bottom
    double editorToastUntil = 0.0;
    double editorBlink = 0.0;
    int editorOpenSelection = 0;         ///< highlighted row in the OPEN dialog
    int editorNewSize = 1;               ///< 0=small, 1=medium, 2=large
    bool editorNewStarter = true;        ///< add floor/walls/spawns
    std::string editorNewName = "NEW MAP";
    /// Wheel notches queued by scrollCallback() for the editor.
    int pendingEditorScroll_ = 0;
    /// True while the right mouse button drives the editor camera.
    bool editorLookActive_ = false;
    /// True when the left button started on a panel (must not paint).
    bool editorUiCapture_ = false;
    /// Undo is pushed once per stroke, not once per held brush stamp.
    bool editorStrokeActive_ = false;
    double editorLastBrushTime_ = 0.0;
    /// Result of the aim ray for the current frame.
    bool editorAimValid_ = false;
    bool editorAimHit_ = false;
    int editorAimBlock_ = -1;
    glm::vec3 editorAimPoint_{0.0f};
    glm::vec3 editorAimNormal_{0.0f, 1.0f, 0.0f};
    /// True while the editor has dropped into the map for a quick playtest.
    bool editorPlaytesting_ = false;
    GameMode editorPlaytestMode_ = DEATHMATCH;
    glm::vec3 editorPlaytestCamPos_{0.0f};
    float editorPlaytestYaw_ = 0.0f;
    float editorPlaytestPitch_ = 0.0f;
    /// Key-edge bookkeeping for the editor (one full keyboard scan per frame,
    /// so text input and shortcuts cannot leak pressed keys between screens).
    bool editorKeyCur_[GLFW_KEY_LAST + 1] = {};
    bool editorKeyPrev_[GLFW_KEY_LAST + 1] = {};
    std::string editorNameBeforeEdit_;

    /// A complete, restorable copy of the editable map data.
    struct EditorSnapshot {
        std::vector<MapBlock> blocks;
        std::vector<SpawnPoint> spawnPoints;
        std::vector<TeamSpawn> teamSpawns;
        std::vector<BombSiteData> bombSites;
        glm::vec3 dimensions{20.0f, 5.0f, 20.0f};
    };
    std::vector<EditorSnapshot> editorUndo_;
    std::vector<EditorSnapshot> editorRedo_;
    static constexpr size_t kEditorUndoLimit = 40;

    /// Open the editor on the current map.
    void enterEditor();
    /// Leave the editor for the main menu (cursor restored).
    void leaveEditor();
    /// Save the editor map under a display name / file stem.
    bool saveEditorMap(const std::string& name);
    /// Load `assets/maps/<file>` into the editor (no match reset).
    bool editorOpenMapFile(const std::string& file);
    /// Build a blank/starter map from the New Map dialog.
    void editorCreateNewMap();
    /// Ask for a destructive action, prompting when there are unsaved edits.
    void editorRequestNew();
    void editorRequestOpen();
    void editorRequestExit();
    void editorResolvePending(bool discard);
    /// Transient toast message (seconds).
    void editorShowToast(const std::string& text, double seconds = 2.5);
    /// Snapshot / restore for undo+redo.
    EditorSnapshot editorCaptureState() const;
    void editorRestoreState(const EditorSnapshot& snapshot);
    void editorPushUndo();
    void editorUndo();
    void editorRedo();
    /// Rebuild the GPU world batch after geometry edits.
    void editorRebuildWorld();
    /// True on the frame `key` went down (editor input).
    bool editorKeyPressed(int key) const;
    /// True while `key` is held.
    bool editorKeyDown(int key) const;
    /// Copy the current keyboard state into the edge tracker.
    void syncEditorKeyEdges();
    /// Apply the current tool to the aim result.
    void editorApplyTool();
    /// Drop into the map as a player from the aimed spot / editor camera.
    void editorStartPlaytest();
    /// Return from a playtest to the editor (camera restored).
    void editorStopPlaytest();

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
    /// Server-owned round results are applied once per round (client HUD /
    /// effects must not fire endRound() again for every periodic state packet).
    bool serverRoundEnded;
    uint32_t serverBombRoundId_ = 0; ///< last server round id applied
    uint32_t lastLocalStateSequence_ = 0; ///< ignore stale own death/respawn
    int roundWinner;
    RoundEndReason roundEndReason;
    /// Authoritative seconds left to plant (from BombStatePacket).
    float roundTimeLeft;

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
    int playerTeam; // 0 = Team A/Red, 1 = Team B/Blue (team-based modes)
    
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
    /// Cycle to the next (+1) / previous (-1) weapon with the mouse wheel.
    void cycleWeapon(int direction);
    /// Wheel notches accumulated by scrollCallback(), consumed in gameplay.
    int pendingWeaponScroll_ = 0;
    /// Re-send an active plant / defuse request so a dropped UDP packet does
    /// not silently cancel the interaction (multiplayer is server-owned).
    float bombInteractResend_ = 0.0f;
    /// Last server-reported plant/defuse actor (0 = none). While another
    /// player or bot owns the interaction, the local client must not restart
    /// its own request (which would replay the start sound every frame).
    uint32_t serverPlantActor_ = 0;
    uint32_t serverDefuseActor_ = 0;
    
    /**
     * @brief Shoot specific weapon types
     */
    void shootPistol();
    void shootShotgun();
    void throwGrenade();
    void throwFlashbang();
    
    /**
     * @brief Create simple explosion at position
     *
     * @param position Blast position (drawn and heard there)
     * @param source The projectile that exploded, when there is one: only
     *               the local player's own grenade damages the local player
     *               (relayed blasts are resolved by the network code)
     */
    void createSimpleExplosion(glm::vec3 position, const Projectile* source = nullptr);
    
    /**
     * @brief Create flashbang effect at position
     */
    void createFlashbangEffect(glm::vec3 position);
    
    /**
     * @brief Update flashbang effect
     */
    void updateFlashbangEffect(float deltaTime);
    
    /**
     * @brief Stop a relayed bullet on our own body (damage comes from the
     *        shooter).
     *
     * @param projectile The relayed projectile to check
     * @return true if it hit our body and was deactivated
     */
    bool checkProjectilePlayerBody(Projectile& projectile);
    
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
    /// Shared enemy death path: start the collapse animation, credit the kill
    /// and play the kill feedback.
    void killEnemy(Enemy& enemy);
    
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
     * @brief Render the sniper scope mask, reticle and mil-dot tick marks
     */
    void renderScopeOverlay();

    /**
     * @brief Update the sniper scope (held scope binding) and FOV transition
     */
    void updateScope(float deltaTime);
    /// Play a queued bolt-action cycle sound when its delay elapsed.
    void updateSniperBolt(float deltaTime);
    
    /**
     * @brief Build/clear bomb sites from the current map and game mode.
     *
     * Called when a map is installed and when the mode is changed from the
     * menu, so leaving bomb defusal removes the sites immediately.
     */
    void refreshBombSitesForMode();

    /**
     * @brief Render bomb sites and bomb
     */
    void renderBombSites();

    /**
     * @brief Render the always-on gameplay HUD.
     *
     * Health panel, weapon + ammo, round details and the local kill/death
     * counter. Drawn in every gameplay mode; the bomb-defusal HUD on top only
     * adds mode-specific elements.
     */
    void renderGameHUD();

    /**
     * @brief Render the first-person weapon viewmodel.
     *
     * The weapon is built from a handful of boxes in camera space and drawn in
     * its own pass (after the world, with a fresh depth buffer), so it never
     * clips into walls and keeps a consistent size on screen.
     */
    void renderWeaponViewmodel();

    /// Advance weapon bob/sway/recoil and the weapon-switch animation.
    void updateWeaponViewmodel(float deltaTime);

    /**
     * @brief Render bomb defusal HUD (team, bomb status, progress)
     */
    void renderBombDefusalHUD();
    
    /**
     * @brief Update bomb state (timer, explosion, etc.)
     * @param deltaTime Time since last frame
     */
    void updateBomb(float deltaTime);
    /// Offline bomb round: elimination and (no-plant) time-out outcome.
    void checkLocalBombRoundOutcome();
    
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
    void endRound(int winner, RoundEndReason reason = ROUND_END_NONE);
    
    /**
     * @brief Start a new round
     */
    void startRound();
    
    /**
     * @brief Update spectator camera (free-flying, like the map editor)
     */
    void updateSpectator(float deltaTime);
    
    /**
     * @brief Update round end timer (delay before next round)
     * @param deltaTime Time since last frame
     */
    void updateRoundEndTimer(float deltaTime);
    
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
    void processEditorInput(float deltaTime);
    
    /**
     * @brief Save current map to file
     *
     * @param filename File name inside assets/maps (".map" is appended when missing)
     * @param mapName Display name written to the map header
     * @return true when the file was written
     */
    bool saveMap(const std::string& filename, const std::string& mapName = "Custom");
    
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