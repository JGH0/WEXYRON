@echo off
REM Launch the dedicated server (headless, no GPU needed).
REM Port defaults to 7777 (override with first arg: runServer.bat 7778).
REM The headless server no longer links GLFW/OpenGL, so it starts even in a
REM GPU-less VM without any OpenGL DLL.
cd /d "%~dp0"
color 0E
echo.
echo === Cube CS Lite dedicated server ===
echo.

if not exist "build\bin\server.exe" (
    echo [INFO] Not built yet. Building first...
    call BUILD.bat
    if %errorlevel% neq 0 (
        echo [ERROR] Build failed, cannot start the server.
        pause
        exit /b 1
    )
)

REM Runtime DLLs for the server (libstdc++/libwinpthread only).
set MISSING=0
if not exist "build\bin\libstdc++-6.dll" set MISSING=1
if not exist "build\bin\libwinpthread-1.dll" set MISSING=1
if %MISSING% equ 1 (
    echo [INFO] Runtime DLLs missing — restaging via BUILD.bat...
    call BUILD.bat
    if %errorlevel% neq 0 (
        echo [ERROR] Staging failed.
        pause
        exit /b 1
    )
)

set SERVER_PORT=7777
if not "%~1"=="" set SERVER_PORT=%~1

echo Starting server on UDP port %SERVER_PORT% ^(Ctrl+C to stop^)...
echo Clients connect with 127.0.0.1:%SERVER_PORT% locally, or their
echo host's LAN IP (ipconfig) from other machines.
echo Windows Firewall may prompt once for the server — allow it.
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| find "IPv4"') do echo   - %%a:%SERVER_PORT%
echo.
build\bin\server.exe %SERVER_PORT%
set SERVER_RESULT=%errorlevel%

echo.
if %SERVER_RESULT% neq 0 (
    echo [ERROR] Server exited with code %SERVER_RESULT%
    echo Check that UDP %SERVER_PORT% is free ^(netstat -an ^| find ":%SERVER_PORT%"^)
    echo and that the firewall allows build\bin\server.exe.
)
echo.
pause
exit /b %SERVER_RESULT%
