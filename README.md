# OpenGL 3D Game with Local Multiplayer

A multiplayer-enabled 3D first-person shooter game built with OpenGL and C++.

## Features

### Single Player
- 3D first-person shooter gameplay
- Multiple weapons (Pistol, Shotgun, Grenade, Flashbang)
- Physics-based movement and collisions
- Enemy AI
- Map editor
- Various game modes

### Local Multiplayer (NEW!)
- **LAN Discovery**: Automatic server discovery on local network
- **Multiple Connection Types**:
  - WiFi network (same router)
  - Ethernet cable connection
  - Direct cable-to-cable connection
  - Mobile hotspot
- **Real-time multiplayer**: Up to 8 players
- **Server-authoritative**: Fair gameplay with server-side physics
- **Low latency**: UDP protocol for responsive controls

## Quick Start

### Windows (MSYS2)
1. Install MSYS2 (see [WINDOWS_SETUP.md](WINDOWS_SETUP.md))
2. Open **MSYS2 MINGW64** from Start Menu
3. Install dependencies: `pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-glfw mingw-w64-x86_64-glm`
4. Build with: `make -f Makefile.windows`
5. Run with: `./build/bin/game.exe`

Or use the batch scripts:
- `install_deps_mgw.bat` — install dependencies for MINGW64
- `BUILD.bat` — interactive build (select MINGW64 when prompted)
- `run.bat` — launch the game
- `runServer.bat` — start a multiplayer server

### Linux/macOS
```bash
cd OpenGl-Game
make clean && make
```

### Start Multiplayer Game

#### Method 1: Automatic LAN Discovery (Recommended)
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
- **Left Mouse**: Shoot

### Weapons
- **1**: Pistol
- **2**: Pump Shotgun  
- **3**: Grenade
- **4**: Flashbang
- **G**: Toggle Grenade/Flashbang

### Other
- **ESC**: Menu / back
- **U**: Unstuck (reset position)

All of these can be rebound in **Settings → Controls**. The current binding is
shown on the right of each row; press ENTER on a row and then the new key (or
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
- 60 FPS gameplay on modern hardware
- Low network latency (< 50ms recommended)

## Building from Source

### Dependencies
- OpenGL 3.3+
- GLFW3
- GLAD
- C++17 compiler (g++/MinGW recommended; the build files set `-std=c++17`)
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
OpenGl-Game/
|-- src/                    # Source code
|   |-- core/              # Game engine
|   |-- systems/           # Game systems (weapons, physics, etc.)
|   |-- network/           # Multiplayer networking
|   |-- ui/                # User interface
|   `-- main.cpp           # Game entry point
|-- assets/                # Game assets (textures, maps)
|-- build/                 # Compiled binaries
|-- .vscode/               # VS Code configuration
|   |-- c_cpp_properties_windows.json  # Windows IntelliSense
|   |-- launch_windows.json            # Windows debug configs
|   `-- tasks.json                    # Build tasks
|-- Makefile.windows         # Windows Makefile (MinGW)
|-- WINDOWS_SETUP.md        # Windows setup guide
|-- build.bat              # Windows build script
|-- run_game.bat           # Windows game launcher
|-- run_server.bat         # Windows server launcher
|-- test_multiplayer.bat   # Windows multiplayer test
|-- MULTIPLAYER_GUIDE.md   # Detailed multiplayer setup
`-- test_multiplayer.sh   # Linux multiplayer test script
```

## Contributing

1. Fork the repository
2. Create feature branch
3. Test multiplayer functionality
4. Submit pull request

## License

[Add your license here]

## Support

- **Windows Setup**: See `WINDOWS_SETUP.md` for detailed Windows installation and troubleshooting
- **Multiplayer Setup**: See `MULTIPLAYER_GUIDE.md` for detailed multiplayer instructions
- **VS Code Configuration**: Use the provided `.vscode/` configurations for Windows development

## Audio

Sound is file-based: everything lives in `assets/audio/` and is loaded at
startup (no system audio library to install; the small `miniaudio` engine is
vendored in `third_party/miniaudio`).

```
assets/audio/effects/*.wav   pistol_fire, shotgun_fire, explosion, footsteps, ...
assets/audio/music/*.wav     menu, game (looping)
```

- 3D positional audio: every effect is attenuated and panned from the camera.
- Menu music in the menus, match music while playing, with cross-fades.
- `MASTER VOLUME`, `MUSIC VOLUME` and `EFFECTS VOLUME` in Settings > Audio apply
  immediately (they are also saved in `assets/settings.cfg`).
- No sound device (e.g. a headless machine) is not an error: the game runs
  silently and picks up a device that appears later.

The shipped WAV files are synthesized by `tools/generate_sounds.py` (Python 3 +
numpy/scipy), so they can be regenerated or tweaked:

```
python3 tools/generate_sounds.py           # writes assets/audio/**
python3 tools/generate_sounds.py --check   # verify the files exist
```

To use your own recordings, replace the files (`.wav`, `.ogg`, `.flac` or
`.mp3` are accepted) keeping the file names, or drop new ones in
`assets/audio/effects/` and play them with `playSoundEffect("name", position)`.

### Tests

```
make && ./build/bin/audio_test        # mixer / decoding / volumes (no device needed)
xvfb-run -a ./build/bin/audio_smoke   # end-to-end: start a match, fire, check sounds
```

`audio_test` also opens the real device when one exists; on CI without sound it
still checks decoding and the volume buses.
