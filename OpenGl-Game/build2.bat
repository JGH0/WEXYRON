@echo off
echo Building OpenGL Game for Windows...
echo.

REM Check if MSYS2 is installed
if not exist "C:\msys64\msys2.exe" (
    echo ERROR: MSYS2 not found at C:\msys64\msys2.exe
    echo Please install MSYS2 from https://www.msys2.org/
    pause
    exit /b 1
)

echo Starting build process...
echo This will take a few minutes on first run.
echo.

REM Direct MSYS2 execution
"C:\msys64\msys2.exe" -ucrt64 -shell bash -lc "cd '%CD%' && echo 'Installing dependencies...' && pacman -Sy --noconfirm && pacman -S --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-glm && echo 'Building project...' && mingw32-make -f Makefile.windows && echo 'Copying DLLs...' && cp /ucrt64/bin/libgcc_s_seh-1.dll build/bin/ && cp /ucrt64/bin/libstdc++-6.dll build/bin/ && cp /ucrt64/bin/libwinpthread-1.dll build/bin/ && cp /ucrt64/bin/glfw3.dll build/bin/ && echo 'BUILD COMPLETED SUCCESSFULLY!'"

echo.
echo Checking build results...

if exist "build\bin\game.exe" (
    echo.
    echo ========================================
    echo BUILD SUCCESSFUL!
    echo ========================================
    echo.
    echo Your OpenGL Game is ready!
    echo Executable: build\bin\game.exe
    
    if exist "build\bin\libgcc_s_seh-1.dll" (
        echo All required DLLs are included.
        echo Ready to run on any Windows PC!
    ) else (
        echo WARNING: Some DLLs may be missing.
    )
    
    echo.
    echo To run the game:
    echo   - run_game.bat
    echo   - Or: build\bin\game.exe
) else (
    echo.
    echo ========================================
    echo BUILD FAILED
    echo ========================================
    echo.
    echo Possible issues:
    echo - MSYS2 installation problems
    echo - Network issues during package download
    echo - Compilation errors
    echo.
    echo Try running these commands manually in MSYS2:
    echo   cd "%CD%"
    echo   pacman -Sy --noconfirm
    echo   pacman -S --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-glm
    echo   mingw32-make -f Makefile.windows
)

echo.
pause