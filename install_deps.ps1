# PowerShell script to install MSYS2 dependencies
Write-Host "Installing MSYS2 development packages..."
Write-Host ""
Write-Host "Select MSYS2 environment:"
Write-Host "  1) UCRT64 (default)"
Write-Host "  2) MINGW64 (recommended for Makefile.windows)"
$envChoice = Read-Host "Choice (1/2)"
if ([string]::IsNullOrEmpty($envChoice)) { $envChoice = "1" }

$msys2Exe = ""
$pkgPrefix = ""
if ($envChoice -eq "2") {
    $msys2Exe = "C:\msys64\mingw64.exe"
    $pkgPrefix = "mingw-w64-x86_64"
    Write-Host "Selected: MINGW64"
} else {
    $msys2Exe = "C:\msys64\ucrt64.exe"
    $pkgPrefix = "mingw-w64-ucrt-x86_64"
    Write-Host "Selected: UCRT64"
}
Write-Host ""

# Update package database
Write-Host "Updating package database..."
& $msys2Exe -c "pacman -Sy --noconfirm"

# Install base development tools
Write-Host "Installing base development tools..."
& $msys2Exe -c "pacman -S --noconfirm $pkgPrefix-gcc $pkgPrefix-make $pkgPrefix-cmake"

# Install OpenGL and graphics libraries
Write-Host "Installing graphics libraries..."
& $msys2Exe -c "pacman -S --noconfirm $pkgPrefix-glfw $pkgPrefix-glm $pkgPrefix-glew"

# Install additional libraries
Write-Host "Installing additional libraries..."
& $msys2Exe -c "pacman -S --noconfirm $pkgPrefix-SDL2"

Write-Host "Installation completed."
Write-Host "Verifying tools..."

# Check if tools are available
& $msys2Exe -c "which gcc"
& $msys2Exe -c "which make"
& $msys2Exe -c "which mingw32-make"

Write-Host "GCC version:"
& $msys2Exe -c "gcc --version | head -1"

Write-Host "Make version:"
& $msys2Exe -c "make --version | head -1"

Write-Host "Press any key to continue..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
