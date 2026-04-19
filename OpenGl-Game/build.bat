@echo off
REM Auto-build script for OpenGL Game

echo === Building OpenGL Game ===
echo.

REM If already in MSYS2, build directly
if defined MSYSTEM (
    echo Building in MSYS2...
    mingw32-make -f Makefile.windows clean
    mingw32-make -f Makefile.windows
    goto :end
)

REM Try to find and launch MSYS2
set MSYS2_PATH=C:\msys64
if not exist "%MSYS2_PATH%\msys2.exe" set MSYS2_PATH=C:\msys2

if exist "%MSYS2_PATH%\msys2.exe" (
    echo Launching MSYS2...
    set CURRENT_DIR=%CD%
    "%MSYS2_PATH%\msys2.exe" -mingw64 -defterm -here -no-start -ucrt64 -shell bash -lc "cd '%CURRENT_DIR%' && mingw32-make -f Makefile.windows clean && mingw32-make -f Makefile.windows && echo '' && echo === Build Complete! === && echo To run the game: && echo   ./build/bin/game.exe && echo '' && echo To run the server: && echo   ./build/bin/server.exe && echo '' && bash"
    goto :end
)

REM Fallback: try regular Windows build
echo MSYS2 not found, trying Windows build...
where mingw32-make >nul 2>nul
if %errorlevel% neq 0 (
    echo ERROR: Install MSYS2 from https://www.msys2.org/
    pause
    exit /b 1
)

mingw32-make -f Makefile.windows clean
mingw32-make -f Makefile.windows

:end
if %errorlevel% equ 0 (
    echo.
    echo === Build Successful! ===
    echo Executables in build/bin/
) else (
    echo === Build Failed! ===
)
pause
