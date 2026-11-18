# Manual MSYS2 Installation Guide

`BUILD.bat` does all of this itself. Use these steps only if you prefer a
manual install.

## Step 1: Open MSYS2 UCRT64
1. Open Start Menu
2. Search for "MSYS2 UCRT64"
3. Click on "MSYS2 UCRT64" to open the terminal

## Step 2: Update Package Database
In the MSYS2 UCRT64 terminal, run:
```bash
pacman -Sy
```

## Step 3: Install the Toolchain + Libraries
The game needs gcc, make, cmake (for the CMake build), glfw and glm.
No SDL2/GLEW: the game does not use them.
```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-glm --noconfirm
```

## Step 4: Verify Installation
```bash
export PATH=/ucrt64/bin:$PATH   # if gcc is not on PATH yet
which gcc
which make
which mingw32-make
gcc --version
make --version
```

## Step 5: Build the Game
1. Navigate to the game directory (adjust the path to yours):
```bash
cd /c/Users/JGH/Documents/GitHub/cube-cs-lite
```

2. Build the game:
```bash
mingw32-make -f Makefile.windows clean
mingw32-make -f Makefile.windows all
```

3. Or with CMake:
```bash
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --config Release
```

## Step 6: Runtime files (automatic)
`mingw32-make -f Makefile.windows` already stages `build/bin/` with
everything the exes need: `libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
`libwinpthread-1.dll`, `glfw3.dll`, `assets/` (exe-relative), plus Mesa
`opengl32_sw.dll` + `libgallium_wgl.dll` + `libdlclose-skip.dll` for
GPU-less VMs. There is nothing to copy by hand. The only manual step is
if you build with a generator that skips `stage-runtime` (e.g. a bare
`cmake --build` — CMake's POST_BUILD does the same staging, so nothing
to do there either).

## Step 7: Run
```bash
./build/bin/game.exe        # or run.bat
./build/bin/server.exe      # or runServer.bat (default UDP port 7777)
```

No GPU? Leave `CUBE_RENDERER` unset: the game tries the GPU first and
falls back to the staged Mesa renderer automatically. Set
`CUBE_RENDERER=gpu` to disable Mesa, or `CUBE_RENDERER=software` to
force it.

If you encounter any issues during these steps, send the specific error
messages (or `game-log.txt` next to `game.exe`).
