@echo off
echo Installing MINGW64 development packages...

REM Update package database
C:\msys64\mingw64.exe -c "pacman -Sy --noconfirm"

REM Install base development tools
C:\msys64\mingw64.exe -c "pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-cmake"

REM Install OpenGL and graphics libraries
C:\msys64\mingw64.exe -c "pacman -S --noconfirm mingw-w64-x86_64-glfw mingw-w64-x86_64-glm mingw-w64-x86_64-glew"

REM Install additional libraries
C:\msys64\mingw64.exe -c "pacman -S --noconfirm mingw-w64-x86_64-SDL2"

echo Installation completed.
echo Verifying tools...

C:\msys64\mingw64.exe -c "which gcc"
C:\msys64\mingw64.exe -c "which make"
C:\msys64\mingw64.exe -c "which mingw32-make"

echo GCC version:
C:\msys64\mingw64.exe -c "gcc --version | head -1"

echo Make version:
C:\msys64\mingw64.exe -c "make --version | head -1"

pause
