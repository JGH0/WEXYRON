@echo off
echo Running OpenGL Game...

if exist "C:\msys64\msys2.exe" (
    "C:\msys64\msys2.exe" -mingw64 -defterm -here -no-start -ucrt64 -shell bash -lc "cd '%CD%' && ./build/bin/game.exe"
) else (
    echo MSYS2 not found at C:\msys64
    echo Install from https://www.msys2.org/
)
pause
