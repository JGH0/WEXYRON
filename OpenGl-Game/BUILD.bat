@echo off
echo Building OpenGL Game...

if exist "C:\msys64\msys2.exe" (
    "C:\msys64\msys2.exe" -mingw64 -defterm -here -no-start -ucrt64 -shell bash -lc "cd '%CD%' && mingw32-make -f Makefile.windows clean && mingw32-make -f Makefile.windows && echo Build done! Run with: ./build/bin/game.exe && bash"
) else (
    echo MSYS2 not found at C:\msys64
    echo Install from https://www.msys2.org/
)
pause
