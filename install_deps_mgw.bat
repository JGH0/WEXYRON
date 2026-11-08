@echo off
REM Install the MSYS2 packages needed by Makefile.windows + CMake.
REM Picks UCRT64 by default (or MINGW64 if you pass it as argument 1).
REM No SDL2/GLEW: the game only needs gcc + make + cmake + glfw + glm.
setlocal enabledelayedexpansion
set ENV=ucrt64
set PREFIX=mingw-w64-ucrt-x86_64
if /i "%~1"=="mingw64" (
    set ENV=mingw64
    set PREFIX=mingw-w64-x86_64
)
REM MSYS2 root + bash (plain bash has no PATH to the toolchain bin dir yet,
REM pacman does not need it). Falls back to C:\msys64.
set MSYS2_ROOT=C:\msys64
if not exist "%MSYS2_ROOT%\usr\bin\bash.exe" set MSYS2_ROOT=C:\msys2
if not exist "%MSYS2_ROOT%\usr\bin\bash.exe" (
    echo [ERROR] MSYS2 not found. Install from https://www.msys2.org/ first.
    pause
    exit /b 1
)
set BASH=%MSYS2_ROOT%\usr\bin\bash.exe
echo Installing %PREFIX% packages into %ENV% ...
"%BASH%" -lc "pacman -Sy --noconfirm --needed %PREFIX%-gcc %PREFIX%-make %PREFIX%-cmake %PREFIX%-glfw %PREFIX%-glm 2>&1 | tail -20"
if %errorlevel% neq 0 (
    echo [ERROR] pacman failed. Retry after checking the connection.
    pause
    exit /b 1
)
echo Verifying:
"%BASH%" -lc "export PATH=/%ENV%/bin:$PATH; gcc --version | head -1; mingw32-make --version | head -1; cmake --version | head -1"
echo.
echo Done. Read BUILD.bat to build.
pause
exit /b 0
