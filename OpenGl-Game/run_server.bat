@echo off
REM Windows Server Launcher Script

echo === Starting OpenGL Game Server ===
echo.

REM Check if server executable exists
if not exist "build\bin\server.exe" (
    echo Server executable not found. Building first...
    call build.bat
    if %errorlevel% neq 0 (
        echo Build failed. Cannot start server.
        pause
        exit /b 1
    )
)

REM Start the server on port 7777
echo Starting server on port 7777...
build\bin\server.exe 7777

if %errorlevel% neq 0 (
    echo Server exited with error.
    pause
)
