# Windows Setup Guide

This guide will help you set up the OpenGL Game to run on Windows using VS Code and MinGW.

## Prerequisites

### 1. Install MSYS2
MSYS2 provides a Unix-like environment on Windows and makes it easy to install MinGW-w64.

1. Download MSYS2 from [https://www.msys2.org/](https://www.msys2.org/)
2. Run the installer and follow the setup instructions
3. Launch MSYS2 MINGW64 (not the regular MSYS2)

### 2. Install MinGW-w64 and Dependencies
In the MSYS2 MINGW64 terminal, run:

```bash
# Update package databases
pacman -Syu

# Install MinGW-w64 toolchain
pacman -S --needed base-devel mingw-w64-x86_64-toolchain

# Install required libraries
pacman -S mingw-w64-x86_64-glfw
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-make
```

### 3. Add MinGW to Windows PATH
Add the following to your Windows PATH environment variable:
```
C:\msys64\mingw64\bin
```

You can do this through:
- Windows Settings > System > About > Advanced system settings > Environment Variables
- Or search for "Edit the system environment variables" in Start Menu

### 4. Install Visual Studio Code
If you haven't already, install VS Code from [https://code.visualstudio.com/](https://code.visualstudio.com/)

### 5. Install VS Code Extensions
Install these extensions in VS Code:
- C/C++ (Microsoft)
- C/C++ Compile Run (danielpinto8zz6)

## Project Setup

### 1. Configure VS Code
The project includes Windows-specific configuration files. To use them:

1. Open the project folder in VS Code
2. Copy the Windows configuration files:
   ```bash
   # In VS Code terminal or PowerShell
   copy .vscode\c_cpp_properties_windows.json .vscode\c_cpp_properties.json
   copy .vscode\launch_windows.json .vscode\launch.json
   ```

### 2. Build the Game
You have several options to build the game:

#### Option A: Use Batch Scripts (Easiest)
```bash
# In Windows Command Prompt or PowerShell
build.bat
```

#### Option B: Use VS Code Tasks
1. Press `Ctrl+Shift+P` in VS Code
2. Type "Tasks: Run Task"
3. Select "Build Game (Windows)"

#### Option C: Use Make Directly
```bash
# In Windows Command Prompt or PowerShell
mingw32-make -f Makefile.windows
```

### 3. Run the Game

#### Option A: Use Batch Scripts
```bash
# Run the game
run_game.bat

# Run the server
run_server.bat
```

#### Option B: Use VS Code
1. Press `F5` to debug the game
2. Or use "Tasks: Run Task" and select "Run Game (Windows)"

#### Option C: Run Executables Directly
```bash
# Navigate to build directory
cd build\bin

# Run the game
game.exe

# Run the server
server.exe 7777
```

## Multiplayer Setup on Windows

### Test Multiplayer
Use the included test script:
```bash
test_multiplayer.bat
```

### Firewall Configuration
Windows may block the game's network connections. Allow access when prompted, or manually configure:

1. Open Windows Defender Firewall
2. Click "Allow an app or feature through Windows Defender Firewall"
3. Add `game.exe` and `server.exe` for both Private and Public networks
4. Allow UDP port 7777

### Network Testing
Test connectivity between machines:
```bash
# Test server connectivity
ping [server_ip]

# Test if port is accessible (requires telnet client)
telnet [server_ip] 7777
```

## Troubleshooting

### Common Issues

#### "mingw32-make not found"
- Ensure MinGW is in your Windows PATH
- Restart Command Prompt/PowerShell after adding to PATH
- Verify installation by running `mingw32-make --version`

#### "glfw3.h not found"
- Install GLFW through MSYS2: `pacman -S mingw-w64-x86_64-glfw`
- Ensure include paths are correct in VS Code configuration

#### "undefined reference to glfwInit"
- Make sure to link against glfw3 in the Makefile
- Check that LDFLAGS includes `-lglfw3`

#### "opengl32.h not found"
- This should be included with MinGW-w64
- If missing, ensure `-DGLFW_INCLUDE_NONE` is defined

#### Game window doesn't open
- Check if graphics drivers support OpenGL 3.3+
- Run with debug mode to see error messages

#### Multiplayer connection fails
- Check Windows Firewall settings
- Ensure both machines are on same network
- Verify UDP port 7777 is not blocked

### Debug Mode
Build with debug symbols:
```bash
mingw32-make -f Makefile.windows debug
```

Run with GDB through VS Code for detailed error information.

## File Structure After Build
```
OpenGl-Game/
|-- build/
|   |-- bin/
|   |   |-- game.exe          # Game executable
|   |   |-- server.exe        # Server executable
|   |-- obj/                  # Object files
|-- src/                      # Source code
|-- assets/                   # Game assets
|-- Makefile.windows          # Windows Makefile
|-- build.bat                 # Windows build script
|-- run_game.bat              # Game launcher
|-- run_server.bat            # Server launcher
|-- test_multiplayer.bat      # Multiplayer test
`-- WINDOWS_SETUP.md          # This file
```

## Performance Tips

1. **Use wired Ethernet** for multiplayer to reduce latency
2. **Close background applications** to improve performance
3. **Update graphics drivers** for better OpenGL performance
4. **Disable Windows Game Bar** if it interferes with the game

## Next Steps

- Read the main README.md for game controls and features
- Check MULTIPLAYER_GUIDE.md for detailed multiplayer setup
- Use VS Code debugger to explore the codebase
