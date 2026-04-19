@echo off
REM Simple Windows Build Script (fallback if mingw32-make not in PATH)

echo === Simple Build Script ===
echo.

REM Try different make commands
echo Trying to find make command...

REM Check for common make locations
set FOUND_MAKE=0

if exist "C:\msys64\mingw64\bin\mingw32-make.exe" (
    echo Found mingw32-make at C:\msys64\mingw64\bin\
    set MAKE_CMD="C:\msys64\mingw64\bin\mingw32-make.exe"
    set FOUND_MAKE=1
    goto :try_build
)

if exist "C:\msys64\ucrt64\bin\mingw32-make.exe" (
    echo Found mingw32-make at C:\msys64\ucrt64\bin\
    set MAKE_CMD="C:\msys64\ucrt64\bin\mingw32-make.exe"
    set FOUND_MAKE=1
    goto :try_build
)

REM Check if make is in PATH
where make >nul 2>nul
if %errorlevel% equ 0 (
    echo Found make in PATH
    set MAKE_CMD=make
    set FOUND_MAKE=1
    goto :try_build
)

REM Check if mingw32-make is in PATH
where mingw32-make >nul 2>nul
if %errorlevel% equ 0 (
    echo Found mingw32-make in PATH
    set MAKE_CMD=mingw32-make
    set FOUND_MAKE=1
    goto :try_build
)

echo ERROR: No make command found!
echo.
echo Please install MinGW-w64:
echo 1. Download MSYS2 from https://www.msys2.org/
echo 2. Install MSYS2
echo 3. Open MSYS2 MINGW64 terminal
echo 4. Run: pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-glfw
echo 5. Add C:\msys64\mingw64\bin to your Windows PATH
echo.
pause
exit /b 1

:try_build
echo Using make command: %MAKE_CMD%
echo.

REM Clean previous build
echo Cleaning previous build...
%MAKE_CMD% -f Makefile.windows clean

if %errorlevel% neq 0 (
    echo ERROR: Clean failed
    pause
    exit /b 1
)

REM Build the game
echo Building game and server...
%MAKE_CMD% -f Makefile.windows

if %errorlevel% equ 0 (
    echo.
    echo === Build Successful! ===
    echo Executables created in build/bin/
    echo   - game.exe
    echo   - server.exe
    echo.
    echo To run the game:
    echo   build\bin\game.exe
    echo.
    echo To run the server:
    echo   build\bin\server.exe
    echo.
) else (
    echo.
    echo === Build Failed! ===
    echo Please check error messages above.
    echo.
    echo Common issues:
    echo - Missing GLFW library: pacman -S mingw-w64-x86_64-glfw
    echo - Missing GLM library: pacman -S mingw-w64-x86_64-glm
    echo - Wrong compiler version
    echo.
)

pause
