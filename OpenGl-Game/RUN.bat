@echo off
echo Running OpenGL Game...

REM Check if executable exists
if not exist "build\bin\game.exe" (
    echo Executable not found: build\bin\game.exe
    echo Please run BUILD.bat first to compile the game.
    pause
    exit /b 1
)

REM Check for required DLLs
set MISSING_DLLS=0

REM Check for OpenGL32.dll (should be system DLL)
where opengl32.dll >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Warning: OpenGL32.dll not found in PATH
    echo This should be a system DLL, but if the game fails to run,
    echo please ensure your graphics drivers are properly installed.
)

REM Check for GLFW DLL (common locations)
set GLFW_FOUND=0
if exist "glfw3.dll" set GLFW_FOUND=1
if exist "libglfw3.dll" set GLFW_FOUND=1
if exist "build\bin\glfw3.dll" set GLFW_FOUND=1
if exist "build\bin\libglfw3.dll" set GLFW_FOUND=1

if %GLFW_FOUND% EQU 0 (
    echo Warning: GLFW DLL not found
    echo The game may fail to run without the GLFW library.
    echo Please ensure GLFW is installed and its DLL is in the PATH
    echo or in the same directory as the executable.
)

echo Starting game...
cd build\bin
game.exe

if %ERRORLEVEL% NEQ 0 (
    echo Game exited with error code: %ERRORLEVEL%
    echo.
    echo Possible solutions:
    echo 1. Ensure graphics drivers are up to date
    echo 2. Install GLFW library and place DLL in build\bin\
    echo 3. Check that all required dependencies are available
)

pause
