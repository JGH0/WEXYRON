@echo off
REM Windows Game Launcher Script

echo === Starting OpenGL Game ===
echo.

REM Check if game executable exists
if not exist "build\bin\game.exe" (
    echo Game executable not found. Building first...
    call build.bat
    if %errorlevel% neq 0 (
        echo Build failed. Cannot start game.
        pause
        exit /b 1
    )
)

REM Start the game
echo Starting game...
build\bin\game.exe

if %errorlevel% neq 0 (
    echo Game exited with error.
    pause
)
