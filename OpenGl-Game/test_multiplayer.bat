@echo off
REM Windows Multiplayer Test Script
REM This script tests the local multiplayer functionality

echo === Cube Game Multiplayer Test (Windows) ===
echo.

REM Check if build exists
if not exist "build\bin\server.exe" (
    echo Server executable not found. Building first...
    call build.bat
    if %errorlevel% neq 0 (
        echo Build failed. Cannot test multiplayer.
        pause
        exit /b 1
    )
)

if not exist "build\bin\game.exe" (
    echo Game executable not found. Building first...
    call build.bat
    if %errorlevel% neq 0 (
        echo Build failed. Cannot test multiplayer.
        pause
        exit /b 1
    )
)

echo 1. Starting server on port 7777...
start "OpenGL Game Server" build\bin\server.exe 7777

REM Wait for server to start
timeout /t 3 /nobreak >nul

echo 2. Server started successfully!
echo    Port: 7777
echo.
echo 3. To test multiplayer:
echo    - Open a new terminal
echo    - Run: build\bin\game.exe
echo    - In game menu, select 'Connect to Server'
echo    - Press 'R' to scan for servers
echo    - Or manually connect to: 127.0.0.1:7777
echo.
echo 4. Close this window to stop the server
echo.

REM Wait for user to stop
pause
