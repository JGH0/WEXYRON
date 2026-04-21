@echo off
cd /d "%~dp0"
color 0A

echo.
echo === OpenGL Game Launcher ===
echo.

REM Check if game executable exists
if not exist "build\bin\game.exe" (
    echo [INFO] Game not built yet. Building first...
    call build.bat
    if %errorlevel% neq 0 (
        echo [ERROR] Build failed. Cannot start game.
        echo.
        echo Possible solutions:
        echo   1. Check the build output above for error messages
        echo   2. Ensure MSYS2 is properly installed
        echo   3. Try running build.bat manually as administrator
        echo   4. Check if all source files are present
        pause
        exit /b 1
    )
)

REM Verify game executable exists after build
if not exist "build\bin\game.exe" (
    echo [91m[ERROR][0m Game executable still not found after build!
    echo.
    echo This could indicate:
    echo   - Compilation errors in source code
    echo   - Build process failed silently
    echo   - Insufficient permissions
    echo.
    echo Try running build.bat manually to see detailed error messages.
    pause
    exit /b 1
)

REM Find MSYS2 for DLL copying (same logic as build.bat)
set MSYS2_FOUND=0
set MSYS2_UCRT64_PATH=""

if exist "C:\msys64\ucrt64\bin" (
    set MSYS2_FOUND=1
    set MSYS2_UCRT64_PATH="C:\msys64\ucrt64\bin"
) else if exist "C:\msys2\ucrt64\bin" (
    set MSYS2_FOUND=1
    set MSYS2_UCRT64_PATH="C:\msys2\ucrt64\bin"
)

REM Verify DLLs are present with fallbacks
echo Checking runtime files...
set MISSING_FILES=0
set DLL_WARNINGS=0

if not exist "build\bin\libgcc_s_seh-1.dll" (
    echo [INFO] libgcc_s_seh-1.dll missing, attempting to copy...
    if %MSYS2_FOUND% equ 1 (
        copy %MSYS2_UCRT64_PATH%\libgcc_s_seh-1.dll "build\bin\" >nul 2>&1
        if %errorlevel% neq 0 (
            set MISSING_FILES=1
            echo [WARNING] Could not copy libgcc_s_seh-1.dll
        ) else (
            echo [OK] libgcc_s_seh-1.dll copied
        )
    ) else (
        set MISSING_FILES=1
        set DLL_WARNINGS=1
        echo [WARNING] MSYS2 not found, cannot copy libgcc_s_seh-1.dll
    )
)

if not exist "build\bin\libstdc++-6.dll" (
    echo [INFO] libstdc++-6.dll missing, attempting to copy...
    if %MSYS2_FOUND% equ 1 (
        copy %MSYS2_UCRT64_PATH%\libstdc++-6.dll "build\bin\" >nul 2>&1
        if %errorlevel% neq 0 (
            set MISSING_FILES=1
            echo [WARNING] Could not copy libstdc++-6.dll
        ) else (
            echo [OK] libstdc++-6.dll copied
        )
    ) else (
        set MISSING_FILES=1
        echo [WARNING] MSYS2 not found, cannot copy libstdc++-6.dll
    )
)

if not exist "build\bin\libwinpthread-1.dll" (
    echo [INFO] libwinpthread-1.dll missing, attempting to copy...
    if %MSYS2_FOUND% equ 1 (
        copy %MSYS2_UCRT64_PATH%\libwinpthread-1.dll "build\bin\" >nul 2>&1
        if %errorlevel% neq 0 (
            set MISSING_FILES=1
            echo [WARNING] Could not copy libwinpthread-1.dll
        ) else (
            echo [OK] libwinpthread-1.dll copied
        )
    ) else (
        set MISSING_FILES=1
        echo [WARNING] MSYS2 not found, cannot copy libwinpthread-1.dll
    )
)

if not exist "build\bin\glfw3.dll" (
    echo [INFO] glfw3.dll missing, attempting to copy...
    if %MSYS2_FOUND% equ 1 (
        copy %MSYS2_UCRT64_PATH%\glfw3.dll "build\bin\" >nul 2>&1
        if %errorlevel% neq 0 (
            set MISSING_FILES=1
            echo [WARNING] Could not copy glfw3.dll
        ) else (
            echo [OK] glfw3.dll copied
        )
    ) else (
        set MISSING_FILES=1
        echo [WARNING] MSYS2 not found, cannot copy glfw3.dll
    )
)

REM Check assets with verification
if not exist "build\assets\shaders\vertex.glsl" (
    echo [INFO] Assets missing, copying...
    xcopy "assets" "build\assets" /E /I /Y >nul 2>&1
    if %errorlevel% neq 0 (
        echo [WARNING] Could not copy assets
        set MISSING_FILES=1
    ) else (
        echo [OK] Assets copied
    )
)

REM Verify critical files exist
echo.
echo Verifying game files...
set CRITICAL_FILES_OK=1

if not exist "build\assets\shaders\vertex.glsl" (
    echo [ERROR] Critical shader files missing!
    set CRITICAL_FILES_OK=0
)

if not exist "build\assets\textures\grass.png" (
    echo [ERROR] Critical texture files missing!
    set CRITICAL_FILES_OK=0
)

if %DLL_WARNINGS% equ 1 (
    echo.
    echo [WARNING] Some DLL files could not be copied automatically.
    echo The game may still work if MSYS2 is in your PATH.
)

if %CRITICAL_FILES_OK% equ 0 (
    echo.
    echo [ERROR] Critical files are missing! Game may not run properly.
    echo.
    echo Missing files will cause:
    echo   - White screen (missing shaders)
    echo   - Crashes (missing textures)
    echo   - Render errors (missing assets)
    echo.
    echo Try these solutions:
    echo   1. Run build.bat again to rebuild assets
    echo   2. Check if original assets folder exists
    echo   3. Manually copy assets folder to build\assets\
    echo.
    echo Continue anyway? (y/n)
    set /p continue="> "
    if /i not "%continue%"=="y" (
        pause
        exit /b 1
    )
)

if %MISSING_FILES% equ 1 (
    echo.
    echo [WARNING] Some files are missing. Game may not run properly.
    echo Consider running build.bat to ensure all files are present.
    echo.
)

echo [READY] Launching OpenGL Game...
echo.
echo Game controls:
echo   - WASD: Move
echo   - Mouse: Look around  
echo   - Left Click: Shoot
echo   - ESC: Exit
echo.
echo System requirements:
echo   - Windows 10 or later
echo   - OpenGL 3.3+ support
echo   - Graphics drivers up to date
echo.
echo Starting game...

REM CRITICAL FIX: Ensure assets are in the right place and launch from project root
echo [INFO] Running from project root: %CD%
echo [INFO] Current directory contains assets:
if exist "assets\shaders\vertex.glsl" (
    echo   - Shaders found in assets\shaders\
) else (
    echo   - WARNING: Shaders not found in assets\shaders\
)

if exist "assets\textures\grass.png" (
    echo   - Textures found in assets\textures\
) else (
    echo   - WARNING: Textures not found in assets\textures\
)

build\bin\game.exe

set GAME_RESULT=%errorlevel%

echo.
if %GAME_RESULT% equ 0 (
    echo [SUCCESS] Game exited normally.
    echo Thanks for playing!
) else (
    echo [ERROR] Game exited with code %GAME_RESULT%
    echo.
    echo Common issues and solutions:
    echo.
    echo 1. Graphics/Rendering Issues:
    echo    - Update graphics drivers
    echo    - Check if OpenGL 3.3+ is supported
    echo    - Try running in windowed mode
    echo.
    echo 2. DLL Issues:
    echo    - Ensure all DLLs are in build\bin\
    echo    - Install Visual C++ Redistributable
    echo    - Run as administrator
    echo.
    echo 3. Asset Issues:
    echo    - White screen: Check shader files
    echo    - Missing textures: Check assets folder
    echo    - Crashes: Verify all files are present
    echo.
    echo 4. System Issues:
    echo    - Check Windows updates
    echo    - Disable antivirus temporarily
    echo    - Verify sufficient disk space
    echo.
    echo 5. Advanced Troubleshooting:
    echo    - Run build.bat to rebuild everything
    echo    - Check Event Viewer for system errors
    echo    - Try compatibility mode (Windows 8)
    echo.
    echo Error code %GAME_RESULT% indicates:
    if %GAME_RESULT% equ 1 (
        echo   - General failure or missing dependencies
    )
    if %GAME_RESULT% equ -1073741515 (
        echo   - Missing DLL files (common issue)
    )
    if %GAME_RESULT% equ -1073741819 (
        echo   - Access violation / crash
    )
    if %GAME_RESULT% equ -1073740791 (
        echo   - Graphics driver issue
    )
)

echo.
pause
