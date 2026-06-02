@echo off
setlocal enabledelayedexpansion
echo Installing MSYS2 development packages...
echo.
echo Select MSYS2 environment:
echo   1) UCRT64 (default, used by existing scripts)
echo   2) MINGW64 (recommended for Makefile.windows)
echo.  
set /p ENV_CHOICE="Choice (1/2): "
if "!ENV_CHOICE!"=="" set ENV_CHOICE=1
if "!ENV_CHOICE!"=="1" (
    set MSYS2_EXE=C:\msys64\ucrt64.exe
    set PKG_PREFIX=mingw-w64-ucrt-x86_64
    echo Selected: UCRT64
)
if "!ENV_CHOICE!"=="2" (
    set MSYS2_EXE=C:\msys64\mingw64.exe
    set PKG_PREFIX=mingw-w64-x86_64
    echo Selected: MINGW64
)
echo.

REM Update package database
%MSYS2_EXE% -c "pacman -Sy --noconfirm"

REM Install base development tools
%MSYS2_EXE% -c "pacman -S --noconfirm %PKG_PREFIX%-gcc %PKG_PREFIX%-make %PKG_PREFIX%-cmake"

REM Install OpenGL and graphics libraries
%MSYS2_EXE% -c "pacman -S --noconfirm %PKG_PREFIX%-glfw %PKG_PREFIX%-glm %PKG_PREFIX%-glew"

REM Install additional libraries
%MSYS2_EXE% -c "pacman -S --noconfirm %PKG_PREFIX%-SDL2"

echo.
echo Installation completed.
echo Verifying tools...

%MSYS2_EXE% -c "which gcc"
%MSYS2_EXE% -c "which make"
%MSYS2_EXE% -c "which mingw32-make"

echo.
echo GCC version:
%MSYS2_EXE% -c "gcc --version | head -1"

echo Make version:
%MSYS2_EXE% -c "make --version | head -1"

pause
