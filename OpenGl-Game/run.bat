@echo off
REM Game launcher for Windows

echo === OpenGL Game Launcher ===
echo.

REM If already in MSYS2, run directly
if defined MSYSTEM (
    echo Running game from MSYS2...
    ./build/bin/game.exe
    goto :end
)

REM Try to launch MSYS2 and run game
set MSYS2_PATH=C:\msys64
if not exist "%MSYS2_PATH%\msys2.exe" set MSYS2_PATH=C:\msys2

if exist "%MSYS2_PATH%\msys2.exe" (
    echo Launching game via MSYS2...
    set CURRENT_DIR=%CD%
    "%MSYS2_PATH%\msys2.exe" -mingw64 -defterm -here -no-start -ucrt64 -shell bash -lc "cd '%CURRENT_DIR%' && ./build/bin/game.exe"
    goto :end
)

REM Fallback: try Windows executable
echo MSYS2 not found, trying Windows executable...
if exist "build\bin\game.exe" (
    build\bin\game.exe
) else (
    echo Game not found. Run build.bat first.
    pause
)

:end
