@echo off
cd /d "%~dp0"
color 0E

echo [96m=== OpenGL Game Server Launcher ===[0m
echo.

REM Check if server executable exists
if not exist "build\bin\server.exe" (
    echo [INFO] Server not built yet. Building first...
    call build.bat
    if %errorlevel% neq 0 (
        echo [ERROR] Build failed. Cannot start server.
        echo.
        echo Possible solutions:
        echo   1. Check build.bat output for error messages
        echo   2. Ensure MSYS2 is properly installed
        echo   3. Try running build.bat as administrator
        pause
        exit /b 1
    )
)

REM Verify server executable exists after build
if not exist "build\bin\server.exe" (
    echo [ERROR] Server executable not found after build!
    echo.
    echo This could indicate:
    echo   - Compilation errors in server code
    echo   - Build process failed silently
    echo   - Insufficient permissions
    echo.
    echo Try running build.bat manually to see detailed error messages.
    pause
    exit /b 1
)

REM Find MSYS2 for DLL copying
set MSYS2_FOUND=0
set MSYS2_UCRT64_PATH=""

if exist "C:\msys64\ucrt64\bin" (
    set MSYS2_FOUND=1
    set MSYS2_UCRT64_PATH="C:\msys64\ucrt64\bin"
) else if exist "C:\msys2\ucrt64\bin" (
    set MSYS2_FOUND=1
    set MSYS2_UCRT64_PATH="C:\msys2\ucrt64\bin"
)

REM Verify DLLs are present with fallbacks
echo Checking runtime files...
set MISSING_FILES=0
set DLL_WARNINGS=0

if not exist "build\bin\libgcc_s_seh-1.dll" (
    echo [INFO] libgcc_s_seh-1.dll missing, attempting to copy...
    if %MSYS2_FOUND% equ 1 (
        copy %MSYS2_UCRT64_PATH%\libgcc_s_seh-1.dll "build\bin\" >nul 2>&1
        if %errorlevel% neq 0 (
            set MISSING_FILES=1
            echo [WARNING] Could not copy libgcc_s_seh-1.dll
        ) else (
            echo [OK] libgcc_s_seh-1.dll copied
        )
    ) else (
        set MISSING_FILES=1
        set DLL_WARNINGS=1
        echo [WARNING] MSYS2 not found, cannot copy libgcc_s_seh-1.dll
    )
)

if not exist "build\bin\libstdc++-6.dll" (
    echo [INFO] libstdc++-6.dll missing, attempting to copy...
    if %MSYS2_FOUND% equ 1 (
        copy %MSYS2_UCRT64_PATH%\libstdc++-6.dll "build\bin\" >nul 2>&1
        if %errorlevel% neq 0 (
            set MISSING_FILES=1
            echo [WARNING] Could not copy libstdc++-6.dll
        ) else (
            echo [OK] libstdc++-6.dll copied
        )
    ) else (
        set MISSING_FILES=1
        echo [WARNING] MSYS2 not found, cannot copy libstdc++-6.dll
    )
)

if not exist "build\bin\libwinpthread-1.dll" (
    echo [INFO] libwinpthread-1.dll missing, attempting to copy...
    if %MSYS2_FOUND% equ 1 (
        copy %MSYS2_UCRT64_PATH%\libwinpthread-1.dll "build\bin\" >nul 2>&1
        if %errorlevel% neq 0 (
            set MISSING_FILES=1
            echo [WARNING] Could not copy libwinpthread-1.dll
        ) else (
            echo [OK] libwinpthread-1.dll copied
        )
    ) else (
        set MISSING_FILES=1
        echo [WARNING] MSYS2 not found, cannot copy libwinpthread-1.dll
    )
)

REM Check for Windows networking DLL
echo [INFO] Checking Windows networking components...
if not exist "C:\Windows\System32\ws2_32.dll" (
    echo [ERROR] Windows networking components missing!
    echo This indicates a problem with your Windows installation.
    echo Try installing Windows updates or repairing Windows.
    set MISSING_FILES=1
) else (
    echo [OK] Windows networking components found
)

REM Network diagnostics
echo.
echo [INFO] Running network diagnostics...
echo.

REM Check if port is available
set SERVER_PORT=12345
echo Checking port %SERVER_PORT% availability...

REM Try to check if port is in use
netstat -an | find ":%SERVER_PORT%" >nul 2>&1
if %errorlevel% equ 0 (
    echo [WARNING] Port %SERVER_PORT% appears to be in use!
    echo.
    echo This could prevent the server from starting.
    echo Options:
    echo   1. Use a different port (edit server config if available)
    echo   2. Stop the process using this port
    echo   3. Continue anyway (server will handle port conflicts)
    echo.
    echo Continue anyway? (y/n)
    set /p continue="> "
    if /i not "%continue%"=="y" (
        echo Server startup cancelled.
        pause
        exit /b 1
    )
) else (
    echo [OK] Port %SERVER_PORT% appears to be available
)

REM Check network connectivity
echo Testing network connectivity...
ping -n 1 127.0.0.1 >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] Local network interface working
) else (
    echo [WARNING] Local network interface may have issues
)

REM Check firewall status (basic check)
echo [INFO] Note: Windows Firewall may block network connections
echo If players cannot connect, check:
echo   - Windows Firewall settings
echo   - Antivirus firewall settings
echo   - Network configuration

if %MISSING_FILES% equ 1 (
    echo.
    echo [WARNING] Some files are missing. Server may not run properly.
    echo Consider running build.bat to ensure all files are present.
    echo.
)

echo.
echo [READY] Starting OpenGL Game Server...
echo.
echo Server Information:
echo   - Default Port: %SERVER_PORT%
echo   - Protocol: TCP/UDP
echo   - Max Players: 32
echo   - Network: IPv4/IPv6 (auto-detect)
echo.
echo Server Controls:
echo   - Press Ctrl+C to stop server
echo   - Server will save state on shutdown
echo   - Log output will appear below
echo.
echo Client Connection:
echo   - Use server IP address and port %SERVER_PORT%
echo   - Example: 192.168.1.100:%SERVER_PORT%
echo   - Localhost: 127.0.0.1:%SERVER_PORT%
echo.
echo Starting server...

REM Get local IP address for client connection
echo [INFO] Your local IP addresses:
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| find "IPv4"') do echo   - %%a:%SERVER_PORT%
echo.

REM Launch server with proper working directory
build\bin\server.exe

set SERVER_RESULT=%errorlevel%

echo.
if %SERVER_RESULT% equ 0 (
    echo [SUCCESS] Server exited normally.
    echo Server state was saved successfully.
) else (
    echo [ERROR] Server exited with code %SERVER_RESULT%
    echo.
    echo Network troubleshooting guide:
    echo.
    echo 1. Port Issues:
    echo    - Port %SERVER_PORT% may be blocked by firewall
    echo    - Another process might be using this port
    echo    - Try running server as administrator
    echo.
    echo 2. Firewall Configuration:
    echo    - Windows Firewall: Allow server.exe through
    echo    - Antivirus: Add exception for server.exe
    echo    - Router: Port forward %SERVER_PORT% (if hosting online)
    echo.
    echo 3. Network Issues:
    echo    - Check network cable/Wi-Fi connection
    echo    - Verify IP address is correct
    echo    - Test with local connection first (127.0.0.1)
    echo.
    echo 4. Server Configuration:
    echo    - Check server config files (if any)
    echo    - Verify server has proper permissions
    echo    - Ensure all DLL files are present
    echo.
    echo 5. Advanced Network Debugging:
    echo    - Run: netstat -an | find ":%SERVER_PORT%"
    echo    - Check Windows Event Viewer for network errors
    echo    - Test with different port numbers
    echo    - Disable VPN/Proxy temporarily
    echo.
    echo Common error codes:
    if %SERVER_RESULT% equ 1 (
        echo   - General server startup failure
    )
    if %SERVER_RESULT% equ 10061 (
        echo   - Connection refused (port blocked)
    )
    if %SERVER_RESULT% equ 10048 (
        echo   - Address already in use (port conflict)
    )
    if %SERVER_RESULT% equ 10060 (
        echo   - Connection timeout
    )
    if %SERVER_RESULT% equ -1073741515 (
        echo   - Missing DLL files
    )
    if %SERVER_RESULT% equ -1073741819 (
        echo   - Access violation (permissions)
    )
    echo.
    echo Quick fixes to try:
    echo   1. Run server as administrator
    echo   2. Temporarily disable Windows Firewall
    echo   3. Use a different port number
    echo   4. Restart network interface
    echo   5. Rebuild with build.bat
)

echo.
pause
