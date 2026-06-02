@echo off
cd /d "%~dp0"
color 0B

echo.
echo === OpenGL Game Build Script ===
echo.

REM Initialize variables
set MSYS2_FOUND=0
set MSYS2_PATH=""
set MSYS2_ENV=ucrt64

:check_msys2
echo [1/4] Checking MSYS2 installation...

REM Check common installation paths
if exist "C:\msys64\msys2.exe" (
    set MSYS2_FOUND=1
    set MSYS2_PATH=C:\msys64\msys2.exe
    echo [SUCCESS] MSYS2 found at C:\msys64\msys2.exe
    goto :msys2_detected
)

if exist "C:\msys2\msys2.exe" (
    set MSYS2_FOUND=1
    set MSYS2_PATH=C:\msys2\msys2.exe
    echo [SUCCESS] MSYS2 found at C:\msys2\msys2.exe
    goto :msys2_detected
)

REM Try to find MSYS2 in PATH
for /f "tokens=*" %%i in ('where msys2.exe 2^>nul') do (
    if not "%%i"=="" (
        set MSYS2_FOUND=1
        set MSYS2_PATH=%%i
        echo [SUCCESS] MSYS2 found in PATH: %%i
        goto :msys2_detected
    )
)

:msys2_not_found
echo [ERROR] MSYS2 not found!
echo.
echo MSYS2 is required to build this game.
echo.
echo What is MSYS2?
echo   - A development environment for Windows
echo   - Provides Linux-like tools on Windows
echo   - Required for building this OpenGL game
echo.
echo Installation Options:
echo.
echo 1. Automatic Installation (Recommended):
echo    - Download: https://www.msys2.org/
echo    - Run installer with default settings
echo    - After installation, this script will continue automatically
echo.
echo 2. Manual Installation:
echo    - Download from: https://www.msys2.org/
echo    - Install to C:\msys64\ (recommended)
echo    - Restart this script after installation
echo.
echo 3. I already have MSYS2 installed:
echo    - Specify the path to msys2.exe
echo    - Common locations: C:\msys64\msys2.exe or C:\msys2\msys2.exe
echo.

set /p choice="Choose option (1/2/3) or press 'q' to quit: "

if /i "%choice%"=="1" goto :install_msys2
if /i "%choice%"=="2" goto :manual_install
if /i "%choice%"=="3" goto :manual_path
if /i "%choice%"=="q" exit /b 0
echo Invalid choice. Please try again.
goto :msys2_not_found

:install_msys2
echo.
echo [2/4] Opening MSYS2 download page...
echo Please follow these steps:
echo.
echo 1. Download MSYS2 from the browser that will open
echo 2. Run the installer
echo 3. Accept all default settings
echo 4. After installation completes, press any key to continue
echo.
echo Download page: https://www.msys2.org/
echo.
start https://www.msys2.org/
echo Waiting for MSYS2 installation...
echo Press any key when installation is complete...
pause >nul
goto :check_msys2

:manual_install
echo.
echo Opening MSYS2 download page...
echo Please install to C:\msys64\ for best compatibility.
start https://www.msys2.org/
echo.
echo After installation, run this script again.
pause
exit /b 0

:manual_path
echo.
set /p manual_path="Enter full path to msys2.exe (e.g., C:\msys64\msys2.exe): "
if exist "%manual_path%" (
    set MSYS2_FOUND=1
    set MSYS2_PATH=%manual_path%
    echo [OK] Using manual path: %MSYS2_PATH%
    goto :msys2_detected
) else (
    echo [ERROR] Path not found: %manual_path%
    goto :manual_path
)

:msys2_detected
echo.
echo [2/4] Validating MSYS2 installation...

REM Test MSYS2 by running a simple command
%MSYS2_PATH% -ucrt64 -shell bash -lc "echo 'MSYS2 validation successful'" >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] MSYS2 validation failed!
    echo The path exists but MSYS2 cannot run properly.
    echo.
    echo Possible causes:
    echo   - MSYS2 installation is corrupted
    echo   - Permission issues
    echo   - Antivirus blocking MSYS2
    echo.
    echo Solutions:
    echo   1. Reinstall MSYS2
    echo   2. Run this script as administrator
    echo   3. Temporarily disable antivirus
    echo.
    pause
    exit /b 1
)

echo [SUCCESS] MSYS2 is working properly

REM Select environment
echo.
echo Select MSYS2 environment:
echo   1) UCRT64 (default)
echo   2) MINGW64 (recommended for Makefile.windows)
set /p env_choice="Choice (1/2): "
if "%env_choice%"=="" set env_choice=1
if "%env_choice%"=="2" (
    set MSYS2_ENV=mingw64
    set PKG_PREFIX=mingw-w64-x86_64
    echo Selected: MINGW64
) else (
    set MSYS2_ENV=ucrt64
    set PKG_PREFIX=mingw-w64-ucrt-x86_64
    echo Selected: UCRT64
)

echo.
echo [3/4] Creating build script...

REM Create temporary build script
(
echo #!/bin/bash
echo cd "%CD:\=/%"
echo echo "[1/6] Updating package database..."
echo pacman -Sy --noconfirm
echo echo "[2/6] Installing dependencies..."
echo pacman -S --noconfirm %PKG_PREFIX%-gcc %PKG_PREFIX%-make %PKG_PREFIX%-glfw %PKG_PREFIX%-glm
echo echo "[3/6] Cleaning previous build..."
echo mingw32-make -f Makefile.windows clean
echo echo "[4/6] Building game..."
echo if mingw32-make -f Makefile.windows all; then
echo     echo "[5/6] Build successful!"
echo     echo "[6/6] Setting up runtime files..."
echo     mkdir -p build/bin
echo     cp /%MSYS2_ENV%/bin/libgcc_s_seh-1.dll build/bin/ 2>/dev/null ^&^& echo "  - libgcc_s_seh-1.dll copied" ^|^| echo "  - WARNING: libgcc_s_seh-1.dll not found"
echo     cp /%MSYS2_ENV%/bin/libstdc++-6.dll build/bin/ 2>/dev/null ^&^& echo "  - libstdc++-6.dll copied" ^|^| echo "  - WARNING: libstdc++-6.dll not found"
echo     cp /%MSYS2_ENV%/bin/libwinpthread-1.dll build/bin/ 2>/dev/null ^&^& echo "  - libwinpthread-1.dll copied" ^|^| echo "  - WARNING: libwinpthread-1.dll not found"
echo     cp /%MSYS2_ENV%/bin/glfw3.dll build/bin/ 2>/dev/null ^&^& echo "  - glfw3.dll copied" ^|^| echo "  - WARNING: glfw3.dll not found"
echo     cp -r assets build/ 2>/dev/null ^&^& echo "  - Assets copied" ^|^| echo "  - WARNING: Could not copy assets"
echo     echo ""
echo     echo "=== BUILD COMPLETED SUCCESSFULLY ==="
echo     if [ -f "build/bin/game.exe" ]; then
echo         echo "Game executable: build/bin/game.exe"
echo         echo "Server executable: build/bin/server.exe"
echo         echo "DLLs: build/bin/*.dll"
echo         echo "Assets: build/assets/"
echo         echo ""
echo         echo "Ready to play! Use 'run.bat' to start the game."
echo     else
echo         echo "ERROR: Game executable not found!"
echo     fi
echo else
echo     echo "BUILD FAILED!"
echo     echo "Check the error messages above."
echo     exit 1
echo fi
) > temp_build.sh

echo [4/4] Building game...
echo This may take a few minutes on first run...
echo.

%MSYS2_PATH% -%MSYS2_ENV% -shell bash -lc "cd '%CD:\=/%' && chmod +x temp_build.sh && ./temp_build.sh"

set BUILD_RESULT=%errorlevel%

REM Cleanup
if exist temp_build.sh del temp_build.sh

echo.
echo === Build Results ===
if %BUILD_RESULT% equ 0 (
    if exist "build\bin\game.exe" (
        echo [SUCCESS] Game built successfully!
        echo.
        echo Files created:
        dir build\bin\*.exe build\bin\*.dll 2>nul
        echo.
        echo Assets: build\assets\ (shaders, textures, maps)
        echo.
        echo Next steps:
        echo   - Run 'run.bat' to play the game
        echo   - Run 'runServer.bat' for multiplayer server
        echo.
        echo Build summary:
        echo   - Game executable: build\bin\game.exe
        echo   - Server executable: build\bin\server.exe
        echo   - Required DLLs: build\bin\*.dll
        echo   - Game assets: build\assets\
    ) else (
        echo [FAILED] Build completed but executable not found
    )
) else (
    echo [FAILED] Build process failed with error %BUILD_RESULT%
    echo.
    echo Common issues and solutions:
    echo.
    echo 1. Network Issues:
    echo    - Check internet connection
    echo    - Try running: pacman -Sy --noconfirm
    echo.
    echo 2. Permission Issues:
    echo    - Run this script as administrator
    echo    - Check if antivirus is blocking MSYS2
    echo.
    echo 3. MSYS2 Issues:
    echo    - Restart MSYS2 completely
    echo    - Try: pacman -Syy --noconfirm (force refresh)
    echo.
    echo 4. Manual Build:
    echo    - Open MSYS2 MINGW64 manually
    echo    - cd to this directory
    echo    - Run: mingw32-make -f Makefile.windows clean ^&^& mingw32-make -f Makefile.windows all
)

echo.
pause
exit /b %BUILD_RESULT%
