@echo off
REM Launch the game client. Build first if needed, then run build\bin\game.exe
REM with the project root as working directory (assets/ lives there too, and
REM game.cpp also falls back to build\bin\assets\ when launched from there —
REM e.g. double-click or after the Mesa restart).
REM Rendering: auto = GPU when present, else Mesa software GL (VMs without a
REM driver). Set CUBE_RENDERER=gpu to disable Mesa, =software to force it.
cd /d "%~dp0"
color 0A
echo.
echo === WEXYRON launcher ===
echo.

if not exist "build\bin\game.exe" (
    echo [INFO] Not built yet. Building first...
    call BUILD.bat
    if %errorlevel% neq 0 (
        echo [ERROR] Build failed, cannot start the game.
        pause
        exit /b 1
    )
)

REM The Makefile/CMake stage-runtime step already put these next to the exe.
REM Re-check the fast path only (no MSYS2 required at launch time).
set MISSING=0
if not exist "build\bin\glfw3.dll" set MISSING=1
if not exist "build\bin\libstdc++-6.dll" set MISSING=1
if not exist "build\bin\libwinpthread-1.dll" set MISSING=1
if not exist "build\bin\libgcc_s_seh-1.dll" set MISSING=1
if not exist "build\bin\assets\shaders\glow_vertex.glsl" set MISSING=1
if %MISSING% equ 1 (
    echo [INFO] Runtime files missing — restaging ^(one click, then it runs^)...
    REM Reuse the build so staging stays in one place.
    call BUILD.bat
    if %errorlevel% neq 0 (
        echo [ERROR] Staging failed.
        pause
        exit /b 1
    )
)

if not exist "build\bin\opengl32_sw.dll" (
    echo [NOTE] No Mesa fallback staged. Machines without a GPU driver
    echo        (QEMU/VirtualBox basic display) need it — see BUILD.bat.
    echo        With a real GPU present the game runs without it.
)

echo Launching game... ^(WASD move, mouse look, LMB shoot, ESC menu^)
echo.
build\bin\game.exe
set GAME_RESULT=%errorlevel%

echo.
if %GAME_RESULT% equ 0 (
    echo Done. Thanks for playing!
) else (
    echo [ERROR] Game exited with code %GAME_RESULT%
    echo.
    echo Quick checks:
    echo   - No GPU driver? Leave CUBE_RENDERER unset (auto) so Mesa runs.
    echo   - Missing DLLs? Re-run BUILD.bat to restage build\bin.
    echo   - See game-log.txt next to the exe for shader / context errors.
)
echo.
pause
exit /b %GAME_RESULT%
