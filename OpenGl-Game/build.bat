@echo off
REM Windows Build Script for OpenGL Game
REM Requires MinGW-w64 and MSYS2

echo === Building OpenGL Game for Windows ===
echo.

REM Check if MinGW is available
where mingw32-make >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: mingw32-make not found in PATH
    echo Please install MinGW-w64 and ensure it's in your PATH
    echo Recommended: Install MSYS2 and MinGW-w64
    pause
    exit /b 1
)

REM Clean previous build
echo Cleaning previous build...
mingw32-make -f Makefile.windows clean

REM Build the game
echo Building game and server...
mingw32-make -f Makefile.windows

if %errorlevel% equ 0 (
    echo.
    echo === Build Successful! ===
    echo Executables created in build/bin/
    echo   - game.exe
    echo   - server.exe
    echo.
    echo To run the game:
    echo   build\bin\game.exe
    echo.
    echo To run the server:
    echo   build\bin\server.exe
    echo.
) else (
    echo.
    echo === Build Failed! ===
    echo Please check error messages above.
    pause
    exit /b 1
)

pause
