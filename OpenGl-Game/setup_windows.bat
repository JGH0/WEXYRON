@echo off
REM Windows Setup Script for OpenGL Game
REM This script helps install and configure MinGW-w64

echo === OpenGL Game Windows Setup ===
echo.

REM Check if we're in MSYS2
echo Checking environment...
if not defined MSYSTEM (
    echo.
    echo ERROR: This script must be run from MSYS2 MINGW64
    echo.
    echo Please:
    echo 1. Install MSYS2 from https://www.msys2.org/
    echo 2. Open "MSYS2 MINGW64" terminal
    echo 3. Navigate to your project folder
    echo 4. Run this script again
    echo.
    pause
    exit /b 1
)

echo MSYS2 environment detected: %MSYSTEM%
echo.

REM Update package databases
echo Updating package databases...
pacman -Syu --noconfirm

REM Install MinGW-w64 toolchain and dependencies
echo Installing MinGW-w64 and dependencies...
pacman -S --needed --noconfirm ^
    mingw-w64-x86_64-toolchain ^
    mingw-w64-x86_64-glfw ^
    mingw-w64-x86_64-gcc ^
    mingw-w64-x86_64-make ^
    mingw-w64-x86_64-glm

if %errorlevel% neq 0 (
    echo.
    echo ERROR: Failed to install packages
    pause
    exit /b 1
)

echo.
echo === Installation Complete! ===
echo.
echo MinGW-w64 has been installed successfully.
echo.
echo IMPORTANT: Add this to your Windows PATH:
echo   C:\msys64\mingw64\bin
echo.
echo To add to PATH:
echo 1. Press Windows key, type "Environment Variables"
echo 2. Click "Edit the system environment variables"
echo 3. Click "Environment Variables..."
echo 4. Under "System variables", find "Path"
echo 5. Click "Edit...", then "New"
echo 6. Add: C:\msys64\mingw64\bin
echo 7. Click OK on all windows
echo 8. Restart Command Prompt/PowerShell
echo.
echo After setting PATH, you can build with:
echo   build.bat
echo.
pause
