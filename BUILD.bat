@echo off
REM Build the game on Windows with MSYS2 (UCRT64 or MINGW64) + Makefile.windows.
REM Canonical with CMakeLists.txt. Linux uses ./Makefile and is untouched.
REM Prereq: MSYS2 installed (https://www.msys2.org/) + internet on first run.
REM Run this script from cmd or PowerShell; it installs the toolchain itself.
setlocal enabledelayedexpansion
cd /d "%~dp0"
color 0B
echo.
echo === WEXYRON — Windows build (MSYS2 + Makefile.windows) ===
echo.

REM Pick MSYS2 root (first found wins) and its bash.exe. The ucrt64.exe /
REM mingw64.exe launchers are mintty front-ends; plain bash.exe with the
REM toolchain bin dir prepended to PATH is what CI and the manual steps use.
set MSYS2_ROOT=
for %%D in ("C:\msys64" "C:\msys2") do if exist "%%~D\usr\bin\bash.exe" if not defined MSYS2_ROOT set MSYS2_ROOT=%%~D
if not defined MSYS2_ROOT (
    for /f "tokens=*" %%i in ('where msys2.exe 2^>nul') do if exist "%%i" set MSYS2_PATH=%%i
    if defined MSYS2_PATH (
        for %%i in ("!MSYS2_PATH!") do set MSYS2_ROOT=%%~dpi
        REM trim trailing backslash
        set "MSYS2_ROOT=!MSYS2_ROOT:~0,-1!"
    )
)
if not defined MSYS2_ROOT goto :msys2_missing
if not exist "%MSYS2_ROOT%\usr\bin\bash.exe" goto :msys2_missing
set BASH=%MSYS2_ROOT%\usr\bin\bash.exe

echo [1/4] MSYS2 found: %MSYS2_ROOT%
goto :msys2_found

:msys2_missing
echo [ERROR] MSYS2 not found ^(no C:\msys64\usr\bin\bash.exe^).
echo Install it from https://www.msys2.org/ ^(default location C:\msys64^),
echo then re-run this script. This script installs the C++ toolchain for you.
pause
exit /b 1

:msys2_found
echo [2/4] Validating MSYS2...
"%BASH%" -lc "echo MSYS2 validation ok" 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] MSYS2 bash failed to start. Try reinstalling MSYS2 or running
    echo as administrator. Antivirus sometimes quarantines msys2_runtime.dll.
    pause
    exit /b 1
)

echo.
echo Select MSYS2 environment:
echo   1) UCRT64  ^(default, recommended for x86-64 Windows^)
echo   2) MINGW64
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

echo [3/4] Installing toolchain packages ^(%PKG_PREFIX%-gcc make glfw glm^)...
echo First run downloads ~100 MB; later runs are quick checks.
REM pacman runs from the plain msys bash (it installs into the chosen env).
"%BASH%" -lc "pacman -Sy --noconfirm --needed %PKG_PREFIX%-gcc %PKG_PREFIX%-make %PKG_PREFIX%-glfw %PKG_PREFIX%-glm 2>&1 | tail -20"
if %errorlevel% neq 0 (
    echo [ERROR] pacman install failed. Check the internet connection,
    echo then retry. A mirror hiccup ^(HTTP 500^) is usually fixed by re-running.
    pause
    exit /b 1
)

echo.
echo [4/4] Building game + server (Makefile.windows stages runtime DLLs,
echo       assets/ and the Mesa software renderer automatically)...
REM The toolchain bin dir must lead PATH so mingw32-make finds g++/glfw/glm.
REM No cd needed: bash inherits the project root set at the top of this script.
"%BASH%" -lc "export PATH=/%MSYS2_ENV%/bin:$PATH; mingw32-make -f Makefile.windows clean && mingw32-make -f Makefile.windows 2>&1 | tail -40"
if %errorlevel% neq 0 (
    echo [FAILED] Build error above. Common causes:
    echo   - MSYS2 env lacks posix threads: re-run pacman with the same env.
    echo   - Stale objects: delete build\ and re-run.
    pause
    exit /b 1
)

echo.
if exist "build\bin\game.exe" goto :build_ok
echo [FAILED] Build finished but build\bin\game.exe is missing.
pause
exit /b 1

:build_ok
echo [SUCCESS] Build complete.
echo.
echo Staged in build\bin :
if exist "build\bin\game.exe" echo   - game.exe
if exist "build\bin\server.exe" echo   - server.exe
if exist "build\bin\glfw3.dll" echo   - glfw3.dll
if exist "build\bin\libstdc++-6.dll" echo   - libstdc++-6.dll
if exist "build\bin\opengl32_sw.dll" echo   - opengl32_sw.dll + Mesa DLLs  ^(GPU-less VM fallback^)
if exist "build\bin\assets\shaders\glow_vertex.glsl" echo   - assets\shaders\glow_vertex.glsl  ^(exe-relative assets^)
echo.
echo Next:
echo   - run.bat          play the game
echo   - runServer.bat    start the multiplayer server
echo.
echo Rendering modes:
echo   - auto (default)  use GPU if present, else Mesa software GL
echo   - set CUBE_RENDERER=gpu to disable Mesa, or =software to force it.
echo.
pause
exit /b 0
