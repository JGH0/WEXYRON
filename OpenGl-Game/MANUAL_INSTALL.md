# Manual MSYS2 Installation Guide

Since the automatic installation is having issues, follow these steps manually:

## Step 1: Open MSYS2 UCRT64
1. Open Start Menu
2. Search for "MSYS2 UCRT64"
3. Click on "MSYS2 UCRT64" to open the terminal

## Step 2: Update Package Database
In the MSYS2 UCRT64 terminal, run:
```bash
pacman -Sy
```

## Step 3: Install Development Tools
```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-cmake --noconfirm
```

## Step 4: Install Graphics Libraries
```bash
pacman -S mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-glm mingw-w64-ucrt-x86_64-glew --noconfirm
```

## Step 5: Install Additional Libraries
```bash
pacman -S mingw-w64-ucrt-x86_64-SDL2 --noconfirm
```

## Step 6: Verify Installation
```bash
which gcc
which make
which mingw32-make
gcc --version
make --version
```

## Step 7: Build the Game
1. Navigate to the game directory:
```bash
cd /c/Users/bfo/Documents/GitHub/cube-cs-lite/OpenGl-Game
```

2. Build the game:
```bash
mingw32-make -f Makefile.windows clean
mingw32-make -f Makefile.windows all
```

## Step 8: Copy Required DLLs
```bash
mkdir -p build/bin
cp /ucrt64/bin/libgcc_s_seh-1.dll build/bin/
cp /ucrt64/bin/libstdc++-6.dll build/bin/
cp /ucrt64/bin/libwinpthread-1.dll build/bin/
cp /ucrt64/bin/glfw3.dll build/bin/
cp -r assets build/
```

## Step 9: Test the Game
```bash
./build/bin/game.exe
```

If you encounter any issues during these steps, please let me know the specific error messages.
