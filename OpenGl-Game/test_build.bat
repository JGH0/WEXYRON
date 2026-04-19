@echo off
echo Testing Windows build...

REM Check if MSYS2 exists
if exist "C:\msys64\msys2.exe" (
    echo Found MSYS2 at C:\msys64
    set MSYS2_CMD="C:\msys64\msys2.exe"
) else if exist "C:\msys2\msys2.exe" (
    echo Found MSYS2 at C:\msys2
    set MSYS2_CMD="C:\msys2\msys2.exe"
) else (
    echo ERROR: MSYS2 not found
    pause
    exit /b 1
)

echo Testing build...
%MSYS2_CMD% -mingw64 -defterm -here -no-start -ucrt64 -shell bash -lc "cd '%CD%' && mingw32-make -f Makefile.windows clean && mingw32-make -f Makefile.windows && echo Build test complete!"

pause
