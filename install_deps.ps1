# Install the MSYS2 packages needed by Makefile.windows + CMake.
# Picks UCRT64 by default (MINGW64 if -Env MINGW64). No SDL2/GLEW: the
# game only needs gcc + make + cmake + glfw + glm.
param([string]$Env = "ucrt64")

$Prefix = "mingw-w64-ucrt-x86_64"
if ($Env -eq "mingw64") { $Prefix = "mingw-w64-x86_64" }

$Root = "C:\msys64"
if (-not (Test-Path "$Root\usr\bin\bash.exe")) { $Root = "C:\msys2" }
$Bash = "$Root\usr\bin\bash.exe"
if (-not (Test-Path $Bash)) {
    Write-Host "[ERROR] MSYS2 not found. Install from https://www.msys2.org/ first." -ForegroundColor Red
    return
}
Write-Host "Installing $Prefix packages into $Env ..."
& $Bash -lc "pacman -Sy --noconfirm --needed $Prefix-gcc $Prefix-make $Prefix-cmake $Prefix-glfw $Prefix-glm 2>&1 | tail -20"
Write-Host "Verifying:"
& $Bash -lc "export PATH=/$Env/bin:`$PATH; gcc --version | head -1; mingw32-make --version | head -1; cmake --version | head -1"
Write-Host "Done. Read BUILD.bat to build."
