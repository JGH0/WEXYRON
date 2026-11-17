/**
 * @file game.cpp
 * @brief Core Game lifecycle: constructor, destructor, init, map loading,
 *        resetGame, key-binding init, and GLFW callbacks.
 *        All other methods live in the split files:
 *          game_input.cpp   – processInput, processGameInput
 *          game_loop.cpp    – update, render, cleanup, shouldClose
 *          game_gameplay.cpp– physics, weapons, projectiles, enemies
 *          game_hud.cpp     – crosshair, damage/flashbang overlays, death screen
 *          game_menus.cpp   – menu / settings / map-selection render+input
 *          game_server.cpp  – server connect render+input, connectToServer, disconnect
 *          game_editor.cpp  – map editor render+input, saveMap
 */

#define GLFW_INCLUDE_NONE
#include "game.hpp"
#include "network/network.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <algorithm>
#include <cctype>
#if defined(_WIN32)
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// ---------------------------------------------------------------------------
// Startup failure diagnostics
// ---------------------------------------------------------------------------
// A Windows release is launched by double-click, which opens a console window
// that vanishes as soon as the process exits: any error printed there is lost.
// Pause before exiting so the message can actually be read.
namespace {
void waitForEnterOnFailure() {
#if defined(_WIN32)
    std::cerr << "Press Enter to close..." << std::endl;
    std::cerr.flush();
    std::fflush(stdout);
    (void)std::getchar();
#endif
}

// Try to create the window. Hardware drivers want a 3.3 core context; some
// older/virtual drivers only offer a compatibility profile, which still runs
// the GLSL 330 shaders, so fall back to that once.
GLFWwindow* createGameWindow(int width, int height) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* win = glfwCreateWindow(width, height, "Cube CS Lite", nullptr, nullptr);
    if (!win) {
        std::cerr << "Core profile window failed, retrying with a "
                     "compatibility profile..." << std::endl;
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
        win = glfwCreateWindow(width, height, "Cube CS Lite", nullptr, nullptr);
    }
    return win;
}

#if defined(_WIN32)
// Software-rendering fallback.
//
// The release ships Mesa's opengl32.dll renamed to opengl32_sw.dll. A DLL
// next to the exe would be loaded *instead of* the system one on every
// machine, so Mesa is only used as a fallback: when WGL cannot create any
// context (no GPU driver — virtual machines with qxl/basic display adapters),
// the game copies it to opengl32.dll and restarts itself with a marker
// variable set. Re-exec is needed because Windows has already mapped the
// system opengl32.dll into the current process by then.
//
// "auto" (the default) = GPU when available, Mesa otherwise.
// CUBE_RENDERER=gpu     = never use Mesa (fail like before)
// CUBE_RENDERER=software = always use Mesa (useful for slower GPUs)
//
// The Mesa loader DLL needs libgallium_wgl.dll (and, for completeness,
// libdlclose-skip.dll) sitting next to it. The packaging step ships those
// with their original names; only opengl32.dll is renamed to opengl32_sw.dll.
// After the copy below, the sibling DLLs are already next to the new
// opengl32.dll, so Windows resolves them without extra work.
//
// Uses the plain <windows.h> declarations (already included above) — the
// previous hand-written dllimport prototypes duplicated those and made the
// [[noreturn]] relaunch path warn on some toolchains.
constexpr const char* kMesaActiveVar = "CUBE_SOFTWARE_GL";
constexpr const char* kRendererVar = "CUBE_RENDERER";

std::filesystem::path exeDirectory() {
    char path[MAX_PATH] = {};
    const DWORD len = ::GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len == 0) return std::filesystem::current_path();
    return std::filesystem::path(std::string(path, len)).parent_path();
}

// Copy Mesa into place if it is not there yet. Returns false when it cannot be
// installed (already active, disabled, or not shipped).
bool enableMesaFallback(const std::filesystem::path& dir) {
    char active[8] = {0};
    if (::GetEnvironmentVariableA(kMesaActiveVar, active, sizeof(active)) > 0) return false;

    char mode[16] = {0};
    if (::GetEnvironmentVariableA(kRendererVar, mode, sizeof(mode)) > 0 && std::strcmp(mode, "gpu") == 0) {
        return false;
    }

    const auto software = dir / "opengl32_sw.dll";
    const auto target = dir / "opengl32.dll";
    const auto gallium = dir / "libgallium_wgl.dll";
    if (!std::filesystem::exists(software)) return false;
    if (!std::filesystem::exists(gallium)) {
        std::cerr << "Software renderer disabled: " << gallium.string()
                  << " is missing (only opengl32_sw.dll was shipped)."
                  << std::endl;
        return false;
    }
    if (!::CopyFileA(software.string().c_str(), target.string().c_str(), FALSE)) return false;
    return true;
}

// Point Mesa at llvmpipe (CPU) instead of its D3D12 backend. See
// relaunchWithMesa for why D3D12 is unusable on GPU-less VMs. Called before
// any context is created, so a second launch that already finds
// opengl32.dll next to the exe (no relaunch) still gets the stable backend.
void forceMesaLlvmpipe() {
    if (::GetEnvironmentVariableA("GALLIUM_DRIVER", nullptr, 0) == 0) {
        ::SetEnvironmentVariableA("GALLIUM_DRIVER", "llvmpipe");
    }
    if (::GetEnvironmentVariableA("LIBGL_ALWAYS_SOFTWARE", nullptr, 0) == 0) {
        ::SetEnvironmentVariableA("LIBGL_ALWAYS_SOFTWARE", "1");
    }
}

[[noreturn]] void relaunchWithMesa(const std::filesystem::path& dir) {
    std::cerr << "No hardware OpenGL available; restarting with the bundled "
                 "software renderer (Mesa). This is slower but works without a "
                 "GPU driver."
              << std::endl;

    ::SetEnvironmentVariableA(kMesaActiveVar, "1");
    // Force Mesa's CPU rasterizer (llvmpipe), not its D3D12 backend. The D3D12
    // backend on GPU-less Windows (Microsoft Basic Render Driver / WARP)
    // creates a context but then exits with 0x80070057 on the second
    // glTexImage2D / first glDrawArrays / glfwTerminate — the window flashes
    // and closes instantly. llvmpipe (pure CPU, no D3D) runs the whole game
    // stably. The env vars only affect the restarted child process.
    forceMesaLlvmpipe();
    // Re-run this executable in a new process, then exit. The new process
    // resolves opengl32.dll to the Mesa copy next to the exe. Keep the
    // caller's working directory (nullptr) so the relative assets/ lookup
    // finds the same files the parent process did; passing the exe directory
    // here previously made the child look for build/bin/assets/ when the
    // parent ran from the project root (and vice versa).
    std::string cmd = "\"" + (dir / "game.exe").string() + "\"";
    std::vector<char> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back('\0');
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (::CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                         nullptr, &si, &pi)) {
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);
        ::ExitProcess(0);
    }
    std::cerr << "Could not restart with the software renderer." << std::endl;
    waitForEnterOnFailure();
    ::ExitProcess(1);
    // The compiler cannot see that ExitProcess never returns on every
    // toolchain (MinGW declares it, MSVC's declaration is not marked
    // noreturn), so keep the no-return contract explicit.
    std::abort();
}
#else
// Linux (and macOS): the Mesa restart path does not exist here. Linux always
// has Mesa/llvmpipe in the system OpenGL stack (LIBGL_ALWAYS_SOFTWARE /
// LIBGL software paths), so a failed context creation is a real error.
std::filesystem::path exeDirectory() {
#if defined(__linux__)
    std::error_code ec;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) return self.parent_path();
#endif
    return std::filesystem::current_path();
}
#endif

// Make the relative "assets/..." lookups used everywhere (shaders, maps,
// textures, audio, settings) find their files no matter how the game was
// started: double-click from Explorer (cwd = exe folder), run.bat from the
// project root, a launcher script, or the Mesa restart above.
//
// Preference order (first hit wins, so normal Linux runs never change):
//   1. ./assets                 (project root / cwd — Linux run.sh, run.bat)
//   2. <exe dir>/assets         (CMake install / build/bin layout)
//   3. <exe dir>/../assets      (exe in build/bin, assets in the repo root)
//   4. <exe dir>/../../assets   (exe in a deeper build tree)
// When a later candidate wins we change the working directory to its parent
// (the folder that holds assets/); everything else keeps its behaviour.
void ensureAssetsVisible() {
    std::error_code ec;
    auto isAssetsDir = [&ec](const std::filesystem::path& p) {
        ec.clear();
        return std::filesystem::is_directory(p, ec);
    };
    if (isAssetsDir("assets")) return; // normal case, including Linux

    const auto exeDir = exeDirectory();
    const std::filesystem::path candidates[] = {
        exeDir / "assets",
        exeDir / ".." / "assets",
        exeDir / ".." / ".." / "assets",
    };
    for (const auto& candidate : candidates) {
        if (!isAssetsDir(candidate)) continue;
        const auto parent = candidate.parent_path();
        if (!parent.empty()) {
            std::filesystem::current_path(parent, ec);
        }
        std::cerr << "assets: using " << std::filesystem::absolute(
            std::filesystem::path("assets"), ec) << " (working directory "
            << std::filesystem::current_path(ec) << ")" << std::endl;
        return;
    }
    // No assets/ found anywhere: keep the cwd so the shader/map loaders
    // print their usual "file not found" diagnostics instead of failing
    // silently. The game already degrades to flat colours in that case.
}
} // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Game::Game(int width, int height)
    : currentMap(nullptr),
      window(nullptr),
      camera(glm::vec3(0.0f, 0.0f, 3.0f)),
      currentWeaponInstance(nullptr),
      pistol(nullptr),
      shotgun(nullptr),
      grenade(nullptr),
      flashbang(nullptr),
      physics(),
      playerPos(glm::vec3(0.0f, 2.0f, 0.0f)),
      playerVelocity(glm::vec3(0.0f, 0.0f, 0.0f)),
      isJumping(false),
      score(0),
      lastFrame(0.0f),
      firstMouse(true),
      lastX(400.0f),
      lastY(300.0f),
      gameState(MENU),
      currentWeapon(PISTOL),
      menuSelection(0),
      isDead(false),
      mapSelection(0),
      gameModeSelection(0),
      menuJustEntered(true),
      inp(),
      keyBindings(),
      editorBlockRotation(0.0f),
      editorBlockPitch(0.0f),
      editorSpawnMode(SPAWN_NONE),
      editorMode(MODE_BLOCK),
      bombDefusalMode(false),
      attackingTeam(0),
      roundActive(false),
      roundTime(0.0f),
      roundEndTimer(0.0f),
      teamADeaths(0),
      teamBDeaths(0),
      spectatorMode(false),
      spectatorTarget(-1),
      isPlanting(false),
      isDefusing(false),
      plantProgress(0.0f),
      defuseProgress(0.0f),
      serverRoundEnded(false),
      roundWinner(-1),
      roundEndReason(ROUND_END_NONE),
      roundTimeLeft(0.0f),
      shootCooldown(0.0f),
      pistolCooldown(0.0f),
      shotgunCooldown(0.0f),
      pistolAmmo(1000),
      shotgunAmmo(25),
      grenadeCount(5),
      flashbangCount(3),
      grenadeCooldown(0.0f),
      flashbangCooldown(0.0f),
      flashEffectTime(0.0f),
      isFlashed(false),
      infiniteAmmoMode(false),
      showFps(false),
      maxFps(144),
      vsyncEnabled(true),
      fpsFrameCount(0),
      fpsTimer(0.0f),
      fpsDisplayValue(0),
      currentGameMode(DEATHMATCH),
      playerTeam(0),
      playerHealth(100),
      playerMaxHealth(100),
      playerDamageCooldown(0.0f),
      damageEffectTime(0.0f),
      networkClient(nullptr),
      multiplayerMode(false),
      playerCount(0),
      serverAddress("127.0.0.1"),
      serverPort(7777),
      enteringServerAddress(false),
      serverConnectSelection(0),
      discoverySocketFd(SOCKET_INVALID),
      discoveryTimeSeconds(0.0f),
      discoverySendCooldown(0.0f),
      discoveredSelection(0),
      focusServerList(false),
      activeProjectiles(0),
      bulletHoleCount(0),
      activeEnemies(0),
      mouseGUIPosX(0.0f),
      mouseGUIPosY(0.0f),
      mouseClicked(false),
      prevMouseDown(false)
{
    std::cout << "Game initializing..." << std::endl;

    // Find assets/ before anything reads it (settings.cfg, audio, maps,
    // shaders). On Linux with ./assets present this is a no-op; on Windows
    // double-click / Mesa restarts it picks the folder that actually holds
    // the files. See ensureAssetsVisible() for the search order.
    ensureAssetsVisible();

    // Remote players
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++)
        remotePlayers[i].active = false;

    // Settings: read assets/settings.cfg (created with the defaults on the
    // first launch) and apply them. vsync is applied again once the window
    // exists, see init().
    loadSettings();

    // Projectiles
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectiles[i].active = false;
        projectiles[i].type = 0; // Initialize type field
        projectiles[i].detonated = false; // Initialize detonated field
        projectilePrevPosInitialized[i] = false;
        projectilePrevPos[i] = glm::vec3(0.0f);
    }

    // Bullet holes
    for (int i = 0; i < MAX_BULLET_HOLES; i++)
        bulletHoles[i].lifetime = 0.0f;

    // Bomb initialization
    bomb.position = glm::vec3(0.0f);
    bomb.planted = false;
    bomb.defused = false;
    bomb.plantTime = 0.0f;
    bomb.explodeTime = 20.0f;
    bomb.defuseTime = 4.0f;
    bomb.defuseProgress = 0.0f;
    bomb.plantingTeam = 0;
    bomb.active = false;

    // Bomb sites initialization
    for (int i = 0; i < MAX_BOMB_SITES; i++) {
        bombSites[i].active = false;
        bombSites[i].position = glm::vec3(0.0f);
        bombSites[i].size = glm::vec3(0.0f);
        bombSites[i].label = 'A';
    }
    activeBombSites = 0;

    // Editor initialization (remaining editor vars not in init list)
    editorNoclip = true;
    editorSpeed = 15.0f;
    editorGridPos = glm::vec3(0.0f);
    editorBlockSize = glm::vec3(2.0f, 2.0f, 2.0f);
    editorSelectedTexture = 1;
    editorShowGrid = true;
    editorGridSize = 1.0f;
    editorBlockOffset = 5.0f;

    // Weapons
    pistol    = new Pistol(infiniteAmmoMode);
    shotgun   = new PumpShotgun(infiniteAmmoMode);
    sniper    = new Sniper(infiniteAmmoMode);
    grenade   = new Grenade(3);
    flashbang = new Flashbang(2);
    currentWeaponInstance = pistol;

    // Physics
    physics.setPlayerPosition(playerPos);
    physics.setPlayerVelocity(playerVelocity);

    // Enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = false;
        enemies[i].alive  = false;
    }
    activeEnemies = 0;

    // ── GLFW / OpenGL window setup ──────────────────────────────────────────
    // Prefer X11 when it is available (it is on every Linux desktop under
    // XWayland too). The menu framework needs an active window for keyboard
    // input, and GLFW's X11 backend delivers that reliably under Xvfb/remote
    // setups, where the native Wayland backend does not report the window as
    // focused. Other platforms pick their default backend.
#if defined(__linux__) && !defined(_WIN32) && !defined(__APPLE__)
    // GLFW_PLATFORM hints exist since 3.4; older distro packages (e.g. Debian
    // bookworm ships 3.3) pick X11 by default on their own.
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
#endif
    // Surface GLFW's own error text (driver, WGL, platform errors) instead of
    // a bare "Failed to create window": on Windows a double-clicked exe also
    // opens a console that closes immediately, so wait for a key on failure.
    glfwSetErrorCallback([](int code, const char* desc) {
        std::cerr << "GLFW error " << code << ": " << (desc ? desc : "(no description)")
                  << std::endl;
    });
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        waitForEnterOnFailure();
        exit(-1);
    }
    // The shaders are GLSL 330, so ask for a real 3.3 core context. The old
    // request was COMPAT_PROFILE: some drivers reject #version 330 core there,
    // and on drivers without a compat profile the window failed to open at all.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    // Ask the driver to prefer the dedicated GPU on hybrid laptops.
    glfwWindowHint(GLFW_COCOA_GRAPHICS_SWITCHING, GLFW_TRUE);

#ifdef _WIN32
    // If Mesa is already the WGL implementation (opengl32.dll next to the
    // exe from a previous fallback), force it to llvmpipe now — before the
    // first context — so second and later launches get the stable CPU
    // rasterizer instead of the crashy D3D12 backend. First launches (no
    // opengl32.dll yet) take the relaunch path below, which sets the same
    // vars for the child. CUBE_RENDERER=gpu opts out explicitly.
    {
        char mode[16] = {0};
        const bool gpuLocked = ::GetEnvironmentVariableA(kRendererVar, mode, sizeof(mode)) > 0 &&
                               std::strcmp(mode, "gpu") == 0;
        if (!gpuLocked) {
            const std::filesystem::path dir = exeDirectory();
            if (std::filesystem::exists(dir / "opengl32.dll") &&
                std::filesystem::exists(dir / "opengl32_sw.dll")) {
                forceMesaLlvmpipe();
            }
        }
    }
    // CUBE_RENDERER=software forces the Mesa path even on machines with a
    // working GPU (useful to test the VM path, or when a GPU driver is
    // present but broken for OpenGL 3.3). CUBE_RENDERER=gpu disables the
    // fallback; unset/"auto" tries the GPU first and falls back on failure.
    {
        char mode[16] = {0};
        if (::GetEnvironmentVariableA(kRendererVar, mode, sizeof(mode)) > 0 &&
            std::strcmp(mode, "software") == 0) {
            const std::filesystem::path dir = exeDirectory();
            if (enableMesaFallback(dir)) {
                glfwTerminate();
                relaunchWithMesa(dir);
            }
            // No Mesa shipped: fall through and try the GPU as usual.
        }
    }
#endif

    window = createGameWindow(width, height);
    if (!window) {
#ifdef _WIN32
        // No OpenGL from the GPU driver at all (typical for a VM with a
        // qxl/basic adapter). Switch to the bundled Mesa renderer and restart.
        // Honours CUBE_RENDERER=gpu (never), =software (always), unset (auto).
        const std::filesystem::path dir = exeDirectory();
        if (enableMesaFallback(dir)) {
            glfwTerminate();
            relaunchWithMesa(dir);
        }
#endif
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        waitForEnterOnFailure();
        exit(-1);
    }
    glfwMakeContextCurrent(window);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);
    // Start in the menu (NORMAL in init()); enable raw motion now so the
    // first capture does not need a restart, and focus the game window so
    // the console does not steal the first click.
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    glfwFocusWindow(window);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    handCursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        exit(-1);
    }

    // Report which OpenGL implementation is actually in use: on a GPU-less
    // Windows install this must say "Mesa" (llvmpipe), otherwise the fallback
    // did not take effect.
    {
        const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const auto* rendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const auto* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        std::cout << "OpenGL: " << (version ? version : "?") << "\n"
                  << "  renderer: " << (rendererName ? rendererName : "?") << "\n"
                  << "  vendor:   " << (vendor ? vendor : "?") << std::endl;
    }

    // Sound engine: open the device and load assets/audio (never fatal).
    initAudio();

    std::cout << "Game constructor complete." << std::endl;
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
Game::~Game() {
    // Stop the audio device before the GLFW window goes away (the mixer runs
    // on its own thread; joining it here keeps shutdown deterministic).
    audio.shutdown();
    if (handCursor) glfwDestroyCursor(handCursor);
    disconnectFromServer();
    if (discoverySocketFd != SOCKET_INVALID) {
        CLOSE_SOCKET(discoverySocketFd);
        discoverySocketFd = SOCKET_INVALID;
    }
    delete pistol;
    delete shotgun;
    delete sniper;
    delete grenade;
    delete flashbang;
    if (currentMap) delete currentMap;
    glfwTerminate();
}

// ---------------------------------------------------------------------------
// init – called after constructor; sets up renderer and spawns initial enemies
// ---------------------------------------------------------------------------
void Game::init() {
    std::cout << "Initializing renderer..." << std::endl;
    renderer.init();

    // Re-apply the video settings now that a GL context exists (swap interval).
    applySettings();
    std::cout << "Renderer initialized." << std::endl;

    // Scan maps folder for available maps
    scanMapsFolder();

    // Start the first map in the background; init() finishes before it is ready
    // and the game shows the menu meanwhile. Simpler and just as fast: load the
    // first map up front so the menu immediately has something to show.
    std::cout << "[init] loadMap" << std::endl;
    if (!currentMap && !mapFiles.empty())
        loadMap(mapFiles[0]);

    // The startup map is only there so the menu has a scene behind it: the
    // game opens on the main menu, not in a match. Build the menu page before
    // the first frame - a menu state without a page draws nothing.
    gameInProgress_ = false;
    std::cout << "[init] openMainMenu" << std::endl;
    openMainMenu();
    gameState = MENU;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    // The startup map was loaded before the texture array existed; build it now
    // so the map renders with its real materials.
    std::cout << "[init] refreshTextures" << std::endl;
    renderer.refreshTextures(currentMap);
    std::cout << "[init] buildWorld" << std::endl;
    renderer.buildWorld(currentMap);

    // Now that weapons, window and renderer exist, apply the settings loaded
    // in the constructor (vsync, FPS cap, performance, infinite ammo,
    // bindings) - no restart needed to see them take effect.
    applySettings();

    playerVelocity = glm::vec3(0.0f);
    isJumping = false;

    spawnEnemy(glm::vec3( 5.0f, -1.0f,  5.0f));
    spawnEnemy(glm::vec3(-5.0f, -1.0f,  5.0f));
    spawnEnemy(glm::vec3( 5.0f, -1.0f, -5.0f));

    std::cout << "[init] done" << std::endl;
    std::cout << "Game initialization complete." << std::endl;
}

// ---------------------------------------------------------------------------
// setPerformanceLevel – how hard the game is allowed to work
// ---------------------------------------------------------------------------
void Game::setPerformanceLevel(int level) {
    perfLevel = std::max(0, std::min(level, 2));
    static const char* const names[] = {"Low", "Balanced", "High"};
    std::cout << "Performance: " << names[perfLevel] << std::endl;
    // The renderer batches either way (identical picture); the profile decides
    // how much is prepared ahead of time and whether worker threads are used.
    if (perfLevel == 1 && threadPool_.threadCount() > 0) {
        // Balanced: leave the worker pool idle unless a load starts.
    }
}

// ---------------------------------------------------------------------------
// loadMap
// ---------------------------------------------------------------------------
bool Game::loadMap(const std::string& mapFile) {
    // Blocking path: used at startup and whenever a worker context can't be
    // created. Parsing + texture upload happen here, on the GL thread.
    std::unique_ptr<Map> loaded = MapLoader::loadBlocking(mapFile);
    if (!loaded) return false;
    installMap(std::move(loaded));
    return currentMap != nullptr;
}

void Game::requestMapLoad(const std::string& mapFile) {
    // Keep the current map on screen while the new one is parsed and its
    // textures are uploaded on a worker thread.
    if (!mapLoader_.start(mapFile, window)) {
        loadMap(mapFile);
    }
}

void Game::pollMapLoad() {
    if (!mapLoader_.isLoading()) return;
    std::unique_ptr<Map> loaded = mapLoader_.pollFinished();
    if (loaded) {
        installMap(std::move(loaded));
    }
}

void Game::installMap(std::unique_ptr<Map> loaded) {
    if (currentMap) delete currentMap;
    currentMap = loaded.release();
    if (!currentMap) return;

    // Setup (or clear) bomb defusal state for the current mode. This is also
    // re-run when the menu changes the mode so stale sites disappear at once,
    // and it re-arms the server-round guards for a fresh match.
    refreshBombSitesForMode();

    resetGame();

    // Rebuild GPU-side state for the new map: the texture array (one bind for
    // every block texture) and the instanced world geometry.
    renderer.refreshTextures(currentMap);
    renderer.buildWorld(currentMap);

    // Now that weapons, window and renderer exist, apply the settings loaded
    // in the constructor (vsync, FPS cap, performance, infinite ammo,
    // bindings) - no restart needed to see them take effect.
    applySettings();
}

void Game::refreshBombSitesForMode() {
    // A mode change (or a freshly installed map) starts a fresh match: clear
    // the HUD statistics and round bookkeeping together with the bomb state.
    kills_ = 0;
    deaths_ = 0;
    roundNumber_ = 1;
    teamAScore_ = 0;
    teamBScore_ = 0;
    roundTime = 0.0f;

    // Changing mode must not leave a planted/defusing bomb or its sites behind.
    isPlanting = false;
    isDefusing = false;
    plantProgress = 0.0f;
    defuseProgress = 0.0f;
    bomb.planted = false;
    bomb.defused = false;
    bomb.active = false;
    bomb.plantTime = 0.0f;
    bomb.defuseProgress = 0.0f;
    bomb.plantingTeam = 0;
    activeBombSites = 0;
    serverRoundEnded = false;
    roundWinner = -1;
    roundEndReason = ROUND_END_NONE;
    roundTimeLeft = 0.0f;
    roundEndTimer = 0.0f;

    const bool roundBased = currentGameMode == TEAM_VS_TEAM ||
                            currentGameMode == BOMB_DEFUSAL;
    bombDefusalMode = currentMap != nullptr && roundBased;
    if (!bombDefusalMode) {
        roundActive = false;
        roundEndTimer = 0.0f;
        return;
    }

    // Local round modes have no server sending the round clock. Start the
    // round here so the HUD timer runs and round end -> next round works. On a
    // live server the authoritative BombStatePacket decides instead.
    if (!(networkClient && networkClient->isConnected())) {
        roundActive = true;
    }

    // Load the current map's authored bomb sites.
    const auto& mapBombSites = currentMap->getBombSites();
    for (const auto& site : mapBombSites) {
        if (activeBombSites >= MAX_BOMB_SITES) break;
        bombSites[activeBombSites] = {
            site.position,
            glm::vec3(2.0f, 0.2f, 2.0f),  // default size
            true,
            site.label
        };
        activeBombSites++;
    }

    // Fallback for maps without authored sites.
    if (activeBombSites == 0) {
        bombSites[0] = {glm::vec3(5.0f, 0.1f, 5.0f), glm::vec3(2.0f, 0.2f, 2.0f), true, 'A'};
        bombSites[1] = {glm::vec3(-5.0f, 0.1f, -5.0f), glm::vec3(2.0f, 0.2f, 2.0f), true, 'B'};
        activeBombSites = 2;
    }
}

Map* Game::getCurrentMap() {
    return currentMap;
}

// ---------------------------------------------------------------------------
// formatMapDisplayName – Convert filename to display name
// ---------------------------------------------------------------------------
std::string Game::formatMapDisplayName(const std::string& filename) {
    // Remove .map extension
    std::string name = filename;
    size_t extPos = name.find(".map");
    if (extPos != std::string::npos) {
        name = name.substr(0, extPos);
    }

    // Replace underscores with spaces
    std::replace(name.begin(), name.end(), '_', ' ');

    // Capitalize first letter of each word
    bool capitalizeNext = true;
    for (char& c : name) {
        if (capitalizeNext && c >= 'a' && c <= 'z') {
            c = std::toupper(c);
            capitalizeNext = false;
        } else if (c == ' ') {
            capitalizeNext = true;
        }
    }

    return name;
}

// ---------------------------------------------------------------------------
// scanMapsFolder – Scan assets/maps folder for .map files
// ---------------------------------------------------------------------------
void Game::scanMapsFolder() {
    mapFiles.clear();
    mapDisplayNames.clear();
    mapPlayerCounts.clear();

    std::string mapsPath = "assets/maps";

    try {
        if (std::filesystem::exists(mapsPath) && std::filesystem::is_directory(mapsPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(mapsPath)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    // Check if it's a .map file
                    if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".map") {
                        mapFiles.push_back(filename);
                        mapDisplayNames.push_back(formatMapDisplayName(filename));
                        mapPlayerCounts.push_back(0);
                    }
                }
            }
        }

        // Sort maps alphabetically by display name
        std::vector<size_t> indices(mapFiles.size());
        for (size_t i = 0; i < indices.size(); i++) indices[i] = i;
        std::sort(indices.begin(), indices.end(), [this](size_t a, size_t b) {
            return mapDisplayNames[a] < mapDisplayNames[b];
        });

        // Reorder vectors based on sorted indices
        std::vector<std::string> sortedFiles = mapFiles;
        std::vector<std::string> sortedNames = mapDisplayNames;
        std::vector<int> sortedCounts = mapPlayerCounts;

        for (size_t i = 0; i < indices.size(); i++) {
            mapFiles[i] = sortedFiles[indices[i]];
            mapDisplayNames[i] = sortedNames[indices[i]];
            mapPlayerCounts[i] = sortedCounts[indices[i]];
        }

        std::cout << "Loaded " << mapFiles.size() << " maps from " << mapsPath << std::endl;
        for (size_t i = 0; i < mapFiles.size(); i++) {
            std::cout << "  " << mapDisplayNames[i] << " (" << mapFiles[i] << ")" << std::endl;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error scanning maps folder: " << e.what() << std::endl;
        // Fallback to hardcoded maps if scanning fails
        mapFiles = {"simple.map", "large.map", "arena.map", "sniper.map", "complex.map", "team_arena.map"};
        mapDisplayNames = {"Simple", "Large", "Arena", "Sniper", "Complex", "Team Arena"};
        mapPlayerCounts = {0, 0, 0, 0, 0, 0};
    }
}



// ---------------------------------------------------------------------------
// resetGame – spawn player, reset health/ammo/weapons
// ---------------------------------------------------------------------------
void Game::resetGame() {
    playerHealth         = playerMaxHealth;
    playerDamageCooldown = 0.0f;
    damageEffectTime     = 0.0f;
    isDead               = false;
    spectatorMode        = false;
    spectatorTarget      = -1;
    isPlanting           = false;
    isDefusing           = false;
    plantProgress        = 0.0f;
    defuseProgress       = 0.0f;

    // Reset bomb state. The server-event guards are cleared from the next
    // authoritative "bomb reset" state packet, not here: a stale explosion
    // packet from the round that just ended must not trigger endRound() again.
    bomb.planted = false;
    bomb.defused = false;
    bomb.active = false;
    bomb.defuseProgress = 0.0f;

    // Spawn at map spawn point
    if (currentMap) {
        // Every map spawn is checked for clearance: spawn points are authored
        // at arbitrary x/z (often the map centre), and maps without spawns
        // fall back to the middle platform (simple.map). A body placed inside
        // a block is what caused the constant "UNSTUCK" climbing.
        float yaw = camera.Yaw, pitch = camera.Pitch;
        auto trySpawn = [&](const glm::vec3& candidate) {
            glm::vec3 feet = candidate;
            if (!placeAtSafePosition(feet)) return false;
            playerPos = feet;
            return true;
        };

        bool placed = false;
        if (currentGameMode == TEAM_VS_TEAM || currentGameMode == BOMB_DEFUSAL) {
            TeamSpawn spawn = currentMap->getRandomTeamSpawn(playerTeam);
            yaw = spawn.yaw; pitch = spawn.pitch;
            placed = trySpawn(spawn.position);
        } else {
            SpawnPoint spawn = currentMap->getRandomSpawnPoint();
            yaw = spawn.yaw; pitch = spawn.pitch;
            placed = trySpawn(spawn.position);
        }

        // No usable spawn point: pick any authored one that has room.
        for (const SpawnPoint& spawn : currentMap->getSpawnPoints()) {
            if (placed) break;
            placed = trySpawn(spawn.position);
        }

        // Deterministic fallback (maps without spawns, or as a last resort):
        // search rings around the map centre, low spots first. A centre-only
        // spot high up (a rooftop) is kept as a last resort.
        if (!placed) {
            glm::vec3 best(0.0f);
            bool found = false;
            bool foundAway = false;
            for (float d = 0.0f; d <= 6.0f && !foundAway; d += 1.0f) {
                for (float y = 1.0f; y <= 9.0f && !foundAway; y += 1.0f) {
                    for (int i = 0; i < 8 && !foundAway; ++i) {
                        const float a = d == 0.0f ? 0.0f : static_cast<float>(i) * glm::pi<float>() * 0.25f;
                        glm::vec3 feet(std::cos(a) * d, y, std::sin(a) * d);
                        if (!placeAtSafePosition(feet)) continue;
                        if (!found) { best = feet; found = true; }
                        if (d > 0.0f || feet.y <= 2.5f) {
                            best = feet;
                            foundAway = true;
                        }
                    }
                }
            }
            if (found) {
                playerPos = best;
                placed = true;
            }
        }
        camera.Yaw = yaw;
        camera.Pitch = pitch;
    } else {
        playerPos    = glm::vec3(0.0f, 2.0f, 0.0f);
        camera.Yaw   = 0.0f;
        camera.Pitch = 0.0f;
    }

    std::cout << "Spawned at (" << playerPos.x << ", " << playerPos.y << ", " << playerPos.z << ")"
              << (bodyBlockedAt(playerPos) ? " - BLOCKED" : " - clear")
              << std::endl;

    playerVelocity = glm::vec3(0.0f);
    camera.Position = glm::vec3(playerPos.x, playerPos.y + eyeHeight, playerPos.z);
    camera.updateCameraVectors();

    // Projectiles
    activeProjectiles = 0;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        projectiles[i].active = false;
        projectiles[i].type = 0; // Initialize type field
        projectiles[i].detonated = false; // Initialize detonated field
        projectilePrevPosInitialized[i] = false;
        projectilePrevPos[i] = glm::vec3(0.0f);
    }

    // Bullet holes
    bulletHoleCount = 0;
    for (int i = 0; i < MAX_BULLET_HOLES; i++)
        bulletHoles[i].lifetime = 0.0f;

    // Enemies
    activeEnemies = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = false;
        enemies[i].alive  = false;
    }

    // Weapons
    currentWeapon    = PISTOL;
    currentWeaponInstance = pistol;
    sniperScoped_    = false;
    camera.Zoom      = kDefaultFov;
    pistolCooldown   = 0.0f;
    shotgunCooldown  = 0.0f;
    grenadeCooldown  = 0.0f;
    flashbangCooldown= 0.0f;
    // Fresh viewmodel pose: a spawn is not a weapon switch mid-aim.
    weaponRecoil_    = 0.0f;
    weaponSwitch_    = 1.0f;
    weaponBobAmount_ = 0.0f;
    weaponSwayX_     = 0.0f;
    weaponSwayY_     = 0.0f;

    // Ammo
    pistolAmmo    = 999;
    shotgunAmmo   = 24;
    grenadeCount  = 3;
    flashbangCount= 2;

    // Effects
    flashEffectTime = 0.0f;
    isFlashed       = false;

    // Audio: fresh footstep state (no phantom landing on spawn) and the
    // magazine being loaded before the round starts.
    wasOnGround  = true;
    footstepTimer = 0.0f;
    fallTime     = 0.0f;
    playUiSound("reload");

    // Re-spawn initial enemies (practice targets: single player only)
    if (!multiplayerMode) {
        spawnEnemy(glm::vec3( 5.0f, -1.0f,  5.0f));
        spawnEnemy(glm::vec3(-5.0f, -1.0f,  5.0f));
        spawnEnemy(glm::vec3( 5.0f, -1.0f, -5.0f));
    } else {
        clearBots();
    }

    // Remote PVP bookkeeping: a new life starts from a clean slate.
    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        pvpRemotes_[i] = PvpRemote{};
    }

    gameState = PLAYING;
    capturePlayCursor(true);

    std::cout << "Game reset. HP=" << playerHealth << "/" << playerMaxHealth << std::endl;
}

// ---------------------------------------------------------------------------
// initKeyBindings
// ---------------------------------------------------------------------------
void Game::initKeyBindings() {
    // Bindings come from the settings (defaults when no file exists yet).
    applySettings();
}

// ---------------------------------------------------------------------------
// GLFW callbacks
// ---------------------------------------------------------------------------
void Game::capturePlayCursor(bool resetLook) {
    if (!window) return;
    // Focus first: GLFW_CURSOR_DISABLED only grabs on the active window.
    // When the exe is double-clicked the console is active, and inside a VM
    // the host may hold the grab — without focus the cursor stays fully free
    // ("cannot look around"). Clicking the game window then focuses it and
    // the next frame captures.
    glfwFocusWindow(window);
    // True relative motion (WM_INPUT on Windows, evdev/libinput on Linux).
    // Without it, an absolute-mode VM tablet/grid (QEMU default) reports
    // absolute positions, so a disabled cursor stuck at the window edge
    // produces no deltas or inverted look. Supported on both platforms;
    // gracefully skipped where it is not.
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    // Only hide/grab when focused. Hiding while unfocused leaves an
    // invisible cursor with no capture — the player cannot click to recover.
    const int focused = glfwGetWindowAttrib(window, GLFW_FOCUSED);
    if (focused) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    if (resetLook) {
        // Forget the menu hover position so the first gameplay move does
        // not spin the camera to the menu's last cursor location.
        firstMouse = true;
        lastX = 400.0f;
        lastY = 300.0f;
    }
}

void Game::framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    if (width > 0 && height > 0)
        glViewport(0, 0, width, height);
}

void Game::mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    Game* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game) return;
    if (game->firstMouse) {
        game->lastX      = (float)xpos;
        game->lastY      = (float)ypos;
        game->firstMouse = false;
    }
    float xoffset =  (float)xpos - game->lastX;
    float yoffset =  game->lastY - (float)ypos;
    game->lastX   = (float)xpos;
    game->lastY   = (float)ypos;
    // While scoped the same mouse distance should move the aim less: the
    // zoomed image amplifies every pixel of look movement.
    if (game->sniperScoped_) {
        xoffset *= 0.35f;
        yoffset *= 0.35f;
    }
    // Only process mouse look while playing or in the editor
    if (game->gameState == PLAYING || game->gameState == MAP_EDITOR)
        game->camera.ProcessMouseMovement(xoffset, yoffset);
}

void Game::scrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
    Game* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (game)
        game->camera.ProcessMouseScroll((float)yoffset);
}