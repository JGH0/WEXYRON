# WEXYRON

**Warfare EXperimentation & Yield-based Reconnaissance Operations Network**

A multiplayer-enabled 3D first-person shooter game built with OpenGL and C++.

## Features

Feature requests are welcome! If there is something you would like to see in
the game, open an issue describing the idea and how it would improve gameplay.

### Single Player
- 3D first-person shooter gameplay
- Multiple weapons (Pistol, Shotgun, Sniper, Grenade, Flashbang)
- First-person glTF weapon viewmodels with bob/sway, recoil and switch animation
- HUD with health, current weapon + ammo, round details, team score and K/D
- Blocky animated humanoid enemies/teammates (skinned glTF model) with hit zones
- Hit zones: head shots deal 2x, body 1x, arms/legs/feet 0.5x; explosions deal fixed damage
- Physics-based movement and collisions
- Enemy AI
- Map editor with mouse placement, undo/redo, new/open/save dialogs and spawn tools
- Discord Rich Presence (optional): shows the current mode, map or editor state
- Various game modes (Deathmatch, Team Deathmatch, Bomb Defusal)
- Match formats: Free Play (endless), Normal (first to 13 rounds / 50 kills)
  and Swift (first to 7 rounds / 25 kills)
- Pre-match team and bot setup screen
- Single-player bots with configurable count, teams and accuracy

### Local Multiplayer
- **LAN Discovery**: Automatic server discovery on local network
- **Multiple Connection Types**:
  - WiFi network (same router)
  - Ethernet cable connection
  - Direct cable-to-cable connection
- **Real-time multiplayer**: Up to 16 players
- **Server-side bots**: Configurable count and accuracy, all game modes
- **Server-authoritative**: Fair gameplay with server-side physics
- **Low latency**: UDP protocol for responsive controls

## Quick Start

### Windows (MSYS2)
1. Install MSYS2 (see [WINDOWS_SETUP.md](WINDOWS_SETUP.md))
2. Run `BUILD.bat` — it installs the gcc/glfw/glm packages, builds with
   `Makefile.windows`, and stages the runtime DLLs, `assets/` and the Mesa
   software renderer into `build/bin/`
3. Run with `run.bat` (or double-click `build\bin\game.exe`)
4. Start the server with `runServer.bat` (UDP port 7777)

GPU-less VMs (QEMU/VirtualBox basic display, no OpenGL driver) work too:
the game ships Mesa `opengl32_sw.dll` in `build/bin/` and automatically
falls back to it when WGL cannot create a context. With a real GPU it uses
the driver directly. Set `CUBE_RENDERER=gpu` to disable Mesa, or
`CUBE_RENDERER=software` to force it.

Manual (MSYS2 UCRT64 terminal, whether the README is accurate is
checked by `BUILD.bat` instead):
```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-glm
mingw32-make -f Makefile.windows
./build/bin/game.exe
```

Or CMake (same sources):
```cmd
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --config Release
```

Or use the batch scripts:
- `install_deps_mgw.bat` — install dependencies
- `BUILD.bat` — build + stage runtime files
- `run.bat` — launch the game
- `runServer.bat` — start a multiplayer server

### Linux/macOS
```bash
make clean && make
```

### Start Multiplayer Game

#### Method 1: Automatic LAN Discovery
1. **Start Server**:
   - **Windows**: `run_server.bat` or `build\bin\server.exe`
   - **Linux/macOS**: `./build/bin/server`

2. **Start Client**:
   - **Windows**: `run_game.bat` or `build\bin\game.exe`
   - **Linux/macOS**: `./build/bin/game`

3. **Connect**:
   - In game menu, select "Connect to Server"
   - Press 'R' to scan for local servers
   - Select server and press ENTER

#### Method 2: Manual Connection
1. Find server IP: `hostname -I` (Linux) or `ipconfig` (Windows)
2. In client, enter IP and port (default: 7777)

### Single Player
- **Windows**: `run_game.bat` or `build\bin\game.exe`
- **Linux/macOS**: `./build/bin/game`

## Controls

### Movement
- **W/A/S/D**: Move
- **Mouse**: Look around
- **Space**: Jump
- **Left Shift (hold)**: Sprint (standing only)
- **Left Ctrl (hold)**: Sneak / crouch and move quietly
- **C**: Lie down / get up (crawl while lying down; toggle or hold in Settings)

### Weapons
- **Left Mouse**: Shoot
- **Right Mouse (hold)**: Scope with the sniper rifle
- **Mouse wheel**: Previous / next weapon (the wheel no longer zooms)
- **1**: Pistol
- **2**: Pump Shotgun
- **3**: Sniper Rifle
- **4**: Grenade
- **5**: Flashbang
- **G**: Toggle Grenade/Flashbang
- **I**: Inspect the held weapon (animation; cancelled by firing)

The sniper is bolt-action: selecting it racks the bolt first, and each shot
must be cycled before the next round can be fired.

### Other
- **E**: Plant / defuse the bomb
- **ESC**: Menu / back
- **U**: Unstuck (reset position)

Movement, sprint, sneak, lying down, shooting, scoping, the weapon slots, the
grenade toggle, planting/defusing and the menu key can all be rebound in
**Settings → Controls**. The current binding
is shown on the right of each row; press ENTER on a row and then the new key (or
mouse button). Bindings and the other settings are saved to
`assets/settings.cfg` automatically.

### Menus and settings
- **UP/DOWN**: move, **ENTER**: confirm/change, **LEFT/RIGHT**: change a value,
  **ESC**: back / leave the menu
- The mouse can be used as well (hover to select, click to confirm)
- Settings are stored in `assets/settings.cfg` (created with the defaults on
  first launch); see [MENUS.md](MENUS.md) for the menu framework and how to add
  a setting or a screen

## Multiplayer Setup

### Network Requirements
- Local network connection (WiFi, Ethernet, or direct cable)
- UDP port 7777 open (default)
- Same network segment (e.g., 192.168.1.x)

### Firewall Configuration
```bash
# Linux (ufw)
sudo ufw allow 7777/udp

# Or temporarily disable for testing
sudo ufw disable
```

### Testing Multiplayer
- **Windows**: `test_multiplayer.bat`
- **Linux/macOS**: `./test_multiplayer.sh`

## Game Modes

- **Deathmatch**: Free-for-all multiplayer
- **Team Play**: Cooperative gameplay
- **Single Player**: Practice against AI enemies

## Technical Details

### Multiplayer Architecture
- **Client-Server Model**: Dedicated server with multiple clients
- **UDP Protocol**: Low-latency networking
- **LAN Discovery**: Broadcast-based server detection
- **Authoritative Server**: Server validates all game actions

### Performance
- Supports up to 8 concurrent players
- 60+ FPS gameplay on modern hardware
- Low network latency (< 50ms recommended)

## Building from Source

### Dependencies
- OpenGL 3.3+
- GLFW3
- GLAD
- C++17 compiler (g++/MinGW recommended; the build files set `-std=c++17`)
- SpatialKit continuous-collision headers (vendored in `third_party/spatialkit`;
  the build prefers the live `../Einzelkleinprojekt/spartialkit` checkout when
  it exists, so both projects stay in sync during development)
- **Windows**: MSYS2 + MinGW-w64
- **Linux**: g++ build-essential
- **macOS**: Xcode Command Line Tools

### Build Commands

#### Windows (MinGW)
```bash
# Build everything
mingw32-make -f Makefile.windows

# Build only client
mingw32-make -f Makefile.windows client

# Build only server
mingw32-make -f Makefile.windows server

# Clean build
mingw32-make -f Makefile.windows clean

# Or use batch scripts:
build.bat              # Build everything
run_game.bat          # Build and run game
run_server.bat        # Build and run server
```

#### Linux/macOS
```bash
# Build everything
make

# Build only client
make client

# Build only server
make server

# Clean build
make clean

# Run game
make run

# Run server
make run-server
```

## Troubleshooting

### Connection Issues
1. Check both devices are on same network
2. Verify firewall allows UDP port 7777
3. Test connectivity: `ping [server_ip]`
4. Try manual connection if auto-discovery fails

### Performance Issues
- Use wired Ethernet for best performance
- Close background applications
- Ensure network bandwidth > 100 Mbps

### Common Problems
- **"No servers found"**: Check network connection and firewall
- **"Failed to connect"**: Verify server IP and port
- **High latency**: Use WiFi with strong signal or Ethernet

## File Structure
```
wexyron/
|-- engine/                 # Reusable, game-agnostic engine library
|   |-- include/engine/     # Public engine headers
|   |-- src/                # Engine implementation
|   `-- tests/              # Engine smoke test
|-- src/                    # Game application code
|   |-- core/               # Game logic (monolithic Game class, editor, HUD)
|   |-- systems/            # Game systems (weapons, physics, etc.)
|   |-- network/            # Multiplayer networking
|   |-- ui/                 # User interface
|   `-- main.cpp            # Game entry point
|-- assets/                 # Game assets (textures, maps, audio)
|-- docs/                   # Guides and legacy notes
|-- tools/                  # Packaging, asset and documentation generators
|-- build/                  # Compiled binaries
|-- .vscode/                # VS Code configuration
|-- Makefile / Makefile.windows
|-- CMakeLists.txt
|-- BUILD.bat               # Windows build script
|-- run.bat                 # Windows game launcher
|-- runServer.bat           # Windows server launcher
|-- WINDOWS_SETUP.md        # Windows setup guide
|-- MULTIPLAYER_GUIDE.md    # Detailed multiplayer setup
|-- MENUS.md                # Menu framework
|-- docs/MAP_EDITOR.md      # Map editor controls and file format
|-- docs/DISCORD.md         # Discord Rich Presence setup
`-- WEAPON_CONTROLS.md      # Weapon controls
```

## Contributing

1. Fork the repository
2. Create feature branch
3. Test multiplayer functionality
4. Submit pull request

## License

WEXYRON is released under the [MIT License](LICENSE).

It bundles third-party libraries with their own permissive licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.

## Support

- **Windows Setup**: See `WINDOWS_SETUP.md` for detailed Windows installation and troubleshooting
- **Multiplayer Setup**: See `MULTIPLAYER_GUIDE.md` for detailed multiplayer instructions
## Audio

Sound is file-based: everything lives in `assets/audio/` and is loaded at
startup (no system audio library to install; the small `miniaudio` engine is
vendored in `third_party/miniaudio`).

```
assets/audio/effects/*.wav   pistol_fire, shotgun_fire, explosion, footsteps, ...
assets/audio/music/*.wav     menu, game
```
You can edit assets file as you want if you want to have an other ingame Sound.

### Tests

```
make && ./build/bin/audio_test        # mixer / decoding / volumes (no device needed)
xvfb-run -a ./build/bin/audio_smoke   # end-to-end: start a match, fire, check sounds
make engine-test && ./build/bin/engine_smoke_test  # extracted engine without game code
make swept-test && ./build/bin/swept_hit_test      # fast projectiles cannot tunnel
CUBE_MENU_TEST="menu:main;dump" xvfb-run -a ./build/bin/game  # game still boots
```

`audio_test` also opens the real device when one exists; on CI without sound it
still checks decoding and the volume buses.

### Engine library

The reusable rendering, map, camera, audio and thread-pool code lives in
`engine/` and builds as the `cube_engine` library. The game links it from
`src/`. See [`engine/README.md`](engine/README.md) for the boundary rules and
the smoke test.
