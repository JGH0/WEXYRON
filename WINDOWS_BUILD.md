# Windows Development Setup

This guide explains how to build and run the game on Windows for development purposes.

## Prerequisites

1. **Visual Studio 2022** (Community edition is free)
   - Download from: https://visualstudio.microsoft.com/
   - During installation, select "Desktop development with C++" workload

2. **CMake** (3.10 or higher)
   - Download from: https://cmake.org/download/
   - Or install via Visual Studio installer

3. **vcpkg** (for dependency management)
   - This will automatically install required libraries

## Quick Start (Visual Studio)

### Option 1: Visual Studio with CMake (Recommended)

1. **Clone the repository**:
   ```cmd
   git clone <repository-url>
   cd OpenGl-Game
   ```

2. **Open in Visual Studio**:
   - Open Visual Studio 2022
   - Select "Open a local folder"
   - Choose the `OpenGl-Game` directory
   - Visual Studio will automatically detect the `CMakeLists.txt`

3. **Install dependencies via vcpkg**:
   Visual Studio should prompt you to install vcpkg dependencies. Click "Yes" to install:
   - GLFW3
   - OpenGL

   Alternatively, manually install:
   ```cmd
   vcpkg install glfw3 opengl
   ```

4. **Build the project**:
   - Select target: `game.exe` (client) or `server.exe` (dedicated server)
   - Click Build → Build All

5. **Run the game**:
   - Press F5 to run with debugging
   - Or run `game.exe` from the build output directory

### Option 2: VS Code with CMake Tools (Recommended for VS Code users)

1. **Install VS Code extensions**:
   - Open VS Code
   - Install extensions: `CMake Tools` and `C/C++` (by Microsoft)

2. **Open the project**:
   - File → Open Folder → Select `OpenGl-Game` directory
   - VS Code will detect the `CMakeLists.txt`

3. **Configure CMake**:
   - Press `Ctrl+Shift+P` → type "CMake: Configure"
   - Select your compiler (e.g., `Visual Studio Community 2022 Release - amd64`)
   - CMake Tools will automatically configure the project

4. **Build**:
   - Press `F7` or `Ctrl+Shift+P` → "CMake: Build"
   - Or click the Build button in the status bar (bottom of VS Code)

5. **Run/Debug**:
   - Press `Ctrl+Shift+P` → "CMake: Debug" (or "CMake: Run Without Debugging")
   - Or select target from the status bar (bottom) and click the play button

6. **Set launch target** (if needed):
   - Click on `[No Active Launch Target]` in the status bar
   - Select `game` to run the game client
   - Select `server` to run the dedicated server

### Option 3: Command Line Build

1. **Open Developer Command Prompt**:
   - Open "x64 Native Tools Command Prompt for VS 2022"

2. **Navigate to project directory**:
   ```cmd
   cd C:\path\to\OpenGl-Game
   ```

3. **Create build directory**:
   ```cmd
   mkdir build
   cd build
   ```

4. **Configure with CMake**:
   ```cmd
   cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
   ```

   If vcpkg is installed elsewhere, adjust the path accordingly.

5. **Build**:
   ```cmd
   cmake --build . --config Release
   ```

6. **Run**:
   ```cmd
   cd bin
   game.exe
   ```

## Project Structure

```
OpenGl-Game/
├── CMakeLists.txt       # Cross-platform build configuration
├── src/                 # Source code
│   ├── core/           # Game core (renderer, camera, map, etc.)
│   ├── network/        # Multiplayer networking (now Windows-compatible)
│   ├── systems/        # Game systems (weapons, physics, enemies)
│   └── ui/             # UI components
├── include/            # Header files (GLAD, STB image)
├── assets/             # Game assets (maps, textures, shaders)
└── build_windows/      # Windows build output (created by CMake)
```

## Controls

- **WASD** - Move
- **Mouse** - Look around
- **Space** - Jump
- **Left Shift (hold)** - Sprint
- **Left Ctrl (hold)** - Sneak / crouch
- **C** - Lie down / get up (crawl while prone; toggle or hold via settings)
- **Left Click** - Shoot
- **Mouse Wheel** - Previous / next weapon
- **1-5** - Switch weapons
- **Right Mouse (hold)** - Scope with the sniper rifle
- **E** - Plant / defuse the bomb
- **ESC** - Menu
- **U** - Unstuck (reset position if stuck)

All of these bindings (including scope and stance) can be changed in
**Settings → Controls**.

## Multiplayer

### Running a Server

```cmd
server.exe [port]
```

Default port is 7777.

### Connecting to a Server

1. Launch the game client
2. From the menu, select "Connect to Server"
3. Enter the server IP (e.g., `127.0.0.1` for local, or LAN IP for network play)
4. Enter the port (default: 7777)
5. Click Connect

## Troubleshooting

### "Cannot find GLFW3"
- Ensure vcpkg is installed and integrated with Visual Studio
- Run: `vcpkg integrate install`

### "Winsock errors"
- The code now uses cross-platform socket APIs
- If you see linker errors, ensure `ws2_32.lib` is linked (CMake handles this automatically)

### "Shader compilation failed"
- Ensure the `assets/shaders/` directory exists in the working directory
- Check that shader files are copied to the build output

## Platform-Specific Changes

The following changes were made to support Windows:

1. **Network layer** (`src/network/network.hpp`, `src/network/network.cpp`):
   - Added conditional compilation for Windows (`#ifdef _WIN32`)
   - Uses Winsock2 on Windows, POSIX sockets on Linux
   - Added `socket_t` typedef for cross-platform socket handles
   - Added `WSAStartup`/`WSACleanup` for Windows socket initialization

2. **Game** (`src/core/game.cpp`, `src/core/game.hpp`):
   - Changed `discoverySocketFd` from `int` to `socket_t`
   - Updated socket close calls to use `CLOSE_SOCKET` macro

3. **Build system** (`CMakeLists.txt`):
   - Cross-platform CMake configuration
   - Automatic platform library linking (`ws2_32` on Windows, `pthread` on Linux)

## Notes

- The game uses **OpenGL 3.3 Core Profile**
- Multiplayer uses **UDP** on port 7777 (configurable)
- LAN discovery uses **UDP broadcast** on the same port
- All networking code is now cross-platform compatible
